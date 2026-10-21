#include "SampleLoader.h"

#include "../Utility/DspHelpers.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

namespace texture
{
    namespace
    {
        enum class ReadStatus
        {
            success,
            insufficientMemory,
            decodeFailed
        };

        constexpr juce::int64 bytesPerFloat = static_cast<juce::int64> (sizeof (float));

        /** JUCE WAV readers zero-pad reads beyond EOF, so verify RIFF chunk bounds first. */
        bool hasCompleteWaveChunks (const juce::File& file)
        {
            juce::FileInputStream stream (file);

            if (! stream.openedOk())
                return false;

            juce::uint8 header[12] {};
            if (stream.read (header, 12) != 12)
                return true; // Let the format manager report short non-WAV files.

            const auto isRiff = header[0] == 'R' && header[1] == 'I'
                             && header[2] == 'F' && header[3] == 'F';
            const auto isWave = header[8] == 'W' && header[9] == 'A'
                             && header[10] == 'V' && header[11] == 'E';

            if (! isRiff || ! isWave)
                return true;

            const auto readLittleEndian32 = [] (const juce::uint8* bytes)
            {
                return static_cast<std::uint64_t> (bytes[0])
                     | (static_cast<std::uint64_t> (bytes[1]) << 8)
                     | (static_cast<std::uint64_t> (bytes[2]) << 16)
                     | (static_cast<std::uint64_t> (bytes[3]) << 24);
            };

            const auto fileLength = static_cast<std::uint64_t> (stream.getTotalLength());
            const auto riffEnd = readLittleEndian32 (header + 4) + 8u;

            if (riffEnd < 12u || riffEnd > fileLength)
                return false;

            std::uint64_t position = 12u;
            bool hasFormatChunk = false;
            bool hasDataChunk = false;

            while (position < riffEnd)
            {
                if (riffEnd - position < 8u)
                    return false;

                juce::uint8 chunkHeader[8] {};
                if (! stream.setPosition (static_cast<juce::int64> (position))
                    || stream.read (chunkHeader, 8) != 8)
                    return false;

                const auto chunkLength = readLittleEndian32 (chunkHeader + 4);
                const auto chunkDataEnd = position + 8u + chunkLength;

                if (chunkDataEnd < position || chunkDataEnd > riffEnd)
                    return false;

                hasFormatChunk |= chunkHeader[0] == 'f' && chunkHeader[1] == 'm'
                               && chunkHeader[2] == 't' && chunkHeader[3] == ' ';
                hasDataChunk |= chunkHeader[0] == 'd' && chunkHeader[1] == 'a'
                             && chunkHeader[2] == 't' && chunkHeader[3] == 'a';

                const auto nextChunk = chunkDataEnd + (chunkLength & 1u);
                if (nextChunk > riffEnd)
                    return false;
                position = nextChunk;
            }

            return position == riffEnd && hasFormatChunk && hasDataChunk;
        }

        bool checkedBufferBytes (int channels, juce::int64 frames, juce::int64& bytes) noexcept
        {
            if (channels <= 0 || frames < 0
                || frames > std::numeric_limits<juce::int64>::max() / channels / bytesPerFloat)
                return false;

            bytes = static_cast<juce::int64> (channels) * frames * bytesPerFloat;
            return true;
        }

        bool conversionFitsMemoryLimit (int sourceChannels,
                                       juce::int64 sourceFrames,
                                       juce::int64 outputFrames) noexcept
        {
            const auto readChannels = juce::jlimit (1, 2, sourceChannels);
            juce::int64 rawBytes = 0;
            juce::int64 stereoBytes = 0;
            juce::int64 outputBytes = 0;

            if (! checkedBufferBytes (readChannels, sourceFrames, rawBytes)
                || ! checkedBufferBytes (2, sourceFrames, stereoBytes)
                || ! checkedBufferBytes (2, outputFrames, outputBytes))
                return false;

            // The raw decode and stereo fold coexist briefly. The folded sample and
            // its resampled copy coexist during rate conversion. A same-rate copy is
            // covered by the second sum as well.
            const auto decodeAndFoldBytes = rawBytes + stereoBytes;
            const auto foldAndResampleBytes = stereoBytes + outputBytes;
            return decodeAndFoldBytes <= SampleLoader::maxConversionWorkingBytes
                && foldAndResampleBytes <= SampleLoader::maxConversionWorkingBytes;
        }

        /** Reads at most `maxFrames` frames from a reader into a float buffer. */
        ReadStatus readIntoBuffer (juce::AudioFormatReader& reader,
                                   juce::AudioBuffer<float>& destination,
                                   juce::int64 maxFrames)
        {
            const auto framesToRead = static_cast<int> (juce::jmin (reader.lengthInSamples, maxFrames));

            if (framesToRead <= 0)
                return ReadStatus::decodeFailed;

            // TEXTURE is stereo internally. Never allocate all channels from an
            // untrusted multichannel file only to discard channel 3+ immediately.
            const auto channelsToRead = juce::jlimit (1, 2, static_cast<int> (reader.numChannels));

            try
            {
                destination.setSize (channelsToRead, framesToRead, false, true, false);
            }
            catch (const std::bad_alloc&)
            {
                return ReadStatus::insufficientMemory;
            }

            try
            {
                if (! reader.read (&destination, 0, framesToRead, 0, true, true))
                    return ReadStatus::decodeFailed;
            }
            catch (const std::exception&)
            {
                return ReadStatus::decodeFailed;
            }

            return ReadStatus::success;
        }

        /** Folds any channel count down to stereo: L = ch0, R = ch1, rest discarded. */
        juce::AudioBuffer<float> foldToStereo (const juce::AudioBuffer<float>& source)
        {
            const auto numSamples = source.getNumSamples();
            juce::AudioBuffer<float> stereo (2, numSamples);

            if (source.getNumChannels() <= 0 || numSamples <= 0)
            {
                stereo.clear();
                return stereo;
            }

            stereo.copyFrom (0, 0, source, 0, 0, numSamples);
            stereo.copyFrom (1, 0, source, source.getNumChannels() > 1 ? 1 : 0, 0, numSamples);

            return stereo;
        }

        /** Resamples a stereo buffer to `targetRate` using Lagrange interpolation. */
        juce::AudioBuffer<float> resampleStereo (const juce::AudioBuffer<float>& source,
                                                 double sourceRate,
                                                 double targetRate)
        {
            if (sourceRate <= 0.0 || targetRate <= 0.0
                || juce::approximatelyEqual (sourceRate, targetRate))
            {
                juce::AudioBuffer<float> copy (source);
                return copy;
            }

            const auto ratio = sourceRate / targetRate;
            const auto outputLength = juce::jmax (1, juce::roundToInt (source.getNumSamples() / ratio));

            juce::AudioBuffer<float> resampled (source.getNumChannels(), outputLength);
            resampled.clear();

            for (int ch = 0; ch < source.getNumChannels(); ++ch)
            {
                juce::LagrangeInterpolator interpolator;
                interpolator.reset();
                interpolator.process (ratio,
                                      source.getReadPointer (ch),
                                      resampled.getWritePointer (ch),
                                      outputLength);
            }

            return resampled;
        }
    }

    /** One decode request. Holds the generation it was queued at so a stale result
        can be recognised and discarded on completion.
    */
    class SampleLoader::LoadJob : public juce::ThreadPoolJob
    {
    public:
        LoadJob (SampleLoader& ownerToUse, juce::File fileToLoad,
                 int generationToUse, int requestTagToUse)
            : juce::ThreadPoolJob ("Texture load: " + fileToLoad.getFileName()),
              owner (ownerToUse),
              file (std::move (fileToLoad)),
              generation (generationToUse),
              requestTag (requestTagToUse)
        {
        }

        JobStatus runJob() override
        {
            if (shouldExit())
                return jobHasFinished;

            auto result = owner.loadSynchronously (file);
            result.requestTag = requestTag;

            if (! shouldExit())
                owner.deliver (std::move (result), generation, requestTag);

            return jobHasFinished;
        }

    private:
        SampleLoader& owner;
        juce::File file;
        int generation;
        int requestTag;
    };

    SampleLoader::SampleLoader()
    {
        formatManager.registerBasicFormats();

        for (auto& generation : requestGenerations)
            generation.store (0, std::memory_order_relaxed);
    }

    int SampleLoader::sanitiseRequestTag (int requestTag) noexcept
    {
        return juce::jlimit (0, maxRequestTags - 1, requestTag);
    }

    SampleLoader::~SampleLoader()
    {
        cancelPendingUpdate();
        pool.removeAllJobs (true, 5000);
    }

    void SampleLoader::prepare (double hostSampleRate)
    {
        targetSampleRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
        targetSampleRateAtomic.store (targetSampleRate, std::memory_order_release);
    }

    void SampleLoader::setCallback (Callback newCallback)
    {
        callback = std::move (newCallback);
    }

    void SampleLoader::loadAsync (const juce::File& file, int requestTag)
    {
        const auto tag = sanitiseRequestTag (requestTag);
        const auto generation = ++requestGenerations[static_cast<size_t> (tag)];
        pool.addJob (new LoadJob (*this, file, generation, tag), true);
    }

    void SampleLoader::waitForPendingLoads (int timeoutMs)
    {
        pool.removeAllJobs (true, timeoutMs);
    }

    void SampleLoader::cancelPendingLoads()
    {
        for (auto& generation : requestGenerations)
            ++generation;

        const juce::ScopedLock lock (resultLock);
        pendingResults.clear();
    }

    void SampleLoader::cancelPendingLoad (int requestTag)
    {
        const auto tag = sanitiseRequestTag (requestTag);
        ++requestGenerations[static_cast<size_t> (tag)];

        const juce::ScopedLock lock (resultLock);
        pendingResults.erase (
            std::remove_if (pendingResults.begin(), pendingResults.end(),
                            [tag] (const LoadResult& result) { return result.requestTag == tag; }),
            pendingResults.end());
    }

    bool SampleLoader::isBusy() const
    {
        return pool.getNumJobs() > 0;
    }

    bool SampleLoader::isPresetFile (const juce::File& file)
    {
        return file.getFileExtension().equalsIgnoreCase (presetExtension);
    }

    juce::String SampleLoader::getSupportedWildcard() const
    {
        return formatManager.getWildcardForAllFormats();
    }

    bool SampleLoader::isSupportedAudioFile (const juce::File& file) const
    {
        return formatManager.findFormatForFileExtension (file.getFileExtension()) != nullptr;
    }

    SampleLoader::LoadResult SampleLoader::loadSynchronously (const juce::File& file)
    {
        LoadResult result;
        result.file = file;

        if (file.isDirectory())
        {
            result.message = "Please drop an audio file, not a folder.";
            result.severity = Severity::warning;
            return result;
        }

        if (! file.existsAsFile())
        {
            result.message = "File not found: " + file.getFileName();
            result.severity = Severity::warning;
            return result;
        }

        if (file.getSize() > maxFileSizeBytes)
        {
            result.message = "File too large. Maximum 100 MB.";
            result.severity = Severity::warning;
            return result;
        }

        if (! hasCompleteWaveChunks (file))
        {
            result.message = "Could not decode file. It may be truncated or corrupt.";
            result.severity = Severity::warning;
            return result;
        }

        std::unique_ptr<juce::AudioFormatReader> reader;

        try
        {
            reader.reset (formatManager.createReaderFor (file));
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog (juce::String ("[warn] [loader] Reader threw: ") + e.what());
            reader.reset();
        }

        if (reader == nullptr)
        {
            result.message = formatManager.findFormatForFileExtension (file.getFileExtension()) == nullptr
                ? juce::String ("Unsupported format. Try WAV, AIFF, FLAC, MP3, OGG.")
                : juce::String ("Could not decode file. It may be corrupt.");
            result.severity = Severity::warning;
            return result;
        }

        const auto sourceRate = reader->sampleRate > 0.0 ? reader->sampleRate : targetSampleRateAtomic.load();
        const auto sourceChannels = static_cast<int> (reader->numChannels);

        if (! std::isfinite (sourceRate) || sourceRate < 1000.0 || sourceRate > 768000.0)
        {
            result.message = "Could not decode file. Its sample rate is invalid.";
            result.severity = Severity::warning;
            return result;
        }

        if (reader->lengthInSamples <= 0 || sourceChannels <= 0)
        {
            result.message = "Could not decode file. It may be corrupt.";
            result.severity = Severity::warning;
            return result;
        }

        const auto maxFrames = static_cast<juce::int64> (SampleBuffer::maxLengthSeconds * sourceRate);
        const auto wasTruncated = reader->lengthInSamples > maxFrames;
        const auto framesToRead = juce::jmin (reader->lengthInSamples, maxFrames);

        const auto rate = targetSampleRateAtomic.load (std::memory_order_acquire);

        if (! std::isfinite (rate) || rate < 1000.0 || rate > 768000.0)
        {
            result.message = "Could not decode file. The host sample rate is invalid.";
            result.severity = Severity::warning;
            return result;
        }

        const auto outputFramesAsDouble = static_cast<double> (framesToRead) * rate / sourceRate;

        if (! std::isfinite (outputFramesAsDouble) || outputFramesAsDouble < 1.0
            || outputFramesAsDouble > static_cast<double> (std::numeric_limits<int>::max()))
        {
            result.message = "Could not decode file. The converted sample would be too large.";
            result.severity = Severity::warning;
            return result;
        }

        const auto outputFrames = static_cast<juce::int64> (std::llround (outputFramesAsDouble));

        if (! conversionFitsMemoryLimit (sourceChannels, framesToRead, outputFrames))
        {
            result.message = "Sample is too large to convert within the 128 MB memory limit.";
            result.severity = Severity::warning;
            return result;
        }

        try
        {
            juce::AudioBuffer<float> raw;
            const auto readStatus = readIntoBuffer (*reader, raw, maxFrames);

            if (readStatus != ReadStatus::success)
            {
                result.message = readStatus == ReadStatus::insufficientMemory
                    ? juce::String ("Not enough memory to load this file.")
                    : juce::String ("Could not decode file. It may be truncated or corrupt.");
                result.severity = readStatus == ReadStatus::insufficientMemory ? Severity::error
                                                                                : Severity::warning;
                return result;
            }

            auto stereo = foldToStereo (raw);
            raw.setSize (0, 0);

            auto resampled = resampleStereo (stereo, sourceRate, rate);
            stereo.setSize (0, 0);

            dsp::sanitiseBuffer (resampled);

            result.buffer = std::make_shared<const SampleBuffer> (std::move (resampled),
                                                                  rate,
                                                                  file,
                                                                  sourceChannels,
                                                                  sourceRate,
                                                                  wasTruncated);
            result.succeeded = true;
        }
        catch (const std::bad_alloc&)
        {
            result.buffer.reset();
            result.message = "Not enough memory to convert this sample.";
            result.severity = Severity::error;
            return result;
        }

        // Informational messages: the file still loaded.
        if (wasTruncated)
        {
            result.message = "File truncated to 30 seconds.";
            result.severity = Severity::info;
        }
        else if (sourceChannels > 2)
        {
            result.message = "File has " + juce::String (sourceChannels)
                           + " channels. Using channels 1 and 2 only.";
            result.severity = Severity::info;
        }

        return result;
    }

    void SampleLoader::deliver (LoadResult&& result, int generation, int requestTag)
    {
        const auto tag = sanitiseRequestTag (requestTag);

        // Only the newest request in this lane is allowed to become active.
        if (generation != requestGenerations[static_cast<size_t> (tag)].load (std::memory_order_acquire))
            return;

        {
            const juce::ScopedLock lock (resultLock);
            pendingResults.push_back (std::move (result));
        }

        triggerAsyncUpdate();
    }

    void SampleLoader::handleAsyncUpdate()
    {
        std::vector<LoadResult> results;

        {
            const juce::ScopedLock lock (resultLock);

            if (pendingResults.empty())
                return;

            results.swap (pendingResults);
        }

        if (! callback)
            return;

        for (const auto& result : results)
            callback (result);
    }
}
