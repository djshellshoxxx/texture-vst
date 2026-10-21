#include "TestHelpers.h"

#include "../Source/IO/OfflineRenderTimeline.h"
#include "../Source/IO/OfflinePatchMath.h"
#include "../Source/IO/WavExporter.h"
#include "../Source/Utility/VersionedRandomStreams.h"

#include <cmath>

namespace texture::tests
{
    class OfflineRenderTimelineTests : public juce::UnitTest
    {
    public:
        OfflineRenderTimelineTests()
            : juce::UnitTest ("Offline render timeline", "texture") {}

        void runTest() override
        {
            using Timeline = OfflineRenderTimeline<4>;

            beginTest ("event boundaries split a normal render block exactly");
            const Timeline::Events events { 0, 100, 513, 900 };
            expectEquals (Timeline::nextBoundary (0, 512, events), static_cast<juce::int64> (100));
            expectEquals (Timeline::nextBoundary (100, 612, events), static_cast<juce::int64> (513));
            expectEquals (Timeline::nextBoundary (513, 1025, events), static_cast<juce::int64> (900));
            expectEquals (Timeline::nextBoundary (900, 1412, events), static_cast<juce::int64> (1412));

            beginTest ("events at the current position are applied before rendering");
            expect (Timeline::hasEventAt (0, events));
            expect (Timeline::hasEventAt (100, events));
            expect (! Timeline::hasEventAt (101, events));

            beginTest ("duplicate event positions do not create zero-length spans");
            const Timeline::Events duplicates { 0, 100, 100, 200 };
            expectEquals (Timeline::nextBoundary (100, 612, duplicates), static_cast<juce::int64> (200));

            beginTest ("events outside the current block do not shorten it");
            expectEquals (Timeline::nextBoundary (0, 64, events), static_cast<juce::int64> (64));


            beginTest ("real exporter writes a readable stereo WAV");

            TemporaryDirectory temp;
            const std::array<SampleBufferPtr, 2> samples {
                makeTestSample (48000.0, 0.5, 220.0, 2),
                makeTestSample (48000.0, 0.5, 330.0, 2)
            };

            WavExporter::RenderSettings settings;
            settings.morph = 0.5f;
            settings.attackMs = 5.0f;
            settings.decayMs = 20.0f;
            settings.sustain = 0.8f;
            settings.releaseMs = 30.0f;
            settings.layerBlend = 0.5f;
            settings.sampleStart = { 0.1f, 0.2f };
            settings.lfoRateHz = 0.5f;
            settings.lfoDepth = 0.2f;

            WavExporter::Options options;
            options.sampleRate = 48000.0;
            options.bitDepth = 24;
            options.holdSeconds = 0.1;
            options.tailSeconds = 0.1;
            options.gapSeconds = 0.05;

            const auto destination = temp.child ("render.wav");
            const auto result = WavExporter::render (samples, settings, options, destination);

            expect (result.succeeded, result.message);
            expect (destination.existsAsFile());
            expectGreaterThan (static_cast<int> (destination.getSize()), 44);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (destination));
            expect (reader != nullptr);

            if (reader != nullptr)
            {
                expectEquals (static_cast<int> (reader->numChannels), 2);
                expectWithinAbsoluteError (reader->sampleRate, 48000.0, 0.001);
                expect (reader->lengthInSamples > 0);
            }

            beginTest ("Repeat mode exports the captured seed deterministically");
            settings.randomMode = 1;
            settings.randomSeed = UINT64_C (0x123456789ABCDEF0);
            settings.shimmerAmount = 0.7f;
            auto repeatedA = temp.child ("repeat-a.wav");
            auto repeatedB = temp.child ("repeat-b.wav");
            const auto firstExport = WavExporter::render (samples, settings, options, repeatedA);
            const auto secondExport = WavExporter::render (samples, settings, options, repeatedB);
            expect (firstExport.succeeded && secondExport.succeeded);

            juce::AudioFormatManager repeatFormats;
            repeatFormats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> readerA (repeatFormats.createReaderFor (repeatedA));
            std::unique_ptr<juce::AudioFormatReader> readerB (repeatFormats.createReaderFor (repeatedB));
            if (readerA != nullptr && readerB != nullptr)
            {
                const auto compareSamples = static_cast<int> (juce::jmin (readerA->lengthInSamples,
                                                                          readerB->lengthInSamples));
                juce::AudioBuffer<float> audioA (2, compareSamples), audioB (2, compareSamples);
                readerA->read (&audioA, 0, compareSamples, 0, true, true);
                readerB->read (&audioB, 0, compareSamples, 0, true, true);
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < compareSamples; ++sample)
                        expectWithinAbsoluteError (audioA.getSample (channel, sample),
                                                   audioB.getSample (channel, sample), 1.0e-7f);
            }
            else
                expect (false, "Repeat exports should produce readable files");

            beginTest ("unsupported random algorithm versions are rejected cleanly");
            settings.randomAlgorithmVersion = 99;
            const auto unsupportedFile = temp.child ("unsupported-random-version.wav");
            const auto unsupportedResult = WavExporter::render (samples, settings, options, unsupportedFile);
            expect (! unsupportedResult.succeeded);
            expect (! unsupportedFile.existsAsFile());
            settings.randomAlgorithmVersion = VersionedRandomStreams::algorithmVersion;

            beginTest ("changing the seed changes the deterministic render");
            settings.randomSeed += 1;
            const auto differentSeedFile = temp.child ("repeat-different-seed.wav");
            const auto differentSeedExport = WavExporter::render (samples, settings, options, differentSeedFile);
            expect (differentSeedExport.succeeded);
            std::unique_ptr<juce::AudioFormatReader> readerC (repeatFormats.createReaderFor (differentSeedFile));
            if (readerA != nullptr && readerC != nullptr)
            {
                const auto compareSamples = static_cast<int> (juce::jmin (readerA->lengthInSamples,
                                                                          readerC->lengthInSamples));
                juce::AudioBuffer<float> audioA (2, compareSamples), audioC (2, compareSamples);
                readerA->read (&audioA, 0, compareSamples, 0, true, true);
                readerC->read (&audioC, 0, compareSamples, 0, true, true);
                double errorEnergy = 0.0;
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < compareSamples; ++sample)
                    {
                        const auto difference = static_cast<double> (audioA.getSample (channel, sample)
                                                                     - audioC.getSample (channel, sample));
                        errorEnergy += difference * difference;
                    }
                expectGreaterThan (errorEnergy, 1.0e-5);
            }
            else
                expect (false, "Different-seed render should produce a readable file");

            beginTest ("dual-layer export uses the same normalized blend law as live playback");
            const auto dry = OfflinePatchMath::layerGains (0.0f);
            expectWithinAbsoluteError (dry[0], 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (dry[1], 0.0f, 1.0e-6f);

            const auto full = OfflinePatchMath::layerGains (1.0f);
            const auto expected = static_cast<float> (1.0 / std::sqrt (2.0));
            expectWithinAbsoluteError (full[0], expected, 1.0e-6f);
            expectWithinAbsoluteError (full[1], expected, 1.0e-6f);

            const auto clamped = OfflinePatchMath::layerGains (2.0f);
            expectWithinAbsoluteError (clamped[0], expected, 1.0e-6f);
            expectWithinAbsoluteError (clamped[1], expected, 1.0e-6f);
        }
    };

    static OfflineRenderTimelineTests offlineRenderTimelineTests;
}
