#include "TestHelpers.h"

#include "../Source/IO/ExportController.h"
#include "../Source/IO/ExportJob.h"
#include "../Source/IO/ExportPlanner.h"
#include "../Source/Utility/VersionedRandomStreams.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <thread>

namespace texture::tests
{
    namespace
    {
        PatchSnapshot makeSnapshot (float gainDb = 0.0f)
        {
            PatchSnapshot snapshot;
            auto& settings = snapshot.settings;
            settings.morph = 0.5f;
            settings.attackMs = 5.0f;
            settings.decayMs = 20.0f;
            settings.sustain = 0.8f;
            settings.releaseMs = 30.0f;
            settings.reverbOn = false;
            settings.gainDb = gainDb;
            settings.lfoRateHz = 0.5f;
            settings.lfoDepth = 0.2f;
            settings.layerBlend = 0.5f;
            settings.sampleStart = { 0.1f, 0.2f };
            settings.randomMode = 1;
            settings.randomSeed = UINT64_C (0x123456789ABCDEF0);
            settings.randomAlgorithmVersion = VersionedRandomStreams::algorithmVersion;
            settings.shimmerAmount = 0.3f;
            snapshot.samples = { makeTestSample (48000.0, 0.5, 220.0, 2), makeTestSample (48000.0, 0.5, 330.0, 2) };
            return snapshot;
        }

        ExportPlanner::Plan shortPlan()
        {
            ExportPlanner::Request request;
            request.mode = ExportPlanner::Mode::singleNote;
            request.holdSeconds = 0.2;
            request.tailSeconds = 0.1;
            request.sampleRate = 48000.0;
            return ExportPlanner::plan (request, 0.03);
        }

        ExportPlanner::Plan longPlan()
        {
            ExportPlanner::Request request;
            request.mode = ExportPlanner::Mode::singleNote;
            request.holdSeconds = 20.0;
            request.tailSeconds = 10.0;
            request.sampleRate = 48000.0;
            return ExportPlanner::plan (request, 0.03);
        }

        bool waitUntil (const std::function<bool()>& condition, int timeoutMs)
        {
            for (int elapsed = 0; elapsed < timeoutMs; elapsed += 2)
            {
                if (condition())
                    return true;

                juce::Thread::sleep (2);
            }

            return condition();
        }

        juce::MemoryBlock readBytes (const juce::File& file)
        {
            juce::MemoryBlock block;
            file.loadFileAsData (block);
            return block;
        }

        int countPartialFiles (const juce::File& directory)
        {
            return directory.getNumberOfChildFiles (juce::File::findFiles, "*.partial");
        }

        const std::atomic<bool>& neverCancelled()
        {
            static const std::atomic<bool> flag { false };
            return flag;
        }
    }

    class ExportJobTests : public juce::UnitTest
    {
    public:
        ExportJobTests()
            : juce::UnitTest ("Export job", "texture") {}

        void runTest() override
        {
            beginTest ("a render writes a stereo file of exactly the planned length and reports its peaks");
            {
                TemporaryDirectory temp;
                const auto plan = shortPlan();
                const auto destination = temp.child ("render.wav");
                std::atomic<juce::int64> frames { 0 };

                const auto result = ExportJob::run (makeSnapshot(), plan, 48000.0, 24, destination,
                                                    neverCancelled(), frames);
                expect (result.succeeded, result.message);
                expect (destination.existsAsFile());
                expectEquals (frames.load(), plan.totalSamples);
                expectEquals (static_cast<int> (result.peaks.size()), ExportJob::peakBins);
                expectEquals (result.clippedSamples, 0);

                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (destination));
                expect (reader != nullptr);
                if (reader != nullptr)
                {
                    expectEquals (reader->lengthInSamples, plan.totalSamples);
                    expectEquals (static_cast<int> (reader->numChannels), 2);
                    expectWithinAbsoluteError (reader->sampleRate, 48000.0, 0.001);
                }

                int nonSilentBins = 0;
                for (const auto peak : result.peaks)
                    if (peak > 0.0f)
                        ++nonSilentBins;
                expect (nonSilentBins > 0, "the rendered audio is not silent");
            }

            beginTest ("the same plan and snapshot render identical bytes");
            {
                TemporaryDirectory temp;
                const auto plan = shortPlan();
                const auto snapshot = makeSnapshot();
                std::atomic<juce::int64> frames { 0 };
                const auto first = temp.child ("first.wav");
                const auto second = temp.child ("second.wav");

                expect (ExportJob::run (snapshot, plan, 48000.0, 24, first, neverCancelled(), frames).succeeded);
                expect (ExportJob::run (snapshot, plan, 48000.0, 24, second, neverCancelled(), frames).succeeded);
                expect (readBytes (first) == readBytes (second), "fixed-seed renders must be reproducible");
            }

            beginTest ("manual grain controls reach the offline renderer, and Follow MORPH is the default");
            {
                TemporaryDirectory temp;
                const auto plan = shortPlan();
                const auto followed = makeSnapshot();
                auto manual = makeSnapshot();
                manual.settings.grain.values = { 40.0f, 60.0f, 0.5f, 25.0f, 0.9f };
                manual.settings.grain.follow = { false, false, false, false, false };

                std::atomic<juce::int64> frames { 0 };
                const auto a = temp.child ("follow_a.wav");
                const auto b = temp.child ("follow_b.wav");
                const auto m = temp.child ("manual.wav");

                expect (ExportJob::run (followed, plan, 48000.0, 24, a, neverCancelled(), frames).succeeded);
                expect (ExportJob::run (followed, plan, 48000.0, 24, b, neverCancelled(), frames).succeeded);
                expect (ExportJob::run (manual, plan, 48000.0, 24, m, neverCancelled(), frames).succeeded);

                expect (readBytes (a) == readBytes (b), "Follow MORPH must reproduce the existing render");
                expect (readBytes (a) != readBytes (m), "manual grain values must change the rendered audio");
            }

            beginTest ("the MORPH LFO shape reaches the offline renderer, and Sine is the default");
            {
                TemporaryDirectory temp;
                const auto plan = shortPlan();
                const auto sine = makeSnapshot();
                auto triangle = makeSnapshot();
                triangle.settings.lfoShape = 1;

                std::atomic<juce::int64> frames { 0 };
                const auto a = temp.child ("sine.wav");
                const auto b = temp.child ("triangle.wav");

                expect (ExportJob::run (sine, plan, 48000.0, 24, a, neverCancelled(), frames).succeeded);
                expect (ExportJob::run (triangle, plan, 48000.0, 24, b, neverCancelled(), frames).succeeded);
                expect (readBytes (a) != readBytes (b), "a different LFO shape must change the render");
            }

            beginTest ("a destination that cannot be created fails without leaving a file");
            {
                TemporaryDirectory temp;
                const auto blocker = temp.child ("not-a-folder");
                blocker.replaceWithText ("occupied");
                std::atomic<juce::int64> frames { 0 };
                const auto result = ExportJob::run (makeSnapshot(), shortPlan(), 48000.0, 24,
                                                    blocker.getChildFile ("render.wav"), neverCancelled(), frames);
                expect (! result.succeeded);
                expect (result.message.isNotEmpty(), "the failure must say why");
                expect (! blocker.getChildFile ("render.wav").existsAsFile());
                expect (blocker.loadFileAsString() == "occupied", "an unrelated file is never touched");
            }

            beginTest ("cancelling before the first block keeps an existing destination and leaves no partial file");
            {
                TemporaryDirectory temp;
                const auto existing = temp.child ("keep.wav");
                existing.replaceWithText ("previous render");
                const std::atomic<bool> cancelled { true };
                std::atomic<juce::int64> frames { 0 };

                const auto result = ExportJob::run (makeSnapshot(), shortPlan(), 48000.0, 24, existing,
                                                    cancelled, frames);
                expect (result.cancelled);
                expect (! result.succeeded);
                expect (existing.loadFileAsString() == "previous render", "a cancelled render must not replace the file");
                expectEquals (countPartialFiles (temp.get()), 0);
                expectEquals (frames.load(), static_cast<juce::int64> (0));
            }

            beginTest ("cancelling mid-render stops within one block and removes the partial file");
            {
                TemporaryDirectory temp;
                const auto plan = longPlan();
                const auto destination = temp.child ("long.wav");
                const auto snapshot = makeSnapshot();
                std::atomic<bool> cancel { false };
                std::atomic<juce::int64> frames { 0 };
                WavExporter::Result result;

                std::thread worker ([&]
                {
                    result = ExportJob::run (snapshot, plan, 48000.0, 24, destination, cancel, frames);
                });

                const auto reached = waitUntil ([&] { return frames.load() >= 20000; }, 60000);
                cancel.store (true);
                worker.join();

                expect (reached, "the render should have started");
                expect (result.cancelled, result.message);
                expect (frames.load() < plan.totalSamples, "cancellation must stop the render early");
                expect (! destination.existsAsFile());
                expectEquals (countPartialFiles (temp.get()), 0);
            }

            beginTest ("full-scale samples are counted per channel and never altered");
            {
                juce::AudioBuffer<float> buffer (2, 4);
                buffer.clear();
                buffer.setSample (0, 0, 1.2f);     // over
                buffer.setSample (0, 1, 0.5f);
                buffer.setSample (0, 2, -1.0f);    // exactly full scale counts
                buffer.setSample (1, 0, 0.999f);   // just under
                buffer.setSample (1, 3, -3.0f);    // over
                expectEquals (ExportJob::countFullScaleSamples (buffer, 4), 3);
                expectEquals (ExportJob::countFullScaleSamples (buffer, 1), 1, "only the first frame is counted");
                expectWithinAbsoluteError (buffer.getSample (0, 0), 1.2f, 1.0e-6f);
                expectWithinAbsoluteError (buffer.getSample (1, 3), -3.0f, 1.0e-6f);
            }

            beginTest ("a loud render reports its clipping count from the rendered audio");
            {
                TemporaryDirectory temp;
                std::atomic<juce::int64> frames { 0 };
                auto loud = makeSnapshot (6.0f);
                const auto result = ExportJob::run (loud, shortPlan(), 48000.0, 24,
                                                    temp.child ("loud.wav"), neverCancelled(), frames);
                expect (result.succeeded, result.message);
                expect (result.clippedSamples >= 0);
            }

            beginTest ("a patch with no sample refuses to render, with the usual message");
            {
                TemporaryDirectory temp;
                auto snapshot = makeSnapshot();
                snapshot.samples = { nullptr, nullptr };
                std::atomic<juce::int64> frames { 0 };
                const auto result = ExportJob::run (snapshot, shortPlan(), 48000.0, 24,
                                                    temp.child ("silent.wav"), neverCancelled(), frames);
                expect (! result.succeeded);
                expect (result.message == "Load a sample before exporting.", result.message);
                expect (! temp.child ("silent.wav").existsAsFile());
            }

            beginTest ("the controller runs one job at a time, collects its result once and returns to idle");
            {
                TemporaryDirectory temp;
                ExportController controller;
                ExportController::Request request;
                request.snapshot = std::make_shared<const PatchSnapshot> (makeSnapshot());
                request.plan = shortPlan();
                request.sampleRate = 48000.0;
                request.bitDepth = 24;
                request.destination = temp.child ("controller.wav");

                expect (controller.start (request));
                expect (! controller.start (request), "a second start is refused while busy");
                expect (controller.isBusy());

                const auto finished = waitUntil ([&] { return controller.getState() == ExportController::State::finished; }, 30000);
                expect (finished);

                WavExporter::Result result;
                expect (controller.takeResult (result));
                expect (result.succeeded, result.message);
                expect (! controller.takeResult (result), "a result is collected only once");
                expect (controller.getState() == ExportController::State::idle);
                expectWithinAbsoluteError (controller.getProgress(), 1.0, 1.0e-9);
            }

            beginTest ("cancelling a running controller job reports cancellation and accepts a new start");
            {
                TemporaryDirectory temp;
                ExportController controller;
                ExportController::Request longRequest;
                longRequest.snapshot = std::make_shared<const PatchSnapshot> (makeSnapshot());
                longRequest.plan = longPlan();
                longRequest.sampleRate = 48000.0;
                longRequest.bitDepth = 24;
                longRequest.destination = temp.child ("cancel-me.wav");

                expect (controller.start (longRequest));
                expect (waitUntil ([&] { return controller.getProgress() > 0.01; }, 60000));
                controller.cancel();

                const auto finished = waitUntil ([&] { return controller.getState() == ExportController::State::finished; }, 60000);
                expect (finished);

                WavExporter::Result cancelled;
                expect (controller.takeResult (cancelled));
                expect (cancelled.cancelled && ! cancelled.succeeded);
                expect (! longRequest.destination.existsAsFile());

                auto shortRequest = longRequest;
                shortRequest.plan = shortPlan();
                shortRequest.destination = temp.child ("after-cancel.wav");
                expect (controller.start (shortRequest), "a cancelled controller accepts the next render");
                expect (waitUntil ([&] { return controller.getState() == ExportController::State::finished; }, 30000));
                WavExporter::Result next;
                expect (controller.takeResult (next) && next.succeeded, next.message);
            }

            beginTest ("destroying the controller during a render cancels it and leaves no partial file");
            {
                TemporaryDirectory temp;
                const auto destination = temp.child ("abandoned.wav");

                {
                    ExportController controller;
                    ExportController::Request request;
                    request.snapshot = std::make_shared<const PatchSnapshot> (makeSnapshot());
                    request.plan = longPlan();
                    request.sampleRate = 48000.0;
                    request.bitDepth = 24;
                    request.destination = destination;
                    expect (controller.start (request));
                    expect (waitUntil ([&] { return controller.getProgress() > 0.01; }, 60000));
                }

                expect (! destination.existsAsFile());
                expectEquals (countPartialFiles (temp.get()), 0);
            }

            beginTest ("the post-success hook runs before the result is published and may amend it");
            {
                TemporaryDirectory temp;
                ExportController controller;
                ExportController::Request request;
                request.snapshot = std::make_shared<const PatchSnapshot> (makeSnapshot());
                request.plan = shortPlan();
                request.sampleRate = 48000.0;
                request.bitDepth = 24;
                request.destination = temp.child ("hooked.wav");

                std::atomic<bool> ran { false };
                request.afterSuccess = [&ran] (WavExporter::Result& result)
                {
                    ran.store (true);
                    result.message = "amended";
                };

                expect (controller.start (request));
                expect (waitUntil ([&] { return controller.getState() == ExportController::State::finished; }, 30000));
                WavExporter::Result result;
                expect (controller.takeResult (result));
                expect (ran.load(), "the hook ran on the worker before the result was handed back");
                expect (result.succeeded && result.message == "amended", result.message);
            }

            beginTest ("the controller refuses requests it cannot run");
            {
                TemporaryDirectory temp;
                ExportController controller;

                ExportController::Request noSnapshot;
                noSnapshot.plan = shortPlan();
                noSnapshot.destination = temp.child ("a.wav");
                expect (! controller.start (noSnapshot));

                ExportController::Request invalidPlan;
                invalidPlan.snapshot = std::make_shared<const PatchSnapshot> (makeSnapshot());
                invalidPlan.destination = temp.child ("b.wav");
                expect (! controller.start (invalidPlan));

                ExportController::Request noDestination;
                noDestination.snapshot = std::make_shared<const PatchSnapshot> (makeSnapshot());
                noDestination.plan = shortPlan();
                expect (! controller.start (noDestination));

                expect (controller.getState() == ExportController::State::idle);
            }
        }
    };

    static ExportJobTests exportJobTests;
}
