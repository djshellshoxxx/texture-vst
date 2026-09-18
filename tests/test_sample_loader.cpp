#include "TestHelpers.h"

#include "../Source/IO/FactoryContent.h"
#include "../Source/IO/SampleLoader.h"

#include <atomic>

namespace texture::tests
{
    class SampleLoaderTests : public juce::UnitTest
    {
    public:
        SampleLoaderTests()
            : juce::UnitTest ("Sample loader", "texture") {}

        void runTest() override
        {
            testFormatCoverage();
            testRejectsNonAudioFile();
            testRejectsFolder();
            testTruncatesLongFiles();
            testDownmixesMultiChannel();
            testResampling();
            testConcurrentLoads();
            testWaveformCache();
            testPresetRouting();
        }

    private:
        void testFormatCoverage()
        {
            beginTest ("loads every supported format");

            TemporaryDirectory temp;
            SampleLoader loader;
            loader.prepare (48000.0);

            // JUCE ships encoders for these three; MP3 is decode-only.
            for (const auto* extension : { ".wav", ".aiff", ".flac", ".ogg" })
            {
                const auto file = temp.child (juce::String ("tone") + extension);

                if (! writeTestAudioFile (file, 44100.0, 0.5, 2))
                {
                    logMessage (juce::String ("skipping ") + extension + ": no encoder in this build");
                    continue;
                }

                const auto result = loader.loadSynchronously (file);

                expect (result.succeeded, juce::String ("failed to load ") + extension
                                              + ": " + result.message);
                expect (result.buffer != nullptr && result.buffer->isValid());

                if (result.buffer != nullptr)
                    expectEquals (result.buffer->getNumChannels(), 2);
            }

            // MP3 has no encoder, so assert the decoder is at least registered.
            expect (loader.getSupportedWildcard().containsIgnoreCase ("mp3"),
                    "MP3 must be offered in the file chooser");
            expect (loader.isSupportedAudioFile (temp.child ("x.mp3")));
        }

        void testRejectsNonAudioFile()
        {
            beginTest ("rejects a non-audio file cleanly");

            TemporaryDirectory temp;
            SampleLoader loader;
            loader.prepare (48000.0);

            const auto textFile = temp.child ("notes.txt");
            textFile.replaceWithText ("this is definitely not audio");

            const auto result = loader.loadSynchronously (textFile);

            expect (! result.succeeded);
            expect (result.buffer == nullptr);
            expect (result.message.containsIgnoreCase ("unsupported"),
                    "expected the unsupported-format message, got: " + result.message);

            // A file with an audio extension but garbage inside is a decode failure.
            const auto fakeWav = temp.child ("broken.wav");
            fakeWav.replaceWithText ("RIFFnope");

            const auto broken = loader.loadSynchronously (fakeWav);
            expect (! broken.succeeded);
            expect (broken.message.isNotEmpty());

            // A missing file must not crash either.
            const auto missing = loader.loadSynchronously (temp.child ("absent.wav"));
            expect (! missing.succeeded);
            expect (missing.message.containsIgnoreCase ("not found"));
        }

        void testRejectsFolder()
        {
            beginTest ("rejects a folder with the documented message");

            TemporaryDirectory temp;
            SampleLoader loader;
            loader.prepare (48000.0);

            const auto folder = temp.child ("a_folder");
            folder.createDirectory();

            const auto result = loader.loadSynchronously (folder);

            expect (! result.succeeded);
            expect (result.message.containsIgnoreCase ("not a folder"));
        }

        void testTruncatesLongFiles()
        {
            beginTest ("truncates a 60 second file to 30 seconds");

            TemporaryDirectory temp;
            SampleLoader loader;
            loader.prepare (48000.0);

            // A low rate keeps the test file small while still being 60 s long.
            const auto file = temp.child ("long.wav");
            expect (writeTestAudioFile (file, 8000.0, 60.0, 1));

            const auto result = loader.loadSynchronously (file);

            expect (result.succeeded);
            expect (result.buffer != nullptr);

            if (result.buffer == nullptr)
                return;

            expect (result.buffer->wasTruncatedOnLoad());
            expect (result.message.containsIgnoreCase ("truncated"));

            // 30 s at the target rate, within one resampling frame.
            expectWithinAbsoluteError (result.buffer->getLengthSeconds(),
                                       SampleBuffer::maxLengthSeconds, 0.01);

            // A short file must not claim to have been truncated.
            const auto shortFile = temp.child ("short.wav");
            expect (writeTestAudioFile (shortFile, 8000.0, 1.0, 1));

            const auto shortResult = loader.loadSynchronously (shortFile);
            expect (shortResult.succeeded);
            expect (! shortResult.buffer->wasTruncatedOnLoad());
        }

        void testDownmixesMultiChannel()
        {
            beginTest ("folds a 4-channel file down to stereo");

            TemporaryDirectory temp;
            SampleLoader loader;
            loader.prepare (48000.0);

            const auto file = temp.child ("quad.wav");
            expect (writeTestAudioFile (file, 44100.0, 0.5, 4));

            const auto result = loader.loadSynchronously (file);

            expect (result.succeeded);
            expect (result.buffer != nullptr);

            if (result.buffer == nullptr)
                return;

            expectEquals (result.buffer->getNumChannels(), 2, "must be folded to stereo");
            expectEquals (result.buffer->getOriginalChannelCount(), 4, "metadata must record the original");
            expect (result.message.containsIgnoreCase ("channels"));

            // A mono file must be duplicated up to stereo.
            const auto monoFile = temp.child ("mono.wav");
            expect (writeTestAudioFile (monoFile, 44100.0, 0.5, 1));

            const auto mono = loader.loadSynchronously (monoFile);
            expect (mono.succeeded);
            expectEquals (mono.buffer->getNumChannels(), 2);

            // Both channels must carry the same signal.
            const auto* left = mono.buffer->getChannel (0);
            const auto* right = mono.buffer->getChannel (1);
            expect (left != nullptr && right != nullptr);

            for (int i = 0; i < juce::jmin (1000, mono.buffer->getNumSamples()); ++i)
                expectWithinAbsoluteError (left[i], right[i], 1.0e-6f);
        }

        void testResampling()
        {
            beginTest ("resamples 44.1 kHz to 48 kHz with the right length and content");

            TemporaryDirectory temp;
            SampleLoader loader;
            loader.prepare (48000.0);

            const auto file = temp.child ("rate.wav");
            constexpr double sourceSeconds = 2.0;
            expect (writeTestAudioFile (file, 44100.0, sourceSeconds, 2));

            const auto result = loader.loadSynchronously (file);

            expect (result.succeeded);
            expect (result.buffer != nullptr);

            if (result.buffer == nullptr)
                return;

            expectWithinAbsoluteError (result.buffer->getSampleRate(), 48000.0, 0.001);
            expectWithinAbsoluteError (result.buffer->getOriginalSampleRate(), 44100.0, 0.001);

            // Duration must survive the rate change; length in frames must not.
            expectWithinAbsoluteError (result.buffer->getLengthSeconds(), sourceSeconds, 0.02);
            expectGreaterThan (result.buffer->getNumSamples(),
                               static_cast<int> (sourceSeconds * 44100.0),
                               "upsampling must produce more frames");

            // Content must survive: a 110 Hz tone should still be loud and finite.
            expectGreaterThan (result.buffer->getGlobalPeak(), 0.1f);
            expect (isFinite (result.buffer->getAudio()));

            // The same file at the source rate must not be resampled at all.
            loader.prepare (44100.0);
            const auto identity = loader.loadSynchronously (file);
            expect (identity.succeeded);
            expectWithinAbsoluteError (identity.buffer->getSampleRate(), 44100.0, 0.001);
        }

        void testConcurrentLoads()
        {
            beginTest ("only the most recent of several queued loads becomes active");

            TemporaryDirectory temp;

            juce::Array<juce::File> files;

            for (int i = 0; i < 6; ++i)
            {
                const auto file = temp.child ("queued_" + juce::String (i) + ".wav");
                expect (writeTestAudioFile (file, 44100.0, 0.3, 2));
                files.add (file);
            }

            SampleLoader loader;
            loader.prepare (48000.0);

            std::atomic<int> callbackCount { 0 };
            juce::String lastDelivered;

            loader.setCallback ([&] (const SampleLoader::LoadResult& result)
            {
                ++callbackCount;

                if (result.succeeded)
                    lastDelivered = result.file.getFileName();
            });

            for (const auto& file : files)
                loader.loadAsync (file);

            const auto finished = waitForCondition ([&] { return ! loader.isBusy() && callbackCount > 0; });

            expect (finished, "the loader never finished");

            // Earlier requests are decoded but discarded, so exactly one lands.
            expectEquals (callbackCount.load(), 1, "stale results must be dropped");
            expectEquals (lastDelivered, files.getLast().getFileName(),
                          "the newest request must win");

            loader.setCallback (nullptr);
            loader.waitForPendingLoads();
        }

        void testWaveformCache()
        {
            beginTest ("the waveform envelope is computed and normalised on load");

            TemporaryDirectory temp;
            SampleLoader loader;
            loader.prepare (48000.0);

            const auto file = temp.child ("envelope.wav");
            expect (writeTestAudioFile (file, 44100.0, 1.0, 2));

            const auto result = loader.loadSynchronously (file);
            expect (result.succeeded);

            if (result.buffer == nullptr)
                return;

            const auto& peaks = result.buffer->getWaveformPeaks();
            const auto& rms = result.buffer->getWaveformRms();

            expectEquals (static_cast<int> (peaks.size()), SampleBuffer::waveformResolution);
            expectEquals (static_cast<int> (rms.size()), SampleBuffer::waveformResolution);

            float highest = 0.0f;

            for (size_t i = 0; i < peaks.size(); ++i)
            {
                expect (peaks[i] >= 0.0f && peaks[i] <= 1.0001f, "peaks must be normalised");
                expect (rms[i] <= peaks[i] + 1.0e-4f, "RMS can never exceed the peak");
                highest = juce::jmax (highest, peaks[i]);
            }

            expectWithinAbsoluteError (highest, 1.0f, 0.01f, "the loudest bin should reach full scale");
        }

        void testPresetRouting()
        {
            beginTest ("preset files are recognised and routed away from the audio loader");

            TemporaryDirectory temp;

            expect (SampleLoader::isPresetFile (temp.child ("patch.texture")));
            expect (SampleLoader::isPresetFile (temp.child ("PATCH.TEXTURE")));
            expect (! SampleLoader::isPresetFile (temp.child ("patch.wav")));

            SampleLoader loader;
            expect (! loader.isSupportedAudioFile (temp.child ("patch.texture")));
        }
    };

    static SampleLoaderTests sampleLoaderTests;

    // -------------------------------------------------------------------------

    class FactoryContentTests : public juce::UnitTest
    {
    public:
        FactoryContentTests()
            : juce::UnitTest ("Factory content", "texture") {}

        void runTest() override
        {
            beginTest ("renders a musically sensible pad");

            juce::AudioBuffer<float> buffer;
            FactoryContent::renderPad (buffer, 0, 48000.0, 48000);

            expectEquals (buffer.getNumChannels(), 2);
            expectEquals (buffer.getNumSamples(), 48000);
            expect (isFinite (buffer));
            expectGreaterThan (peakOf (buffer), 0.05f, "the pad should be audible");
            expect (peakOf (buffer) <= 1.0f, "the pad must not clip");

            beginTest ("names files Pad_01 .. Pad_20");

            expectEquals (FactoryContent::getFileName (0), juce::String ("Pad_01.wav"));
            expectEquals (FactoryContent::getFileName (19), juce::String ("Pad_20.wav"));

            beginTest ("different indices give different pitches");

            juce::AudioBuffer<float> low;
            juce::AudioBuffer<float> high;
            FactoryContent::renderPad (low, 0, 48000.0, 24000);
            FactoryContent::renderPad (high, FactoryContent::numSamples - 1, 48000.0, 24000);

            auto crossings = [] (const juce::AudioBuffer<float>& buffer)
            {
                int count = 0;
                const auto* data = buffer.getReadPointer (0);

                for (int i = 1; i < buffer.getNumSamples(); ++i)
                    if ((data[i - 1] < 0.0f) != (data[i] < 0.0f))
                        ++count;

                return count;
            };

            expectGreaterThan (crossings (high), crossings (low),
                               "the last pad should be higher than the first");

            beginTest ("installation is idempotent");

            TemporaryDirectory temp;
            const auto directory = temp.child ("factory");

            expect (! FactoryContent::isInstalledIn (directory));

            // Generating all twenty 30-second files is slow, so verify the guard
            // logic against pre-placed stand-ins instead.
            directory.createDirectory();

            for (int i = 0; i < FactoryContent::numSamples; ++i)
                directory.getChildFile (FactoryContent::getFileName (i)).replaceWithText ("placeholder");

            expect (FactoryContent::isInstalledIn (directory));
            expectEquals (FactoryContent::generateInto (directory), 0,
                          "existing files must never be overwritten");
        }
    };

    static FactoryContentTests factoryContentTests;
}
