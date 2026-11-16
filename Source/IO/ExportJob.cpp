#include "ExportJob.h"

#include "AtomicFileWriter.h"
#include "OfflinePatchMath.h"

#include "../Effects/EffectsChain.h"
#include "../Engine/GrainRenderController.h"
#include "../Engine/GrainTargetResolver.h"
#include "../Engine/GranularEngine.h"
#include "../Modulation/HeldNoteCounter.h"
#include "../Modulation/MorphLFO.h"
#include "../Utility/DspHelpers.h"
#include "../Utility/VersionedRandomStreams.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>

namespace texture
{
    int ExportJob::countFullScaleSamples (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        // Counted before sanitising and never fixed: F06 has no implicit limiter.
        int count = 0;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);

            for (int i = 0; i < numSamples; ++i)
                if (std::abs (data[i]) >= 1.0f)
                    ++count;
        }

        return count;
    }

    WavExporter::Result ExportJob::run (const PatchSnapshot& snapshot,
                                        const ExportPlanner::Plan& plan,
                                        double requestedRate,
                                        int requestedDepth,
                                        const juce::File& destination,
                                        const std::atomic<bool>& cancel,
                                        std::atomic<juce::int64>& framesDone)
    {
        WavExporter::Result result;
        result.file = destination;
        framesDone.store (0, std::memory_order_relaxed);

        const auto& settings = snapshot.settings;

        if (settings.randomAlgorithmVersion != VersionedRandomStreams::algorithmVersion)
        {
            result.message = "This random-stream version is not supported by the offline renderer.";
            return result;
        }

        if (snapshot.samples[0] == nullptr || ! snapshot.samples[0]->isValid())
        {
            result.message = "Load a sample before exporting.";
            return result;
        }

        if (destination == juce::File())
        {
            result.message = "No destination chosen.";
            return result;
        }

        if (! plan.valid || plan.totalSamples <= 0)
        {
            result.message = plan.message.isNotEmpty() ? plan.message : "Nothing to render.";
            return result;
        }

        const auto sampleRate = requestedRate > 0.0 ? requestedRate : 48000.0;
        const auto totalSamples = plan.totalSamples;

        // Private engines and effects: the live ones belong to the audio thread.
        std::array<std::unique_ptr<GranularEngine>, 2> engines {
            std::make_unique<GranularEngine>(),
            std::make_unique<GranularEngine>()
        };
        auto effects = std::make_unique<EffectsChain>();
        MorphLFO lfo;

        for (int layer = 0; layer < 2; ++layer)
        {
            const auto index = static_cast<size_t> (layer);
            engines[index]->prepare (sampleRate, renderBlockSize);
            engines[index]->setAdsrParameters (settings.attackMs, settings.decayMs, settings.sustain, settings.releaseMs);
            engines[index]->setSourcePitchCalibration (settings.sourceRootMidiNotes[index], settings.sourceFineCents[index]);
            engines[index]->setSampleStart (juce::jlimit (0.0f, 1.0f, settings.sampleStart[index]));
            const auto layerSeed = settings.randomSeed ^ (static_cast<std::uint64_t> (layer + 1)
                                                           * UINT64_C (0x9E3779B97F4A7C15));
            engines[index]->setRandomConfig (settings.randomMode, layerSeed);
            engines[index]->setVoiceConfig (settings.mono);
            engines[index]->setScanSettings (settings.scan[index]);
            engines[index]->setAttackCloud (settings.attackCloud);
        }

        lfo.prepare (sampleRate);
        lfo.setRateHz (settings.lfoRateHz);
        lfo.setDepth (settings.lfoDepth);
        lfo.setShape (lfoShapeFromIndex (settings.lfoShape));
        lfo.setRetrigger (lfoRetriggerFromIndex (settings.lfoRetrigger));
        lfo.setPhaseOffset (settings.lfoPhase);
        lfo.setSlewMs (settings.lfoSlewMs);
        lfo.setSeed (settings.randomSeed);
        lfo.setCurve (settings.lfoCurve);

        // There is no host: a Host retrigger runs as a transport that is playing, at the
        // tempo the live patch last saw. The live effective rate covers the other modes.
        const auto exportRetrigger = lfoRetriggerFromIndex (settings.lfoRetrigger);

        if (exportRetrigger == LfoRetrigger::host)
            lfo.updateFromPlayHead (settings.hostBpm, true, std::nullopt);

        // Held keys for First note retrigger. Export events are all on channel 1.
        HeldNoteCounter heldNotes;

        // F13: the same resolver the live processor uses, so manual grain values sound the same.
        GrainTargetResolver grainResolver;
        grainResolver.prepare (sampleRate);

        // F19: the same grain-level composition as the live processor. There is no host
        // transport here, so the grid free-runs at the tempo the live patch last saw.
        auto renderController = std::make_unique<GrainRenderController>();
        renderController->prepare (sampleRate);

        effects->prepare (sampleRate, renderBlockSize, 2);
        effects->setReverbEnabled (settings.reverbOn);
        effects->setOutputGainDb (settings.gainDb);
        effects->getSecretEffect().setBitDepth (settings.gritBits);
        effects->getSecretEffect().setDownsample (settings.gritDownsample);
        effects->getSecretEffect().setMix (settings.gritMix);

        const auto layerGains = OfflinePatchMath::layerGains (settings.layerBlend);

        const auto bitDepth = WavExporter::getSupportedBitDepths().contains (requestedDepth) ? requestedDepth : 24;

        AtomicFileWriter writer (destination);
        juce::String error;
        auto stream = writer.openStream (error);

        if (stream == nullptr)
        {
            result.message = error;
            return result;
        }

        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> wav (format.createWriterFor (stream.get(), sampleRate, 2,
                                                                              bitDepth, {}, 0));

        if (wav == nullptr)
        {
            result.message = "Could not create a WAV writer for those settings.";
            return result;
        }

        // The writer owns the stream from here.
        stream.release();

        juce::AudioBuffer<float> block (2, renderBlockSize);
        juce::AudioBuffer<float> layerBlock (2, renderBlockSize);
        std::array<float, ExportJob::peakBins> peaks {};
        juce::int64 position = 0;
        size_t nextEvent = 0;
        int clipped = 0;
        bool writeFailed = false;

        framesDone.store (0, std::memory_order_relaxed);

        while (position < totalSamples && ! writeFailed)
        {
            if (cancel.load (std::memory_order_acquire))
            {
                result.cancelled = true;
                break;
            }

            // Apply every event at this exact sample before rendering the span after it.
            while (nextEvent < plan.events.size() && plan.events[nextEvent].sample <= position)
            {
                const auto& event = plan.events[nextEvent];

                if (event.sample == position)
                {
                    if (event.on)
                    {
                        // Applied at this sample, so the restart is exact in the offline render.
                        if (heldNotes.noteOn (1, event.note))
                            lfo.noteStarted();
                    }
                    else
                    {
                        heldNotes.noteOff (1, event.note);
                    }

                    for (auto& engine : engines)
                    {
                        if (event.on)
                            engine->noteOn (1, event.note, event.velocity);
                        else
                            engine->noteOff (1, event.note);
                    }
                }

                ++nextEvent;
            }

            const auto nominalBlockEnd = juce::jmin (position + static_cast<juce::int64> (renderBlockSize),
                                                     totalSamples);
            auto boundary = nominalBlockEnd;

            if (nextEvent < plan.events.size())
            {
                const auto upcoming = plan.events[nextEvent].sample;

                if (upcoming > position && upcoming < boundary)
                    boundary = upcoming;
            }

            const auto numSamples = static_cast<int> (boundary - position);

            if (numSamples <= 0)
            {
                position = nominalBlockEnd;
                continue;
            }

            block.clear();

            lfo.advanceBlock (numSamples);
            const auto effectiveMorph = lfo.applyTo (juce::jlimit (0.0f, 1.0f, settings.morph));
            auto targets = deriveMorphTargets (effectiveMorph);
            targets.tuneSemitones = settings.tuneSemitones;
            targets.reverseChance = settings.reverseChance;
            targets.shimmerAmount = settings.shimmerAmount;
            {
                GrainRenderInputs inputs;
                inputs.rhythm = settings.rhythm;
                inputs.morph = effectiveMorph;
                inputs.personality = personality::fromIndex (settings.personality);
                inputs.personalityIntensity = settings.personalityIntensity;

                TransportSpan transport;
                transport.bpm = settings.hostBpm;
                renderController->beginBlock (inputs, targets,
                                              settings.grain.follow[static_cast<std::size_t> (GrainField::size)],
                                              transport, numSamples);
            }
            const auto& schedule = renderController->renderSpan (0, numSamples);

            targets = grainResolver.advance (targets, settings.grain, numSamples).targets;
            effects->setToneSpace (settings.toneSpace);
            effects->setMorphTargets (targets);

            for (int layer = 0; layer < 2; ++layer)
            {
                const auto index = static_cast<size_t> (layer);
                const auto& sample = snapshot.samples[index];

                if (sample == nullptr || ! sample->isValid() || layerGains[index] <= 0.0001f)
                    continue;

                layerBlock.clear();
                auto layerTargets = targets;
                layerTargets.sourceFineCents = settings.sourceFineCents[index];
                engines[index]->renderNextBlock (sample, layerTargets, layerBlock, 0, numSamples, &schedule);

                for (int ch = 0; ch < 2; ++ch)
                    block.addFrom (ch, 0, layerBlock, ch, 0, numSamples, layerGains[index]);
            }

            effects->process (block, 0, numSamples);
            clipped += countFullScaleSamples (block, numSamples);
            dsp::sanitiseBuffer (block);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto frame = position + i;
                const auto bin = static_cast<size_t> (juce::jmin (static_cast<juce::int64> (peakBins - 1),
                                                                  frame * peakBins / totalSamples));
                const auto magnitude = juce::jmax (std::abs (block.getSample (0, i)), std::abs (block.getSample (1, i)));
                peaks[bin] = juce::jmax (peaks[bin], magnitude);
            }

            if (! wav->writeFromAudioSampleBuffer (block, 0, numSamples))
                writeFailed = true;

            position = boundary;
            framesDone.store (position, std::memory_order_relaxed);
        }

        // Closing the writer flushes it. Only after that is the file complete.
        wav.reset();

        if (result.cancelled)
        {
            writer.abandon();
            result.message = "Export cancelled.";
            return result;
        }

        if (writeFailed)
        {
            writer.abandon();
            result.message = "Export failed while writing audio. Check free space and permissions.";
            return result;
        }

        // Read the header back: a file that does not describe the render is not published.
        // Detected from the stream, because the temporary name has no audio extension.
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> check (
            formats.createReaderFor (std::make_unique<juce::FileInputStream> (writer.getTemporaryFile())));

        if (check == nullptr)
        {
            writer.abandon();
            result.message = "Export failed: the written file could not be read back.";
            return result;
        }

        if (check->lengthInSamples != position || check->numChannels != 2
            || juce::roundToInt (check->sampleRate) != juce::roundToInt (sampleRate))
        {
            const auto detail = "expected " + juce::String (position) + " frames, found "
                              + juce::String (check->lengthInSamples) + ".";
            check.reset();
            writer.abandon();
            result.message = "Export failed: the written file did not match the render (" + detail + ")";
            return result;
        }

        check.reset();

        if (writer.commit (-1, error) != AtomicFileWriter::Status::ok)
        {
            result.message = error;
            return result;
        }

        result.succeeded = true;
        result.lengthSeconds = static_cast<double> (position) / sampleRate;
        result.fileSizeBytes = destination.getSize();
        result.clippedSamples = clipped;
        result.peaks.reserve (peaks.size());

        for (const auto peak : peaks)
            result.peaks.push_back (juce::jmin (1.0f, peak));

        result.message = "Exported " + destination.getFileName() + " ("
                       + juce::String (result.lengthSeconds, 2) + " s, "
                       + juce::String (sampleRate / 1000.0, 1) + " kHz / " + juce::String (bitDepth)
                       + "-bit stereo WAV)";

        return result;
    }
}
