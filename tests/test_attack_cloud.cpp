#include "TestHelpers.h"

#include "../Source/Engine/AttackCloud.h"
#include "../Source/Engine/GranularEngine.h"
#include "../Source/Presets/LockPolicy.h"
#include "../Source/Presets/PresetManager.h"

#include <cmath>
#include <vector>

// F17 attack-to-cloud: the gain plan (hold, smoothstep crossfade, latching, clamps), the exact
// mixing identity through the engine, the early end of a short sample, the default path, and the
// rules that connect the controls to presets, locks and Gentle Mutate.

namespace texture::tests
{
    namespace
    {
        constexpr double attackRate = 48000.0;
        constexpr int attackBlock = 128;

        AttackCloudSettings attackOn (float holdMs, float transitionMs, float original = 1.0f)
        {
            AttackCloudSettings settings;
            settings.on = true;
            settings.holdMs = holdMs;
            settings.transitionMs = transitionMs;
            settings.original = original;
            return settings;
        }

        /** Renders `blocks` blocks of one note, returning the left channel. The envelope is set to
            attack in 1 ms and then hold at full level, so the ADSR is exactly one from 64 samples on. */
        std::vector<float> renderNote (GranularEngine& engine, const SampleBufferPtr& sample, int blocks)
        {
            const auto targets = deriveMorphTargets (0.5f);
            juce::AudioBuffer<float> buffer (2, attackBlock);
            std::vector<float> out;

            for (int b = 0; b < blocks; ++b)
            {
                buffer.clear();
                engine.renderNextBlock (sample, targets, buffer, 0, attackBlock);

                for (int i = 0; i < attackBlock; ++i)
                    out.push_back (buffer.getSample (0, i));
            }

            return out;
        }

        /** Prepares an engine with a flat full-level envelope and starts one note on it. */
        void startNote (GranularEngine& engine, const AttackCloudSettings& settings)
        {
            engine.prepare (attackRate, attackBlock);
            engine.setAdsrParameters (1.0f, 500.0f, 1.0f, 1200.0f);
            engine.setAttackCloud (settings);
            engine.noteOn (1, 60, 1.0f);
        }
    }

    class AttackCloudTests : public juce::UnitTest
    {
    public:
        AttackCloudTests()
            : juce::UnitTest ("Attack to cloud", "texture-attack") {}

        void runTest() override
        {
            beginTest ("the smoothstep is zero at both ends, monotonic and symmetric about one half");
            {
                expectWithinAbsoluteError (AttackCloudPlan::smoothstep (0.0f), 0.0f, 1.0e-7f);
                expectWithinAbsoluteError (AttackCloudPlan::smoothstep (1.0f), 1.0f, 1.0e-7f);
                expectWithinAbsoluteError (AttackCloudPlan::smoothstep (0.5f), 0.5f, 1.0e-7f);
                expectWithinAbsoluteError (AttackCloudPlan::smoothstep (-1.0f), 0.0f, 1.0e-7f);
                expectWithinAbsoluteError (AttackCloudPlan::smoothstep (2.0f), 1.0f, 1.0e-7f);

                float previous = -1.0f;
                bool monotonic = true;
                for (int i = 0; i <= 100; ++i)
                {
                    const auto value = AttackCloudPlan::smoothstep (static_cast<float> (i) / 100.0f);
                    monotonic = monotonic && value >= previous;
                    previous = value;
                    expectWithinAbsoluteError (value + AttackCloudPlan::smoothstep (1.0f - static_cast<float> (i) / 100.0f),
                                               1.0f, 1.0e-5f);
                }
                expect (monotonic, "smoothstep must not decrease");
            }

            beginTest ("the gains hold the direct read, crossfade with cloud + direct = 1, then hand over");
            {
                AttackCloudPlan plan;
                plan.latch (attackOn (20.0f, 50.0f), attackRate);
                const auto hold = plan.getHoldSamples();
                const auto transition = plan.getTransitionSamples();
                expectEquals (hold, 960);
                expectEquals (transition, 2400);

                float cloud = -1.0f;
                float direct = -1.0f;

                plan.gainsAt (0, cloud, direct);
                expectWithinAbsoluteError (cloud, 0.0f, 1.0e-7f);
                expectWithinAbsoluteError (direct, 1.0f, 1.0e-7f);

                plan.gainsAt (hold - 1, cloud, direct);
                expectWithinAbsoluteError (cloud, 0.0f, 1.0e-7f);
                expectWithinAbsoluteError (direct, 1.0f, 1.0e-7f);

                bool sumsToOne = true;
                for (int n = hold; n < hold + transition; n += 37)
                {
                    plan.gainsAt (n, cloud, direct);
                    sumsToOne = sumsToOne && std::abs (cloud + direct - 1.0f) <= 1.0e-6f;
                }
                expect (sumsToOne, "during the crossfade the cloud and direct gains must sum to one");

                plan.gainsAt (hold + transition, cloud, direct);
                expectWithinAbsoluteError (cloud, 1.0f, 1.0e-7f);
                expectWithinAbsoluteError (direct, 0.0f, 1.0e-7f);
            }

            beginTest ("times are latched at note-on, clamped, and a non-finite time falls to its minimum");
            {
                AttackCloudPlan plan;
                auto settings = attackOn (40.0f, 250.0f);
                plan.latch (settings, attackRate);
                const auto latched = plan.getTotalSamples();

                settings.holdMs = 400.0f;
                settings.transitionMs = 1500.0f;
                expectEquals (plan.getTotalSamples(), latched);

                plan.latch (attackOn (-50.0f, 0.0f), attackRate);
                expectEquals (plan.getHoldSamples(), 0);
                expectEquals (plan.getTransitionSamples(), 240);   // 5 ms minimum

                plan.latch (attackOn (9000.0f, 9000.0f), attackRate);
                expectEquals (plan.getHoldSamples(), 24000);       // 500 ms maximum
                expectEquals (plan.getTransitionSamples(), 96000); // 2 s maximum

                plan.latch (attackOn (std::nanf (""), std::nanf ("")), attackRate);
                expectEquals (plan.getHoldSamples(), 0);
                expectEquals (plan.getTransitionSamples(), 240);
            }

            beginTest ("zero hold starts the crossfade at the note-on");
            {
                AttackCloudPlan plan;
                plan.latch (attackOn (0.0f, 100.0f), attackRate);
                float cloud = 0.0f;
                float direct = 0.0f;
                plan.gainsAt (0, cloud, direct);
                expectWithinAbsoluteError (cloud, 0.0f, 1.0e-7f);
                expectWithinAbsoluteError (direct, 1.0f, 1.0e-7f);
                plan.gainsAt (2400, cloud, direct);
                expectWithinAbsoluteError (cloud, 0.5f, 1.0e-6f);
            }

            beginTest ("the direct attack and the cloud mix to the exact crossfade identity, sample for sample");
            {
                const auto sample = makeTestSample (attackRate, 2.0, 220.0);
                const auto settings = attackOn (20.0f, 50.0f, 0.5f);

                GranularEngine with;
                startNote (with, settings);
                GranularEngine without;
                startNote (without, AttackCloudSettings {});
                const auto on = renderNote (with, sample, 80);
                const auto off = renderNote (without, sample, 80);

                const auto hold = 960;
                const auto total = 960 + 2400;
                const auto source = sample->getChannel (0);

                // With ADSR at one and velocity at one, the attack-off output is the granular sum.
                // The attack-on output is the cloud share of that sum plus the direct read, so:
                // on - t x off must equal (1 - t) x original x source.
                bool identity = true;
                for (int n = 64; n < total + 200 && n < static_cast<int> (on.size()); ++n)
                {
                    float t = 0.0f;
                    if (n >= hold)
                    {
                        const auto u = static_cast<float> (n - hold) / 2400.0f;
                        t = n >= total ? 1.0f : AttackCloudPlan::smoothstep (u);
                    }

                    const auto residual = on[static_cast<std::size_t> (n)] - t * off[static_cast<std::size_t> (n)];
                    const auto expected = (1.0f - t) * 0.5f * source[n];
                    identity = identity && std::abs (residual - expected) <= 1.0e-5f;
                }

                expect (identity, "attack-on output broke the crossfade identity");
            }

            beginTest ("after the crossfade the output is bit-identical to the attack-off render");
            {
                const auto sample = makeTestSample (attackRate, 2.0, 220.0);
                GranularEngine with;
                startNote (with, attackOn (20.0f, 50.0f));
                GranularEngine without;
                startNote (without, AttackCloudSettings {});
                const auto on = renderNote (with, sample, 80);
                const auto off = renderNote (without, sample, 80);

                bool identical = true;
                for (std::size_t n = 3360 + 128; n < on.size(); ++n)
                    identical = identical && juce::exactlyEqual (on[n], off[n]);

                expect (identical, "the attack path must leave nothing behind once it has finished");
            }

            beginTest ("with the feature off the render matches a default engine exactly");
            {
                const auto sample = makeTestSample (attackRate, 2.0, 220.0);
                GranularEngine explicitOff;
                startNote (explicitOff, AttackCloudSettings {});

                GranularEngine defaults;
                defaults.prepare (attackRate, attackBlock);
                defaults.setAdsrParameters (1.0f, 500.0f, 1.0f, 1200.0f);
                defaults.noteOn (1, 60, 1.0f);

                const auto a = renderNote (explicitOff, sample, 40);
                const auto b = renderNote (defaults, sample, 40);

                bool identical = a.size() == b.size();
                for (std::size_t n = 0; identical && n < a.size(); ++n)
                    identical = juce::exactlyEqual (a[n], b[n]);

                expect (identical, "a disabled attack changed the render");
            }

            beginTest ("a change to the times mid-note does not move the crossfade already in progress");
            {
                const auto sample = makeTestSample (attackRate, 2.0, 220.0);
                GranularEngine with;
                startNote (with, attackOn (20.0f, 50.0f, 0.5f));
                GranularEngine without;
                startNote (without, AttackCloudSettings {});

                const auto targets = deriveMorphTargets (0.5f);
                juce::AudioBuffer<float> buffer (2, attackBlock);
                bool identity = true;
                std::vector<float> off;

                for (int b = 0; b < 80; ++b)
                {
                    buffer.clear();
                    without.renderNextBlock (sample, targets, buffer, 0, attackBlock);
                    for (int i = 0; i < attackBlock; ++i)
                        off.push_back (buffer.getSample (0, i));
                }

                with.setAttackCloud (attackOn (400.0f, 2000.0f, 0.5f));   // edited while the note sounds

                for (int b = 0; b < 80; ++b)
                {
                    buffer.clear();
                    with.renderNextBlock (sample, targets, buffer, 0, attackBlock);

                    for (int i = 0; i < attackBlock; ++i)
                    {
                        const auto n = b * attackBlock + i;
                        if (n < 64)
                            continue;

                        float t = 0.0f;
                        if (n >= 960)
                            t = n >= 3360 ? 1.0f : AttackCloudPlan::smoothstep (static_cast<float> (n - 960) / 2400.0f);

                        const auto residual = buffer.getSample (0, i) - t * off[static_cast<std::size_t> (n)];
                        identity = identity && std::abs (residual - (1.0f - t) * 0.5f * sample->getChannel (0)[n]) <= 1.0e-5f;
                    }
                }

                expect (identity, "an edit moved the crossfade of a note already sounding");
            }

            beginTest ("a sample that ends inside the hold hands over to the cloud, finite and not silent");
            {
                const auto shortSample = makeTestSample (attackRate, 300.0 / attackRate, 220.0);
                GranularEngine engine;
                startNote (engine, attackOn (40.0f, 250.0f));
                const auto out = renderNote (engine, shortSample, 120);

                bool finite = true;
                float peak = 0.0f;
                for (const auto value : out)
                {
                    finite = finite && std::isfinite (value);
                    peak = juce::jmax (peak, std::abs (value));
                }

                expect (finite, "non-finite output after the sample ended");
                expect (peak > 0.0001f, "the cloud did not take over from a short sample");
            }

            beginTest ("a note-off during the crossfade releases both paths and decays to silence");
            {
                const auto sample = makeTestSample (attackRate, 2.0, 220.0);
                GranularEngine engine;
                startNote (engine, attackOn (20.0f, 200.0f));
                const auto targets = deriveMorphTargets (0.5f);
                juce::AudioBuffer<float> buffer (2, attackBlock);
                bool finite = true;

                for (int b = 0; b < 4; ++b)
                {
                    buffer.clear();
                    engine.renderNextBlock (sample, targets, buffer, 0, attackBlock);
                }

                engine.noteOff (1, 60);

                for (int b = 0; b < 800; ++b)
                {
                    buffer.clear();
                    engine.renderNextBlock (sample, targets, buffer, 0, attackBlock);
                    finite = finite && isFinite (buffer);
                }

                expect (finite, "non-finite output after a release during the crossfade");
                expectEquals (engine.getActiveVoiceCount(), 0);
            }

            beginTest ("the stolen and re-triggered notes stay finite across a rapid stream of attacks");
            {
                const auto sample = makeTestSample (attackRate, 2.0, 220.0);
                GranularEngine engine;
                engine.prepare (attackRate, attackBlock);
                engine.setAdsrParameters (1.0f, 500.0f, 0.8f, 800.0f);
                engine.setAttackCloud (attackOn (0.0f, 5.0f, 1.0f));

                const auto targets = deriveMorphTargets (0.5f);
                juce::AudioBuffer<float> buffer (2, attackBlock);
                bool finite = true;

                for (int b = 0; b < 300; ++b)
                {
                    if (b % 3 == 0)
                        engine.noteOn (1, 48 + (b % 24), 0.9f);

                    buffer.clear();
                    engine.renderNextBlock (sample, targets, buffer, 0, attackBlock);
                    finite = finite && isFinite (buffer);
                }

                expect (finite, "non-finite output under a rapid stream of attacks");
            }

            beginTest ("the attack controls follow Gentle Mutate's rules");
            {
                expect (! isAttackCloudParameterMutable (params::attackCloudOn, true), "the switch must not mutate");
                expect (! isAttackCloudParameterMutable (params::attackCloudHoldMs, false), "hold moved while off");
                expect (isAttackCloudParameterMutable (params::attackCloudHoldMs, true), "hold not mutable while on");
                expect (isAttackCloudParameterMutable (params::attackCloudOriginal, true), "amount not mutable while on");
                expect (isAttackCloudParameterMutable (params::morph, false), "other controls are not gated");
            }

            beginTest ("the lock registry includes the attack controls in the envelope group");
            {
                const auto* field = LockPolicy::findField (params::attackCloudHoldMs);
                expect (field != nullptr, "hold has no lock entry");
                expect (field != nullptr && field->group == LockGroup::envelope, "hold is not in the envelope group");
                expectEquals (static_cast<int> (LockPolicy::fields().size()), params::numParameters);
            }

            beginTest ("presets accept attack values in range and refuse the rest");
            {
                expect (PresetManager::isValidParameterValue (params::attackCloudOn, 1.0), "switch on refused");
                expect (! PresetManager::isValidParameterValue (params::attackCloudOn, 0.5), "fractional switch accepted");
                expect (PresetManager::isValidParameterValue (params::attackCloudHoldMs, 0.0), "zero hold refused");
                expect (! PresetManager::isValidParameterValue (params::attackCloudHoldMs, 501.0), "hold above maximum accepted");
                expect (! PresetManager::isValidParameterValue (params::attackCloudTransitionMs, 4.0), "transition below minimum accepted");
                expect (PresetManager::isValidParameterValue (params::attackCloudOriginal, 0.0), "zero amount refused");
                expect (! PresetManager::isValidParameterValue (params::attackCloudOriginal, 1.2), "amount above one accepted");
            }
        }
    };

    static AttackCloudTests attackCloudTests;
}
