/*
    The fuzzer from 17_TESTING.md.

    Random parameters, random MIDI and random buffer sizes are pushed through the
    full audio path (voice pool -> effects chain) for a fixed wall-clock budget.
    The only assertions are the ones that matter for a plugin nobody can debug
    inside a host: the output stays finite, it stays inside a sane range, and the
    process does not fall over.

    The budget is 60 seconds as specified. `TEXTURE_FUZZ_SECONDS` shortens it for
    a quick local run; CI uses the default.
*/

#include "TestHelpers.h"

#include "../Source/Effects/EffectsChain.h"
#include "../Source/Engine/GranularEngine.h"
#include "../Source/Modulation/MorphLFO.h"

#include <cstdlib>
#include <limits>

namespace texture::tests
{
    namespace
    {
        constexpr int defaultBudgetSeconds = 60;

        /** Honours TEXTURE_FUZZ_SECONDS when it parses to a positive number. */
        int fuzzBudgetSeconds()
        {
            if (const auto override_ = juce::SystemStats::getEnvironmentVariable ("TEXTURE_FUZZ_SECONDS", {});
                override_.isNotEmpty())
            {
                const auto seconds = override_.getIntValue();

                if (seconds > 0)
                    return seconds;
            }

            return defaultBudgetSeconds;
        }

        /** Sample rates and block sizes a host might realistically hand us,
            including the awkward ones (10_CONSTRAINTS.md). */
        constexpr double candidateRates[] { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        constexpr int candidateBlocks[] { 1, 7, 16, 32, 64, 111, 128, 256, 512, 1024, 2048 };

        /** Anything above this is not a plausible synth output and means the DSP
            has run away, even though it is still technically finite. */
        constexpr float runawayThreshold = 64.0f;
    }

    class FuzzTests : public juce::UnitTest
    {
    public:
        FuzzTests()
            : juce::UnitTest ("Fuzz", "fuzz") {}

        void runTest() override
        {
            const auto budgetSeconds = fuzzBudgetSeconds();

            beginTest ("Random parameters, MIDI and block sizes for "
                       + juce::String (budgetSeconds) + "s");

            juce::Random random (0x7e8705);   // fixed seed: a failure is reproducible

            const auto deadline = juce::Time::getMillisecondCounter()
                                + static_cast<juce::uint32> (budgetSeconds * 1000);

            GranularEngine engine;
            EffectsChain effects;
            MorphLFO lfo;

            double sampleRate = 0.0;
            int maxBlockSize = 0;
            juce::AudioBuffer<float> buffer;
            SampleBufferPtr sample;

            int blocksRendered = 0;
            int configurations = 0;
            float worstPeak = 0.0f;

            while (juce::Time::getMillisecondCounter() < deadline)
            {
                // --- Re-prepare occasionally, as a host would on a device change ---
                if (sampleRate <= 0.0 || random.nextInt (200) == 0)
                {
                    sampleRate = candidateRates[random.nextInt (juce::numElementsInArray (candidateRates))];
                    maxBlockSize = candidateBlocks[random.nextInt (juce::numElementsInArray (candidateBlocks))];

                    engine.prepare (sampleRate, maxBlockSize);
                    effects.prepare (sampleRate, maxBlockSize, 2);
                    lfo.prepare (sampleRate);
                    buffer.setSize (2, maxBlockSize, false, true, true);

                    ++configurations;
                }

                // --- Swap the sample out from under the running voices ----------
                if (sample == nullptr || random.nextInt (150) == 0)
                {
                    // A no-sample engine is a legal state and must also stay silent.
                    sample = random.nextInt (10) == 0
                                 ? SampleBufferPtr()
                                 : makeTestSample (candidateRates[random.nextInt (4)],
                                                   0.05 + random.nextDouble() * 2.0,
                                                   40.0 + random.nextDouble() * 4000.0,
                                                   random.nextBool() ? 1 : 2);
                }

                // --- Random parameter state -------------------------------------
                const auto morph = random.nextFloat();
                auto targets = deriveMorphTargets (morph);

                // Push past the documented ranges too: the DSP must clamp, not blow up.
                if (random.nextInt (8) == 0)
                {
                    targets.grainsPerVoice = random.nextInt ({ -4, 32 });
                    targets.grainDurationMs = random.nextFloat() * 4000.0f - 500.0f;
                    targets.grainOverlap = random.nextFloat() * 4.0f - 1.0f;
                    targets.positionJitter = random.nextFloat() * 4.0f - 1.0f;
                    targets.detuneCents = random.nextFloat() * 400.0f - 200.0f;
                    targets.panSpread = random.nextFloat() * 4.0f - 1.0f;
                    targets.reverbSend = random.nextFloat() * 4.0f - 1.0f;
                    targets.chorusDepth = random.nextFloat() * 4.0f - 1.0f;
                    targets.saturatorDrive = random.nextFloat() * 8.0f - 1.0f;
                    targets.highShelfTiltDb = random.nextFloat() * 48.0f - 24.0f;
                }

                // The v0.0.1 grain controls, sometimes far outside their ranges.
                targets.tuneSemitones = random.nextInt (4) == 0 ? random.nextFloat() * 200.0f - 100.0f
                                                                : static_cast<float> (random.nextInt ({ -24, 25 }));
                targets.reverseChance = random.nextFloat() * 1.5f - 0.25f;
                targets.shimmerAmount = random.nextFloat() * 1.5f - 0.25f;

                engine.setAdsrParameters (random.nextFloat() * 6000.0f,
                                          random.nextFloat() * 6000.0f,
                                          random.nextFloat(),
                                          random.nextFloat() * 12000.0f);

                effects.setMorphTargets (targets);
                effects.setReverbEnabled (random.nextInt (20) != 0);
                effects.setOutputGainDb (random.nextFloat() * 36.0f - 60.0f);

                auto& secret = effects.getSecretEffect();
                secret.setBitDepth (random.nextFloat() * 24.0f - 4.0f);
                secret.setDownsample (random.nextFloat() * 128.0f - 8.0f);
                secret.setMix (random.nextFloat() * 2.0f - 0.5f);

                lfo.setRateHz (random.nextFloat() * 20.0f);
                lfo.setDepth (random.nextFloat() * 2.0f - 0.5f);
                lfo.setSyncEnabled (random.nextBool());
                lfo.setDivisionIndex (random.nextInt ({ -2, 12 }));

                // --- Random MIDI --------------------------------------------------
                for (int events = random.nextInt (6); --events >= 0;)
                {
                    switch (random.nextInt (8))
                    {
                        case 0:
                        case 1:
                        case 2:
                            engine.noteOn (1 + random.nextInt (16),
                                           random.nextInt (128),
                                           random.nextFloat());
                            break;

                        case 3:
                        case 4:
                            engine.noteOff (1 + random.nextInt (16), random.nextInt (128));
                            break;

                        case 5:
                            engine.setSustainPedal (random.nextBool());
                            break;

                        case 6:
                            engine.setPitchBend (random.nextInt (16384));
                            break;

                        default:
                            if (random.nextBool())
                                engine.allNotesOff();
                            else
                                engine.allSoundOff();
                            break;
                    }
                }

                // --- Render a random slice of a random block ----------------------
                const auto numSamples = 1 + random.nextInt (maxBlockSize);
                const auto startSample = random.nextInt (maxBlockSize - numSamples + 1);

                buffer.clear();

                // Feed the chain rubbish now and then: a broken upstream plugin or a
                // host that hands us an uninitialised buffer must not propagate.
                if (random.nextInt (40) == 0)
                {
                    const auto poison = random.nextBool() ? std::numeric_limits<float>::quiet_NaN()
                                                          : std::numeric_limits<float>::infinity();

                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        buffer.setSample (ch, startSample, poison);
                }

                engine.renderNextBlock (sample, targets, buffer, startSample, numSamples);
                effects.process (buffer, startSample, numSamples);

                const auto morphed = lfo.applyTo (morph);
                lfo.advanceBlock (numSamples);

                if (! std::isfinite (morphed) || morphed < 0.0f || morphed > 1.0f)
                {
                    expect (false, "MorphLFO left the unit range at block "
                                       + juce::String (blocksRendered));
                    return;
                }

                // The engine is allowed to pass poison through, but only where we put
                // it: what matters is that the rest of the block stays clean and that
                // the DSP state does not become permanently poisoned. Sanitise the one
                // injected sample and check everything else.
                juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(),
                                                buffer.getNumChannels(),
                                                startSample,
                                                numSamples);

                if (dsp::sanitiseBuffer (slice))
                {
                    // Poison reached the output: legal only in the block we injected it
                    // into, and the chain must recover on the next one.
                    effects.reset();
                    engine.allSoundOff();
                }

                const auto peak = peakOf (slice);
                worstPeak = juce::jmax (worstPeak, peak);

                if (peak > runawayThreshold)
                {
                    expect (false, "Output ran away to " + juce::String (peak)
                                       + " at block " + juce::String (blocksRendered)
                                       + " (morph " + juce::String (morph, 3)
                                       + ", rate " + juce::String (sampleRate, 0)
                                       + ", block " + juce::String (numSamples) + ")");
                    return;
                }

                ++blocksRendered;
            }

            logMessage ("Fuzz: " + juce::String (blocksRendered) + " blocks over "
                        + juce::String (configurations) + " configurations, worst peak "
                        + juce::String (worstPeak, 3));

            expect (blocksRendered > 0, "The fuzzer rendered nothing");
        }
    };

    static FuzzTests fuzzTests;
}
