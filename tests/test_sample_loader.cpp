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
            testRejectsTruncatedAudioData();
            testRejectsFolder();
            testBoundsConversionMemory();
            testTruncatesLongFiles();
            testDownmixesMultiChannel();
            testResampling();
            testConcurrentLoads();
            testIndependentRequestLanes();
            testLaneCancellation();
            testWaveformCache();
            testWaveformBoundaries();
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

        void testRejectsTruncatedAudioData()
        {
            beginTest ("rejects a WAV whose declared audio data is missing");

            TemporaryDirectory temp;
            SampleLoader loader;
            loader.prepare (48000.0);

            const auto file = temp.child ("truncated-data.wav");
            expect (writeTestAudioFile (file, 44100.0, 0.25, 2));

            juce::MemoryBlock bytes;
            expect (file.loadFileAsData (bytes));

            auto* data = static_cast<juce::uint8*> (bytes.getData());
            const auto readLittleEndian = [data] (int offset)
            {
                return static_cast<juce::uint32> (data[offset])
                     | (static_cast<juce::uint32> (data[offset + 1]) << 8)
                     | (static_cast<juce::uint32> (data[offset + 2]) << 16)
                     | (static_cast<juce::uint32> (data[offset + 3]) << 24);
            };
            int dataChunk = -1;

            for (size_t offset = 12; offset + 8 <= bytes.getSize();)
            {
                const auto chunkSize = readLittleEndian (static_cast<int> (offset + 4));

                if (data[offset] == 'd' && data[offset + 1] == 'a'
                    && data[offset + 2] == 't' && data[offset + 3] == 'a')
                {
                    dataChunk = static_cast<int> (offset);
                    break;
                }

                const auto next = offset + 8u + static_cast<size_t> (chunkSize) + (chunkSize & 1u);

                if (next <= offset || next > bytes.getSize())
                    break;

                offset = next;
            }

            expect (dataChunk >= 0, "expected a standard WAV data chunk");

            if (dataChunk < 0)
                return;

            const auto writeLittleEndian = [data] (int offset, juce::uint32 value)
            {
                data[offset]     = static_cast<juce::uint8> (value & 0xffu);
                data[offset + 1] = static_cast<juce::uint8> ((value >> 8) & 0xffu);
                data[offset + 2] = static_cast<juce::uint8> ((value >> 16) & 0xffu);
                data[offset + 3] = static_cast<juce::uint8> ((value >> 24) & 0xffu);
            };

            constexpr juce::uint32 missingBytes = 4096;
            writeLittleEndian (dataChunk + 4, readLittleEndian (dataChunk + 4) + missingBytes);
            writeLittleEndian (4, readLittleEndian (4) + missingBytes);

            expect (file.deleteFile(), "remove the original WAV before writing the damaged header");
            auto output = file.createOutputStream();
            expect (output != nullptr);

            if (output == nullptr)
                return;

            expect (output->write (bytes.getData(), bytes.getSize()));
            output.reset();
            expect (static_cast<juce::int64> (readLittleEndian (4)) + 8 > file.getSize(),
                    "fixture RIFF size must extend past the physical file");

            const auto result = loader.loadSynchronously (file);
            expect (! result.succeeded, "a short read must not be accepted as a valid sample");
            expect (result.buffer == nullptr);
            expect (result.message.containsIgnoreCase ("truncated")
                    || result.message.containsIgnoreCase ("corrupt"),
                    "expected a decode error, got: " + result.message);
        }

        void testBoundsConversionMemory()
        {
            beginTest ("rejects conversion that exceeds the temporary memory budget");

            TemporaryDirectory temp;
            SampleLoader loader;
            loader.prepare (768000.0);

            // The input file is small, but the 30 second stereo output at this host
            // rate would exceed the explicit temporary-buffer ceiling.
            const auto file = temp.child ("high-rate-conversion.wav");
            expect (writeTestAudioFile (file, 48000.0, SampleBuffer::maxLengthSeconds, 2));
            expect (file.getSize() < SampleLoader::maxFileSizeBytes);

            const auto result = loader.loadSynchronously (file);
            expect (! result.succeeded);
            expect (result.buffer == nullptr);
            expect (result.message.containsIgnoreCase ("128 MB"),
                    "expected the conversion-memory limit message, got: " + result.message);
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


        void testIndependentRequestLanes()
        {
            beginTest ("independent request lanes both deliver their newest result");

            TemporaryDirectory temp;
            const auto layerAOld = temp.child ("layer_a_old.wav");
            const auto layerANew = temp.child ("layer_a_new.wav");
            const auto layerB = temp.child ("layer_b.wav");

            expect (writeTestAudioFile (layerAOld, 44100.0, 0.2, 2));
            expect (writeTestAudioFile (layerANew, 44100.0, 0.2, 2));
            expect (writeTestAudioFile (layerB, 44100.0, 0.2, 2));

            SampleLoader loader;
            loader.prepare (48000.0);

            juce::StringArray delivered;
            juce::Array<int> tags;

            loader.setCallback ([&] (const SampleLoader::LoadResult& result)
            {
                if (result.succeeded)
                {
                    delivered.add (result.file.getFileName());
                    tags.add (result.requestTag);
                }
            });

            loader.loadAsync (layerAOld, 0);
            loader.loadAsync (layerB, 1);
            loader.loadAsync (layerANew, 0);

            const auto finished = waitForCondition ([&]
            {
                return ! loader.isBusy() && delivered.size() >= 2;
            });

            expect (finished);
            expectEquals (delivered.size(), 2,
                          "one newest result per independent lane must be delivered");
            expect (delivered.contains (layerANew.getFileName()));
            expect (delivered.contains (layerB.getFileName()));
            expect (! delivered.contains (layerAOld.getFileName()));
            expect (tags.contains (0));
            expect (tags.contains (1));

            loader.setCallback (nullptr);
            loader.waitForPendingLoads();
        }


        void testLaneCancellation()
        {
            beginTest ("cancelling one request lane does not cancel another");

            TemporaryDirectory temp;
            const auto layerA = temp.child ("cancel_a.wav");
            const auto layerB = temp.child ("keep_b.wav");
            expect (writeTestAudioFile (layerA, 44100.0, 0.4, 2));
            expect (writeTestAudioFile (layerB, 44100.0, 0.4, 2));

            SampleLoader loader;
            loader.prepare (48000.0);

            juce::Array<int> deliveredTags;
            loader.setCallback ([&] (const SampleLoader::LoadResult& result)
            {
                if (result.succeeded)
                    deliveredTags.add (result.requestTag);
            });

            loader.loadAsync (layerA, 0);
            loader.loadAsync (layerB, 1);
            loader.cancelPendingLoad (0);

            const auto finished = waitForCondition ([&]
            {
                return ! loader.isBusy() && deliveredTags.size() >= 1;
            });

            expect (finished);
            expect (! deliveredTags.contains (0));
            expect (deliveredTags.contains (1));

            loader.setCallback (nullptr);
            loader.waitForPendingLoads();
        }

        void testWaveformBoundaries()
        {
            beginTest ("waveform includes the final frame of a non-divisible clip");
            juce::AudioBuffer<float> audio (2, 513);
            audio.clear();
            audio.setSample (1, 512, 0.75f);
            SampleBuffer sample (std::move (audio), 48000.0, juce::File(), 2, 48000.0, false);
            expectWithinAbsoluteError (sample.getGlobalPeak(), 0.75f, 1.0e-6f);
            expectWithinAbsoluteError (sample.getWaveformPeaks().back(), 1.0f, 1.0e-6f);

            beginTest ("two-frame waveform spans the display proportionally");
            juce::AudioBuffer<float> shortAudio (1, 2);
            shortAudio.setSample (0, 0, 0.25f);
            shortAudio.setSample (0, 1, 0.5f);
            SampleBuffer shortSample (std::move (shortAudio), 48000.0, juce::File(), 1, 48000.0, false);
            for (int bin = 0; bin < SampleBuffer::waveformResolution; ++bin)
                expectWithinAbsoluteError (shortSample.getWaveformPeaks()[static_cast<size_t> (bin)],
                                           bin < 256 ? 0.5f : 1.0f, 1.0e-6f);
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

            auto crossings = [] (const juce::AudioBuffer<float>& pad)
            {
                int count = 0;
                const auto* data = pad.getReadPointer (0);

                for (int i = 1; i < pad.getNumSamples(); ++i)
                    if ((data[i - 1] < 0.0f) != (data[i] < 0.0f))
                        ++count;

                return count;
            };

            expectGreaterThan (crossings (high), crossings (low),
                               "the last pad should be higher than the first");

            beginTest ("factory validation rejects non-WAV stand-ins");

            TemporaryDirectory temp;
            const auto directory = temp.child ("factory");
            directory.createDirectory();

            const auto fake = directory.getChildFile (FactoryContent::getFileName (0));
            fake.replaceWithText ("placeholder");

            expect (! FactoryContent::isValidFactoryFile (fake));
            expect (! FactoryContent::isInstalledIn (directory));

            beginTest ("factory validation accepts the documented WAV shape");

            const auto valid = directory.getChildFile ("valid.wav");
            expect (writeTestAudioFile (valid,
                                        FactoryContent::sampleRate,
                                        FactoryContent::lengthSeconds,
                                        FactoryContent::numChannels,
                                        FactoryContent::bitDepth));
            expect (FactoryContent::isValidFactoryFile (valid));
        }
    };

    static FactoryContentTests factoryContentTests;
}
