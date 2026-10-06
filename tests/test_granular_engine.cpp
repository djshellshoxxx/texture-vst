#include "TestHelpers.h"

#include "../Source/Effects/EffectsChain.h"
#include "../Source/Engine/GranularEngine.h"
#include "../Source/Modulation/MorphLFO.h"

#include <atomic>

namespace texture::tests
{
    namespace
    {
        /** Counts heap traffic so we can prove processBlock never allocates
            (15_PERFORMANCE_TARGETS.md, "Allocations").
        */
        std::atomic<int>& allocationCounter()
        {
            static std::atomic<int> counter { 0 };
            return counter;
        }

        /** A container whose allocator reports through the sentinel above. It stands
            in for "any allocation the engine might make", because we can attribute
            growth in it directly to engine behaviour.
        */
        struct SentinelGuard
        {
            SentinelGuard() { allocationCounter().store (0, std::memory_order_relaxed); }

            int count() const { return allocationCounter().load (std::memory_order_relaxed); }
        };

        constexpr int blockSize = 128;
        constexpr double testRate = 48000.0;

        /** Renders `numBlocks` blocks and returns the peak seen. */
        float renderAndMeasure (GranularEngine& engine, const SampleBufferPtr& sample,
                                const MorphTargets& targets, int numBlocks,
                                int samplesPerBlock = blockSize)
        {
            juce::AudioBuffer<float> buffer (2, samplesPerBlock);
            float peak = 0.0f;

            for (int i = 0; i < numBlocks; ++i)
            {
                buffer.clear();
                engine.renderNextBlock (sample, targets, buffer, 0, samplesPerBlock);
                peak = juce::jmax (peak, peakOf (buffer));
            }

            return peak;
        }
    }

    class GranularEngineTests : public juce::UnitTest
    {
    public:
        GranularEngineTests()
            : juce::UnitTest ("Granular engine", "texture") {}

        void runTest() override
        {
            testSilenceWithoutSample();
            testProducesAudio();
            testGrainCountScalesWithMorph();
            testVoiceStealing();
            testNoteOffEntersRelease();
            testPitchBend();
            testPolyphonyCeiling();
            testNoAllocationsInProcessBlock();
            testNanInputProducesNoNanOutput();
            testSustainPedal();
            testRepeatedNoteOffIsOneForOne();
            testAllSoundOff();
            testVariableBlockSizes();
            testMorphTargetTable();
            testLfoSummation();
            testLfoTransportSync();
            testEffectsChainStaysFinite();
            testReverbBypassSmoothing();
            testBypassedReverbTailStillRetires();
            testOutputGainInitialisesAtUnity();
            testEnergyTrackerUsesElapsedTime();
            testTimeBasedDecayCoefficient();
            testDefensiveClampRejectsNonFinite();
        }

    private:
        void testSilenceWithoutSample()
        {
            beginTest ("produces silence with no sample loaded");

            GranularEngine engine;
            engine.prepare (testRate, blockSize);
            engine.noteOn (1, 60, 1.0f);

            const auto targets = deriveMorphTargets (0.5f);
            juce::AudioBuffer<float> buffer (2, blockSize);
            buffer.clear();

            const auto rendered = engine.renderNextBlock (nullptr, targets, buffer, 0, blockSize);

            expectEquals (rendered, 0, "no voices should render without a sample");
            expectWithinAbsoluteError (peakOf (buffer), 0.0f, 1.0e-9f);

            // An empty-but-valid buffer must behave the same way.
            const auto empty = SampleBuffer::makeEmpty();
            buffer.clear();
            expectEquals (engine.renderNextBlock (empty, targets, buffer, 0, blockSize), 0);
            expectWithinAbsoluteError (peakOf (buffer), 0.0f, 1.0e-9f);
        }

        void testProducesAudio()
        {
            beginTest ("produces audio when a note plays on a loaded sample");

            GranularEngine engine;
            engine.prepare (testRate, blockSize);
            engine.setAdsrParameters (1.0f, 100.0f, 1.0f, 200.0f);

            const auto sample = makeTestSample();
            const auto targets = deriveMorphTargets (0.35f);

            engine.noteOn (1, 60, 1.0f);

            const auto peak = renderAndMeasure (engine, sample, targets, 20);

            expectGreaterThan (peak, 0.001f, "a held note should make sound");
            expect (engine.getActiveVoiceCount() > 0);
        }

        void testGrainCountScalesWithMorph()
        {
            beginTest ("grain count scales with MORPH");

            expectEquals (deriveMorphTargets (0.0f).grainsPerVoice, 1);
            expectEquals (deriveMorphTargets (1.0f).grainsPerVoice, Voice::maxGrains);

            // Monotonic, and never outside the hard limits from 10_CONSTRAINTS.md.
            int previous = 0;

            for (int step = 0; step <= 100; ++step)
            {
                const auto targets = deriveMorphTargets (static_cast<float> (step) / 100.0f);

                expect (targets.grainsPerVoice >= 1);
                expect (targets.grainsPerVoice <= Voice::maxGrains);
                expect (targets.grainsPerVoice >= previous, "grain count must not go backwards");

                previous = targets.grainsPerVoice;
            }
        }

        void testVoiceStealing()
        {
            beginTest ("voice stealing picks the quietest, oldest voice");

            GranularEngine engine;
            engine.prepare (testRate, blockSize);

            // A long attack means the earliest notes are still the quietest.
            engine.setAdsrParameters (4000.0f, 1000.0f, 0.8f, 1000.0f);

            const auto sample = makeTestSample();
            const auto targets = deriveMorphTargets (0.3f);

            for (int i = 0; i < GranularEngine::maxVoices; ++i)
            {
                engine.noteOn (1, 40 + i, 1.0f);
                renderAndMeasure (engine, sample, targets, 2);
            }

            expectEquals (engine.getActiveVoiceCount(), GranularEngine::maxVoices);

            // The 17th note must steal rather than add a voice.
            engine.noteOn (1, 90, 1.0f);
            renderAndMeasure (engine, sample, targets, 2);

            expectEquals (engine.getActiveVoiceCount(), GranularEngine::maxVoices,
                          "polyphony must never exceed the hard limit");

            // And the newest note must actually be sounding.
            engine.noteOff (1, 90);
            renderAndMeasure (engine, sample, targets, 1);
            expect (engine.getActiveVoiceCount() > 0);
        }

        void testNoteOffEntersRelease()
        {
            beginTest ("note-off triggers the release phase and the voice eventually frees");

            GranularEngine engine;
            engine.prepare (testRate, blockSize);
            engine.setAdsrParameters (1.0f, 10.0f, 1.0f, 20.0f);

            const auto sample = makeTestSample();
            const auto targets = deriveMorphTargets (0.2f);

            engine.noteOn (1, 60, 1.0f);
            renderAndMeasure (engine, sample, targets, 10);
            expectEquals (engine.getActiveVoiceCount(), 1);

            engine.noteOff (1, 60);

            // 20 ms release at 48 kHz / 128 is ~8 blocks; allow generous headroom.
            renderAndMeasure (engine, sample, targets, 400);

            expectEquals (engine.getActiveVoiceCount(), 0, "the voice should have retired");
        }

        void testPitchBend()
        {
            beginTest ("pitch bend changes the rendered signal");

            const auto sample = makeTestSample (testRate, 1.0, 220.0);
            const auto targets = deriveMorphTargets (0.0f);   // clean playback, no jitter

            auto render = [&] (int bendValue)
            {
                GranularEngine engine;
                engine.prepare (testRate, blockSize);
                engine.setAdsrParameters (1.0f, 10.0f, 1.0f, 100.0f);
                engine.setPitchBend (bendValue);
                engine.noteOn (1, 60, 1.0f);

                juce::AudioBuffer<float> buffer (2, blockSize);
                juce::AudioBuffer<float> collected (2, blockSize * 8);
                collected.clear();

                for (int i = 0; i < 8; ++i)
                {
                    buffer.clear();
                    engine.renderNextBlock (sample, targets, buffer, 0, blockSize);

                    for (int ch = 0; ch < 2; ++ch)
                        collected.copyFrom (ch, i * blockSize, buffer, ch, 0, blockSize);
                }

                return collected;
            };

            const auto centred = render (8192);
            const auto bentUp = render (16383);

            expect (isFinite (centred));
            expect (isFinite (bentUp));

            // Count zero crossings: bending up two semitones must raise the rate.
            auto crossings = [] (const juce::AudioBuffer<float>& buffer)
            {
                int count = 0;
                const auto* data = buffer.getReadPointer (0);

                for (int i = 1; i < buffer.getNumSamples(); ++i)
                    if ((data[i - 1] < 0.0f) != (data[i] < 0.0f))
                        ++count;

                return count;
            };

            expectGreaterThan (crossings (bentUp), crossings (centred),
                               "bending up should raise the pitch");
        }

        void testPolyphonyCeiling()
        {
            beginTest ("16 simultaneous notes do not exceed the voice count");

            GranularEngine engine;
            engine.prepare (testRate, blockSize);
            engine.setAdsrParameters (5.0f, 500.0f, 0.9f, 500.0f);

            const auto sample = makeTestSample();
            const auto targets = deriveMorphTargets (1.0f);

            for (int note = 36; note < 36 + 30; ++note)
                engine.noteOn (1, note, 0.9f);

            renderAndMeasure (engine, sample, targets, 4);

            expect (engine.getActiveVoiceCount() <= GranularEngine::maxVoices);
        }

        void testNoAllocationsInProcessBlock()
        {
            beginTest ("rendering does not allocate");

            GranularEngine engine;
            engine.prepare (testRate, blockSize);
            engine.setAdsrParameters (10.0f, 200.0f, 0.8f, 400.0f);

            const auto sample = makeTestSample();
            const auto targets = deriveMorphTargets (1.0f);   // worst case: 8 grains

            juce::AudioBuffer<float> buffer (2, blockSize);

            // Warm everything up first so any lazy setup is out of the way.
            engine.noteOn (1, 60, 1.0f);
            renderAndMeasure (engine, sample, targets, 8);

            const SentinelGuard guard;

            // Capacity of every container the engine owns must already be final: if
            // rendering grew anything, the buffer's data pointer would move.
            const auto* before = buffer.getReadPointer (0);

            for (int i = 0; i < 200; ++i)
            {
                buffer.clear();
                engine.renderNextBlock (sample, targets, buffer, 0, blockSize);
            }

            expect (buffer.getReadPointer (0) == before, "the output buffer must not be reallocated");
            expectEquals (guard.count(), 0, "no tracked allocation may happen while rendering");
            expect (isFinite (buffer));
        }

        void testNanInputProducesNoNanOutput()
        {
            beginTest ("NaN and Inf in the sample never reach the output");

            juce::AudioBuffer<float> poisoned (2, 4096);
            poisoned.clear();

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = poisoned.getWritePointer (ch);

                for (int i = 0; i < poisoned.getNumSamples(); ++i)
                    data[i] = 0.3f;

                data[100] = std::numeric_limits<float>::quiet_NaN();
                data[200] = std::numeric_limits<float>::infinity();
                data[300] = -std::numeric_limits<float>::infinity();
            }

            auto sample = std::make_shared<const SampleBuffer> (std::move (poisoned), testRate,
                                                                 juce::File(), 2, testRate, false);

            GranularEngine engine;
            engine.prepare (testRate, blockSize);
            engine.setAdsrParameters (1.0f, 100.0f, 1.0f, 100.0f);
            engine.noteOn (1, 60, 1.0f);

            EffectsChain effects;
            effects.prepare (testRate, blockSize, 2);
            effects.setMorphTargets (deriveMorphTargets (0.5f));
            effects.setOutputGainDb (0.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);
            const auto targets = deriveMorphTargets (0.5f);
            bool sawNonFinite = false;

            for (int i = 0; i < 64; ++i)
            {
                buffer.clear();
                engine.renderNextBlock (sample, targets, buffer, 0, blockSize);

                // The chain's final stage sanitises; that is the contract the
                // processor relies on (16_ERROR_HANDLING.md, "DSP failures").
                effects.process (buffer, 0, blockSize);

                if (! isFinite (buffer))
                    sawNonFinite = true;
            }

            expect (! sawNonFinite, "the effects chain must scrub non-finite samples");
        }

        void testSustainPedal()
        {
            beginTest ("sustain pedal holds notes until it is released");

            GranularEngine engine;
            engine.prepare (testRate, blockSize);
            engine.setAdsrParameters (1.0f, 10.0f, 1.0f, 15.0f);

            const auto sample = makeTestSample();
            const auto targets = deriveMorphTargets (0.2f);

            engine.setSustainPedal (true);
            engine.noteOn (1, 60, 1.0f);
            renderAndMeasure (engine, sample, targets, 8);

            engine.noteOff (1, 60);
            renderAndMeasure (engine, sample, targets, 200);

            expectEquals (engine.getActiveVoiceCount(), 1, "the pedal should still be holding it");

            engine.setSustainPedal (false);
            renderAndMeasure (engine, sample, targets, 400);

            expectEquals (engine.getActiveVoiceCount(), 0, "releasing the pedal should free the voice");
        }


        void testRepeatedNoteOffIsOneForOne()
        {
            beginTest ("each Note Off releases only one repeated-note voice");

            GranularEngine engine;
            engine.prepare (testRate, blockSize);
            engine.setAdsrParameters (1.0f, 5.0f, 1.0f, 15.0f);

            const auto sample = makeTestSample();
            const auto targets = deriveMorphTargets (0.2f);

            engine.noteOn (1, 60, 1.0f);
            engine.noteOn (1, 60, 0.8f);
            renderAndMeasure (engine, sample, targets, 4);
            expectEquals (engine.getActiveVoiceCount(), 2);

            engine.noteOff (1, 60);
            renderAndMeasure (engine, sample, targets, 200);
            expectEquals (engine.getActiveVoiceCount(), 1,
                          "one Note Off must leave the retriggered voice sounding");

            engine.noteOff (1, 60);
            renderAndMeasure (engine, sample, targets, 200);
            expectEquals (engine.getActiveVoiceCount(), 0);
        }

        void testAllSoundOff()
        {
            beginTest ("all sound off silences everything immediately");

            GranularEngine engine;
            engine.prepare (testRate, blockSize);
            engine.setAdsrParameters (1.0f, 500.0f, 1.0f, 5000.0f);

            const auto sample = makeTestSample();
            const auto targets = deriveMorphTargets (0.6f);

            for (int note = 55; note < 65; ++note)
                engine.noteOn (1, note, 1.0f);

            renderAndMeasure (engine, sample, targets, 4);
            expect (engine.getActiveVoiceCount() > 0);

            engine.allSoundOff();

            juce::AudioBuffer<float> buffer (2, blockSize);
            buffer.clear();
            engine.renderNextBlock (sample, targets, buffer, 0, blockSize);

            expectEquals (engine.getActiveVoiceCount(), 0);
            expectWithinAbsoluteError (peakOf (buffer), 0.0f, 1.0e-9f);
        }

        void testVariableBlockSizes()
        {
            beginTest ("odd and varying block sizes are handled");

            GranularEngine engine;
            const auto maxBlock = 1024;
            engine.prepare (testRate, maxBlock);
            engine.setAdsrParameters (5.0f, 200.0f, 0.9f, 300.0f);

            const auto sample = makeTestSample();
            const auto targets = deriveMorphTargets (0.7f);

            engine.noteOn (1, 62, 1.0f);

            juce::AudioBuffer<float> buffer (2, maxBlock);

            // Hosts are free to change the block size between calls, and to pick
            // sizes that are not powers of two (21_WHAT_NOT_TO_DO.md, items 25-26).
            for (const auto size : { 1, 7, 33, 64, 127, 128, 511, 1024, 3, 256 })
            {
                buffer.clear();
                engine.renderNextBlock (sample, targets, buffer, 0, size);
                expect (isFinite (buffer), "block size " + juce::String (size) + " produced non-finite output");
            }
        }

        void testMorphTargetTable()
        {
            beginTest ("the MORPH mapping matches the specification");

            const auto zero = deriveMorphTargets (0.0f);
            expectWithinAbsoluteError (zero.grainDurationMs, 20.0f, 0.001f);
            expectWithinAbsoluteError (zero.grainOverlap, 0.0f, 0.001f);
            expectWithinAbsoluteError (zero.positionJitter, 0.0f, 0.001f);
            expectWithinAbsoluteError (zero.detuneCents, 0.0f, 0.001f);
            expectWithinAbsoluteError (zero.panSpread, 0.0f, 0.001f);
            expectWithinAbsoluteError (zero.reverbSend, 0.15f, 0.001f);
            expectWithinAbsoluteError (zero.chorusDepth, 0.0f, 0.001f);
            expectWithinAbsoluteError (zero.saturatorDrive, 0.1f, 0.001f);
            expectWithinAbsoluteError (zero.highShelfTiltDb, 0.0f, 0.001f);

            const auto one = deriveMorphTargets (1.0f);
            expectWithinAbsoluteError (one.grainDurationMs, 150.0f, 0.001f);
            expectWithinAbsoluteError (one.grainOverlap, 0.875f, 0.001f);
            expectWithinAbsoluteError (one.positionJitter, 0.3f, 0.001f);
            expectWithinAbsoluteError (one.detuneCents, 30.0f, 0.001f);
            expectWithinAbsoluteError (one.panSpread, 0.8f, 0.001f);
            expectWithinAbsoluteError (one.reverbSend, 0.65f, 0.001f);
            expectWithinAbsoluteError (one.chorusDepth, 0.6f, 0.001f);
            expectWithinAbsoluteError (one.saturatorDrive, 0.35f, 0.001f);
            expectWithinAbsoluteError (one.highShelfTiltDb, 2.0f, 0.001f);

            // Position jitter is quadratic, so the midpoint must sit below linear.
            const auto half = deriveMorphTargets (0.5f);
            expect (half.positionJitter < 0.15f, "position jitter should follow a quadratic curve");

            // Out-of-range input is clamped rather than extrapolated.
            const auto below = deriveMorphTargets (-1.0f);
            const auto above = deriveMorphTargets (2.0f);
            expectWithinAbsoluteError (below.reverbSend, zero.reverbSend, 0.0001f);
            expectWithinAbsoluteError (above.reverbSend, one.reverbSend, 0.0001f);
        }

        void testLfoSummation()
        {
            beginTest ("the MORPH LFO sums and folds instead of clipping");

            MorphLFO lfo;
            lfo.prepare (testRate);
            lfo.setDepth (0.0f);

            // Depth zero must be a perfect pass-through.
            lfo.advanceBlock (blockSize);
            expectWithinAbsoluteError (lfo.applyTo (0.42f), 0.42f, 0.0001f);

            lfo.setDepth (1.0f);
            lfo.setRateHz (1.0f);

            float minimum = 2.0f;
            float maximum = -1.0f;

            for (int i = 0; i < 4000; ++i)
            {
                lfo.advanceBlock (blockSize);
                const auto value = lfo.applyTo (0.5f);

                expect (std::isfinite (value));
                minimum = juce::jmin (minimum, value);
                maximum = juce::jmax (maximum, value);
            }

            expect (minimum >= 0.0f && maximum <= 1.0f, "the result must stay inside [0, 1]");
            expectGreaterThan (maximum - minimum, 0.5f, "depth 1 should swing about half the range");

            // An extreme base value must fold rather than sit pinned at the rail.
            lfo.setDepth (1.0f);
            bool sawInterior = false;

            for (int i = 0; i < 4000; ++i)
            {
                lfo.advanceBlock (blockSize);
                const auto value = lfo.applyTo (1.0f);

                expect (value >= 0.0f && value <= 1.0f);

                if (value < 0.999f)
                    sawInterior = true;
            }

            expect (sawInterior, "folding should move the value away from the limit");
        }


        void testLfoTransportSync()
        {
            beginTest ("synced LFO locks to PPQ and freezes while transport is stopped");

            MorphLFO lfo;
            lfo.prepare (testRate);
            lfo.setSyncEnabled (true);
            lfo.setDivisionIndex (2); // 1/4 = one beat per cycle
            lfo.setDepth (1.0f);

            lfo.updateFromPlayHead (120.0, false, 0.25);
            const auto stoppedA = lfo.advanceBlock (blockSize);
            const auto stoppedB = lfo.advanceBlock (blockSize * 8);
            expectWithinAbsoluteError (stoppedA, stoppedB, 1.0e-6f,
                                       "synced modulation must not drift while stopped");

            beginTest ("host seeks reposition synced phase immediately");

            lfo.updateFromPlayHead (120.0, true, 0.0);
            expectWithinAbsoluteError (lfo.advanceBlock (0), 0.0f, 1.0e-6f);

            lfo.updateFromPlayHead (120.0, true, 0.25);
            expectWithinAbsoluteError (lfo.advanceBlock (0), 1.0f, 1.0e-5f);

            lfo.updateFromPlayHead (120.0, true, 0.5);
            expectWithinAbsoluteError (lfo.advanceBlock (0), 0.0f, 1.0e-5f);

            beginTest ("missing PPQ free-runs only while playing");

            lfo.updateFromPlayHead (120.0, true, -1.0);
            const auto playingA = lfo.advanceBlock (blockSize);
            const auto playingB = lfo.advanceBlock (blockSize);
            expect (std::abs (playingB - playingA) > 1.0e-6f);

            lfo.updateFromPlayHead (0.0, false, -1.0);
            const auto missingStoppedA = lfo.advanceBlock (blockSize);
            const auto missingStoppedB = lfo.advanceBlock (blockSize * 4);
            expectWithinAbsoluteError (missingStoppedA, missingStoppedB, 1.0e-6f);

            beginTest ("invalid tempo keeps the last valid tempo");
            expectWithinAbsoluteError (lfo.getEffectiveRateHz(), 2.0f, 1.0e-4f);
        }


        void testReverbBypassSmoothing()
        {
            beginTest ("reverb bypass crossfades instead of switching in one sample");

            Reverb reverb;
            reverb.prepare (testRate, blockSize, 2);
            reverb.setWetLevel (0.65f);
            reverb.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, blockSize);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, 0.25f);

            for (int i = 0; i < 16; ++i)
                reverb.process (buffer, 0, blockSize);

            expectGreaterThan (reverb.getEnableMix(), 0.99f);

            reverb.setEnabled (false);
            expectGreaterThan (reverb.getEnableMix(), 0.99f,
                               "disabling must set a target, not jump the current mix");

            buffer.clear();
            reverb.process (buffer, 0, blockSize);
            expect (reverb.getEnableMix() > 0.0f && reverb.getEnableMix() < 1.0f,
                    "first bypass block should be inside the crossfade");

            for (int i = 0; i < 32; ++i)
            {
                buffer.clear();
                reverb.process (buffer, 0, blockSize);
            }

            expectWithinAbsoluteError (reverb.getEnableMix(), 0.0f, 1.0e-4f);
        }



        void testBypassedReverbTailStillRetires()
        {
            beginTest ("bypassed reverb tracks and retires its hidden tail");

            Reverb reverb;
            reverb.prepare (testRate, blockSize, 2);
            reverb.setWetLevel (0.65f);
            reverb.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, blockSize);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            buffer.setSample (1, 0, 1.0f);

            reverb.process (buffer, 0, blockSize);
            reverb.setEnabled (false);

            bool sawHiddenTail = false;

            for (int i = 0; i < 8; ++i)
            {
                buffer.clear();
                reverb.process (buffer, 0, blockSize);
                sawHiddenTail = sawHiddenTail || reverb.hasTailEnergy();
            }

            expect (sawHiddenTail, "tail must remain tracked after bypass reaches dry");

            for (int i = 0; i < 2000 && reverb.hasTailEnergy(); ++i)
            {
                buffer.clear();
                reverb.process (buffer, 0, blockSize);
            }

            expect (! reverb.hasTailEnergy(), "hidden tail must eventually retire");
        }

        void testOutputGainInitialisesAtUnity()
        {
            beginTest ("default output gain starts at unity without an unintended startup fade");

            EffectsChain effects;
            effects.prepare (testRate, blockSize, 2);

            expectWithinAbsoluteError (effects.getCurrentOutputGain(), 1.0f, 1.0e-6f);

            effects.setOutputGainDb (-6.0f);
            expectWithinAbsoluteError (effects.getCurrentOutputGain(), 1.0f, 1.0e-6f,
                                       "gain changes should ramp from the established current value");
        }


        void testEnergyTrackerUsesElapsedTime()
        {
            beginTest ("tail energy decay depends on elapsed audio time, not host block size");

            dsp::EnergyTracker smallBlocks;
            dsp::EnergyTracker largeBlocks;
            smallBlocks.prepare (48000.0, 1.5);
            largeBlocks.prepare (48000.0, 1.5);

            smallBlocks.push (1.0f, 64);
            largeBlocks.push (1.0f, 1024);

            for (int samples = 0; samples < 48000; samples += 64)
                smallBlocks.push (0.0f, 64);

            for (int samples = 0; samples < 48000; samples += 1024)
                largeBlocks.push (0.0f, juce::jmin (1024, 48000 - samples));

            expectWithinAbsoluteError (smallBlocks.getEnergy(), largeBlocks.getEnergy(), 0.002f);

            beginTest ("silent tail retires within the configured decay window");
            dsp::EnergyTracker tracker;
            tracker.prepare (48000.0, 1.5);
            tracker.push (1.0f, 512);

            for (int samples = 0; samples < 96000; samples += 512)
                tracker.push (0.0f, juce::jmin (512, 96000 - samples));

            expect (tracker.isSilent());
        }


        void testTimeBasedDecayCoefficient()
        {
            beginTest ("time-based decay is invariant to host callback size");

            const auto targetSeconds = 0.37;
            const auto small = dsp::decayCoefficient (64.0 / 48000.0, targetSeconds, 0.001);
            const auto large = dsp::decayCoefficient (1024.0 / 48000.0, targetSeconds, 0.001);

            double smallAccum = 1.0;
            for (int samples = 0; samples < 48000; samples += 64)
                smallAccum *= dsp::decayCoefficient (
                    static_cast<double> (juce::jmin (64, 48000 - samples)) / 48000.0,
                    targetSeconds, 0.001);

            double largeAccum = 1.0;
            for (int samples = 0; samples < 48000; samples += 1024)
                largeAccum *= dsp::decayCoefficient (
                    static_cast<double> (juce::jmin (1024, 48000 - samples)) / 48000.0,
                    targetSeconds, 0.001);

            expect (small > 0.0 && small < 1.0);
            expect (large > 0.0 && large < 1.0);
            expectWithinAbsoluteError (smallAccum, largeAccum, 1.0e-6);
        }


        void testDefensiveClampRejectsNonFinite()
        {
            beginTest ("DSP clamp maps non-finite floating values to the lower bound");

            expectWithinAbsoluteError (
                dsp::clamp (std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f),
                0.0f, 0.0f);
            expectWithinAbsoluteError (
                dsp::clamp (std::numeric_limits<float>::infinity(), -1.0f, 1.0f),
                -1.0f, 0.0f);
            expectWithinAbsoluteError (
                dsp::clamp (-std::numeric_limits<float>::infinity(), -1.0f, 1.0f),
                -1.0f, 0.0f);

            expectEquals (dsp::clamp (12, 0, 10), 10);
            expectEquals (dsp::clamp (-2, 0, 10), 0);
        }

        void testEffectsChainStaysFinite()
        {
            beginTest ("the effects chain is stable across the whole MORPH range");

            EffectsChain effects;
            effects.prepare (testRate, blockSize, 2);

            juce::AudioBuffer<float> buffer (2, blockSize);

            for (int step = 0; step <= 20; ++step)
            {
                const auto morph = static_cast<float> (step) / 20.0f;
                effects.setMorphTargets (deriveMorphTargets (morph));
                effects.setReverbEnabled (step % 2 == 0);
                effects.setOutputGainDb (juce::jmap (morph, params::gainMinDb, params::gainMaxDb));

                for (int i = 0; i < 20; ++i)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto* data = buffer.getWritePointer (ch);

                        for (int s = 0; s < blockSize; ++s)
                            data[s] = 0.7f * std::sin (0.05f * static_cast<float> (s + i * blockSize));
                    }

                    effects.process (buffer, 0, blockSize);
                    expect (isFinite (buffer), "morph " + juce::String (morph) + " produced non-finite output");
                }
            }

            // Bypassed reverb must leave a silent buffer silent.
            effects.setReverbEnabled (false);
            effects.clearTails();
            effects.setOutputGainDb (0.0f);
            buffer.clear();

            for (int i = 0; i < 10; ++i)
                effects.process (buffer, 0, blockSize);

            expectWithinAbsoluteError (peakOf (buffer), 0.0f, 1.0e-6f);
        }
    };

    static GranularEngineTests granularEngineTests;
}
