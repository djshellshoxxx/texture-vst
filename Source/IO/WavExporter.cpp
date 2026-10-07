#include "WavExporter.h"
#include "OfflineRenderTimeline.h"
#include "OfflinePatchMath.h"

#include "../Effects/EffectsChain.h"
#include "../Engine/GranularEngine.h"
#include "../Modulation/MorphLFO.h"
#include "../Utility/DspHelpers.h"

#include <memory>

namespace texture
{
    namespace
    {
        constexpr int renderBlockSize = 512;

        /** Longest bounce we will produce, as a guard against absurd settings. */
        constexpr double maxRenderSeconds = 60.0;
    }

    juce::Array<int> WavExporter::getSupportedBitDepths()
    {
        return { 16, 24, 32 };
    }

    juce::Array<double> WavExporter::getSupportedSampleRates()
    {
        return { 44100.0, 48000.0, 88200.0, 96000.0 };
    }

    juce::String WavExporter::describeQuality (const Options& options)
    {
        return juce::String (options.sampleRate / 1000.0, 1) + " kHz / "
             + juce::String (options.bitDepth) + "-bit stereo WAV";
    }

    WavExporter::Result WavExporter::render (const std::array<SampleBufferPtr, 2>& samples,
                                             const RenderSettings& settings,
                                             const Options& options,
                                             const juce::File& destination)
    {
        Result result;
        result.file = destination;

        if (samples[0] == nullptr || ! samples[0]->isValid())
        {
            result.message = "Load a sample before exporting.";
            return result;
        }

        if (destination == juce::File())
        {
            result.message = "No destination chosen.";
            return result;
        }

        const auto sampleRate = options.sampleRate > 0.0 ? options.sampleRate : 48000.0;
        const auto hold = juce::jlimit (0.1, 30.0, options.holdSeconds);
        const auto tail = juce::jlimit (0.0, 30.0, options.tailSeconds);
        const auto gap = juce::jlimit (0.0, 5.0, options.gapSeconds);
        const auto releaseSeconds = juce::jlimit (0.01, 8.0, static_cast<double> (settings.releaseMs) * 0.001);

        // Layout: [short hit] [gap] [hit + 4 s tail]
        const auto firstHitSeconds = hold + releaseSeconds + 0.5;
        const auto secondHitSeconds = hold + releaseSeconds + tail;
        const auto totalSeconds = juce::jmin (maxRenderSeconds, firstHitSeconds + gap + secondHitSeconds);
        const auto totalSamples = static_cast<juce::int64> (totalSeconds * sampleRate);

        if (totalSamples <= 0)
        {
            result.message = "Nothing to render.";
            return result;
        }

        // Private engines: the live ones belong to the audio thread.
        std::array<std::unique_ptr<GranularEngine>, 2> engines {
            std::make_unique<GranularEngine>(),
            std::make_unique<GranularEngine>()
        };
        auto effects = std::make_unique<EffectsChain>();
        MorphLFO lfo;

        for (int layer = 0; layer < 2; ++layer)
        {
            engines[static_cast<size_t> (layer)]->prepare (sampleRate, renderBlockSize);
            engines[static_cast<size_t> (layer)]->setAdsrParameters (
                settings.attackMs, settings.decayMs, settings.sustain, settings.releaseMs);
            engines[static_cast<size_t> (layer)]->setSampleStart (
                juce::jlimit (0.0f, 1.0f, settings.sampleStart[static_cast<size_t> (layer)]));
        }

        lfo.prepare (sampleRate);
        lfo.setRateHz (settings.lfoRateHz);
        lfo.setDepth (settings.lfoDepth);

        effects->prepare (sampleRate, renderBlockSize, 2);
        effects->setReverbEnabled (settings.reverbOn);
        effects->setOutputGainDb (settings.gainDb);
        effects->getSecretEffect().setBitDepth (settings.gritBits);
        effects->getSecretEffect().setDownsample (settings.gritDownsample);
        effects->getSecretEffect().setMix (settings.gritMix);

        const auto layerGains = OfflinePatchMath::layerGains (settings.layerBlend);

        // Sample-accurate event schedule, in samples from the start of the render.
        const auto firstNoteOn = static_cast<juce::int64> (0);
        const auto firstNoteOff = static_cast<juce::int64> (hold * sampleRate);
        const auto secondNoteOn = static_cast<juce::int64> ((firstHitSeconds + gap) * sampleRate);
        const auto secondNoteOff = secondNoteOn + static_cast<juce::int64> (hold * sampleRate);

        std::unique_ptr<juce::FileOutputStream> stream;
        std::unique_ptr<juce::AudioFormatWriter> writer;

        try
        {
            destination.getParentDirectory().createDirectory();

            if (destination.existsAsFile() && ! destination.deleteFile())
            {
                result.message = "Could not overwrite " + destination.getFileName() + ".";
                return result;
            }

            stream = destination.createOutputStream();

            if (stream == nullptr)
            {
                result.message = "Could not write to " + destination.getFullPathName() + ".";
                return result;
            }

            juce::WavAudioFormat format;
            const auto bitDepth = getSupportedBitDepths().contains (options.bitDepth) ? options.bitDepth : 24;

            writer.reset (format.createWriterFor (stream.get(), sampleRate, 2,
                                                  bitDepth, {}, 0));

            if (writer == nullptr)
            {
                result.message = "Could not create a WAV writer for those settings.";
                return result;
            }

            // The writer owns the stream from here.
            stream.release();
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog (juce::String ("[error] [export] ") + e.what());
            result.message = "Export failed while opening the file.";
            return result;
        }

        juce::AudioBuffer<float> block (2, renderBlockSize);
        juce::int64 position = 0;
        bool firstNoteStarted = false;
        bool firstNoteStopped = false;
        bool secondNoteStarted = false;
        bool secondNoteStopped = false;
        bool writeFailed = false;

        using Timeline = OfflineRenderTimeline<4>;
        const Timeline::Events eventPositions {
            firstNoteOn, firstNoteOff, secondNoteOn, secondNoteOff
        };

        while (position < totalSamples && ! writeFailed)
        {
            // Apply every event scheduled exactly at this sample before rendering
            // the following span.
            if (! firstNoteStarted && position == firstNoteOn)
            {
                for (auto& engine : engines)
                    engine->noteOn (1, settings.midiNote, settings.velocity);
                firstNoteStarted = true;
            }

            if (firstNoteStarted && ! firstNoteStopped && position == firstNoteOff)
            {
                for (auto& engine : engines)
                    engine->noteOff (1, settings.midiNote);
                firstNoteStopped = true;
            }

            if (! secondNoteStarted && position == secondNoteOn)
            {
                for (auto& engine : engines)
                    engine->noteOn (1, settings.midiNote, settings.velocity);
                secondNoteStarted = true;
            }

            if (secondNoteStarted && ! secondNoteStopped && position == secondNoteOff)
            {
                for (auto& engine : engines)
                    engine->noteOff (1, settings.midiNote);
                secondNoteStopped = true;
            }

            const auto nominalBlockEnd = juce::jmin (position + static_cast<juce::int64> (renderBlockSize),
                                                     totalSamples);
            const auto boundary = Timeline::nextBoundary (position, nominalBlockEnd, eventPositions);
            const auto numSamples = static_cast<int> (boundary - position);

            if (numSamples <= 0)
            {
                position = nominalBlockEnd;
                continue;
            }

            block.clear();

            lfo.advanceBlock (numSamples);
            const auto effectiveMorph = lfo.applyTo (juce::jlimit (0.0f, 1.0f, settings.morph));
            const auto targets = deriveMorphTargets (effectiveMorph);
            effects->setMorphTargets (targets);

            juce::AudioBuffer<float> layerBlock (2, numSamples);

            for (int layer = 0; layer < 2; ++layer)
            {
                const auto& sample = samples[static_cast<size_t> (layer)];

                if (sample == nullptr || ! sample->isValid()
                    || layerGains[static_cast<size_t> (layer)] <= 0.0001f)
                    continue;

                layerBlock.clear();
                engines[static_cast<size_t> (layer)]->renderNextBlock (
                    sample, targets, layerBlock, 0, numSamples);

                for (int ch = 0; ch < 2; ++ch)
                    block.addFrom (ch, 0, layerBlock, ch, 0, numSamples,
                                   layerGains[static_cast<size_t> (layer)]);
            }

            effects->process (block, 0, numSamples);
            dsp::sanitiseBuffer (block);

            if (! writer->writeFromAudioSampleBuffer (block, 0, numSamples))
                writeFailed = true;

            position = boundary;
        }

        writer->flush();
        writer.reset();

        if (writeFailed)
        {
            result.message = "Export failed while writing audio.";
            return result;
        }

        result.succeeded = true;
        result.lengthSeconds = static_cast<double> (position) / sampleRate;
        result.fileSizeBytes = destination.getSize();
        result.message = "Exported " + destination.getFileName() + " ("
                       + juce::String (result.lengthSeconds, 2) + " s, "
                       + describeQuality (options) + ")";

        return result;
    }
}
