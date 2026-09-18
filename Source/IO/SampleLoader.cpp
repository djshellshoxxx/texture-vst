#include "SampleLoader.h"

#include "../Utility/DspHelpers.h"

namespace texture
{
    namespace
    {
        /** Reads at most `maxFrames` frames from a reader into a float buffer. */
        bool readIntoBuffer (juce::AudioFormatReader& reader,
                             juce::AudioBuffer<float>& destination,
                             juce::int64 maxFrames)
        {
            const auto framesToRead = static_cast<int> (juce::jmin (reader.lengthInSamples, maxFrames));

            if (framesToRead <= 0)
                return false;

            const auto channelsToRead = static_cast<int> (juce::jmax (1u, reader.numChannels));

            try
            {
                destination.setSize (channelsToRead, framesToRead, false, true, false);
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }

            reader.read (&destination, 0, framesToRead, 0, true, true);
            return true;
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
        LoadJob (SampleLoader& ownerToUse, juce::File fileToLoad, int generationToUse)
            : juce::ThreadPoolJob ("Texture load: " + fileToLoad.getFileName()),
              owner (ownerToUse),
              file (std::move (fileToLoad)),
              generation (generationToUse)
        {
        }

        JobStatus runJob() override
        {
            if (shouldExit())
                return jobHasFinished;

            auto result = owner.loadSynchronously (file);

            if (! shouldExit())
                owner.deliver (std::move (result), generation);

            return jobHasFinished;
        }

    private:
        SampleLoader& owner;
        juce::File file;
        int generation;
    };

    SampleLoader::SampleLoader()
    {
        formatManager.registerBasicFormats();
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

    void SampleLoader::loadAsync (const juce::File& file)
    {
        const auto generation = ++requestGeneration;
        pool.addJob (new LoadJob (*this, file, generation), true);
    }

    void SampleLoader::waitForPendingLoads (int timeoutMs)
    {
        pool.removeAllJobs (true, timeoutMs);
    }

    void SampleLoader::cancelPendingLoads()
    {
        ++requestGeneration;

        const juce::ScopedLock lock (resultLock);
        hasPendingResult = false;
        pendingResult = {};
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

        if (reader->lengthInSamples <= 0 || sourceChannels <= 0)
        {
            result.message = "Could not decode file. It may be corrupt.";
            result.severity = Severity::warning;
            return result;
        }

        const auto maxFrames = static_cast<juce::int64> (SampleBuffer::maxLengthSeconds * sourceRate);
        const auto wasTruncated = reader->lengthInSamples > maxFrames;

        juce::AudioBuffer<float> raw;

        if (! readIntoBuffer (*reader, raw, maxFrames))
        {
            result.message = "Not enough memory to load this file.";
            result.severity = Severity::error;
            return result;
        }

        auto stereo = foldToStereo (raw);
        raw.setSize (0, 0);

        const auto rate = targetSampleRateAtomic.load (std::memory_order_acquire);
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

    void SampleLoader::deliver (LoadResult&& result, int generation)
    {
        // Only the newest request is allowed to become active (05_SAMPLE_LOADER.md).
        if (generation != requestGeneration.load (std::memory_order_acquire))
            return;

        {
            const juce::ScopedLock lock (resultLock);
            pendingResult = std::move (result);
            hasPendingResult = true;
        }

        triggerAsyncUpdate();
    }

    void SampleLoader::handleAsyncUpdate()
    {
        LoadResult result;

        {
            const juce::ScopedLock lock (resultLock);

            if (! hasPendingResult)
                return;

            result = std::move (pendingResult);
            pendingResult = {};
            hasPendingResult = false;
        }

        if (callback)
            callback (result);
    }
}
