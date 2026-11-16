#include "TestHelpers.h"

#include "../Source/Engine/GrainTargetResolver.h"
#include "../Source/Engine/GranularEngine.h"
#include "../Source/Presets/LockPolicy.h"
#include "../Source/Presets/PresetManager.h"
#include "../Source/Utility/StateSchema.h"

#include <array>
#include <cmath>
#include <set>
#include <string>

// F13 expandable grain controls: the mapping from Follow flags and manual values to the
// grain targets, the ramps, the density and duration the voices actually spawn at, and the
// rules that connect the controls to presets, state, locks and Gentle Mutate.

namespace texture::tests
{
    namespace
    {
        constexpr double grainRate = 48000.0;
        constexpr int grainBlock = 128;

        GrainControlSettings allFollow()
        {
            return {};
        }

        GrainControlSettings withManual (GrainField field, float value, bool follow = false)
        {
            GrainControlSettings settings;
            settings.values[static_cast<std::size_t> (field)] = value;
            settings.follow[static_cast<std::size_t> (field)] = follow;
            return settings;
        }

        bool sameTargets (const MorphTargets& a, const MorphTargets& b)
        {
            return juce::exactlyEqual (a.grainsPerVoice, b.grainsPerVoice)
                && juce::exactlyEqual (a.grainDurationMs, b.grainDurationMs)
                && juce::exactlyEqual (a.grainOverlap, b.grainOverlap)
                && juce::exactlyEqual (a.positionJitter, b.positionJitter)
                && juce::exactlyEqual (a.detuneCents, b.detuneCents)
                && juce::exactlyEqual (a.panSpread, b.panSpread)
                && juce::exactlyEqual (a.reverbSend, b.reverbSend)
                && juce::exactlyEqual (a.chorusDepth, b.chorusDepth)
                && juce::exactlyEqual (a.saturatorDrive, b.saturatorDrive)
                && juce::exactlyEqual (a.highShelfTiltDb, b.highShelfTiltDb)
                && juce::exactlyEqual (a.tuneSemitones, b.tuneSemitones)
                && juce::exactlyEqual (a.reverseChance, b.reverseChance)
                && juce::exactlyEqual (a.shimmerAmount, b.shimmerAmount)
                && juce::exactlyEqual (a.grainDensityHz, b.grainDensityHz);
        }

        /** Renders a sustained note through an engine with the given targets. Returns the
            grain starts and the largest number of grains sounding at any block end. */
        struct SpawnCount
        {
            std::uint64_t starts { 0 };
            int peakActive { 0 };
        };

        SpawnCount countSpawns (const MorphTargets& targets, double seconds)
        {
            GranularEngine engine;
            engine.prepare (grainRate, grainBlock);
            engine.setAdsrParameters (1.0f, 1.0f, 1.0f, 1000.0f);

            const auto sample = makeTestSample (grainRate, 3.0, 220.0);
            juce::AudioBuffer<float> buffer (2, grainBlock);

            engine.noteOn (1, 60, 0.9f);

            SpawnCount count;
            const auto blocks = static_cast<int> (seconds * grainRate / grainBlock);

            for (int block = 0; block < blocks; ++block)
            {
                buffer.clear();
                engine.renderNextBlock (sample, targets, buffer, 0, grainBlock);
                count.peakActive = juce::jmax (count.peakActive, engine.getActiveGrainCount());
            }

            count.starts = engine.getGrainStartCount();
            return count;
        }

        bool isFiniteTargets (const MorphTargets& t)
        {
            return std::isfinite (t.grainDurationMs) && std::isfinite (t.grainOverlap)
                && std::isfinite (t.positionJitter) && std::isfinite (t.detuneCents)
                && std::isfinite (t.panSpread) && std::isfinite (t.grainDensityHz);
        }
    }

    class GrainControlTests : public juce::UnitTest
    {
    public:
        GrainControlTests()
            : juce::UnitTest ("Grain controls", "texture") {}

        void runTest() override
        {
            beginTest ("all Follow MORPH reproduces the MORPH targets exactly across the range");
            {
                for (int step = 0; step <= 100; ++step)
                {
                    const auto morph = deriveMorphTargets (static_cast<float> (step) / 100.0f);
                    const auto resolved = GrainTargetResolver::resolve (morph, allFollow());
                    expect (sameTargets (resolved.targets, morph), "follow-all changed a target at step " + juce::String (step));
                    expect (! resolved.oversubscribed, "the MORPH maximum must not be reported as oversubscribed");
                }
            }

            beginTest ("taking a field from Follow to manual at its effective value changes nothing audible");
            {
                // The readout is the value the manual control would need, so the copy in
                // planGrainFollowChange cannot move the sound. Checked for every field and MORPH.
                for (const float morphValue : { 0.0f, 0.35f, 0.8f, 1.0f })
                {
                    const auto morph = deriveMorphTargets (morphValue);
                    const auto followed = GrainTargetResolver::resolve (morph, allFollow());

                    for (std::size_t i = 0; i < numGrainFields; ++i)
                    {
                        const auto field = static_cast<GrainField> (i);
                        const auto change = planGrainFollowChange (field, false, followed.effective[i]);
                        auto settings = allFollow();
                        settings.values[static_cast<std::size_t> (field)] = change.writes[0].value;
                        settings.follow[i] = false;
                        const auto manual = GrainTargetResolver::resolve (morph, settings);

                        expectWithinAbsoluteError (manual.targets.grainDurationMs, followed.targets.grainDurationMs, 1.0e-3f);
                        expectWithinAbsoluteError (manual.targets.positionJitter, followed.targets.positionJitter, 1.0e-5f);
                        expectWithinAbsoluteError (manual.targets.detuneCents, followed.targets.detuneCents, 1.0e-4f);
                        expectWithinAbsoluteError (manual.targets.panSpread, followed.targets.panSpread, 1.0e-6f);
                        // Density copied from the legacy cadence gives the same hop as the legacy hop.
                        if (field == GrainField::density)
                            expectWithinAbsoluteError (1.0f / manual.targets.grainDensityHz,
                                                       1.0f / followed.effective[1], 1.0e-4f);
                    }
                }
            }

            beginTest ("the defaults are Follow on, and MORPH-derived effective values are reported");
            {
                const auto morph = deriveMorphTargets (0.35f);
                const auto resolved = GrainTargetResolver::resolve (morph, allFollow());
                expectWithinAbsoluteError (resolved.effective[static_cast<std::size_t> (GrainField::size)],
                                           morph.grainDurationMs, 1.0e-4f);
                expectWithinAbsoluteError (resolved.effective[static_cast<std::size_t> (GrainField::jitter)],
                                           morph.positionJitter / 0.3f, 1.0e-6f);
                expectWithinAbsoluteError (resolved.effective[static_cast<std::size_t> (GrainField::detune)],
                                           morph.detuneCents, 1.0e-6f);
                // Pan spread is in the same units on both sides, so the readout is the value itself.
                expectWithinAbsoluteError (resolved.effective[static_cast<std::size_t> (GrainField::pan)],
                                           morph.panSpread, 1.0e-6f);

                // Legacy density is 1 / hop, where hop = duration x (1 - overlap).
                const auto legacyDensity = 1000.0f / (morph.grainDurationMs * (1.0f - morph.grainOverlap));
                expectWithinAbsoluteError (resolved.effective[static_cast<std::size_t> (GrainField::density)],
                                           legacyDensity, 1.0e-3f);
            }

            beginTest ("a manual duration keeps the MORPH overlap, so the legacy cadence scales with it");
            {
                const auto morph = deriveMorphTargets (0.6f);
                const auto resolved = GrainTargetResolver::resolve (morph,
                    withManual (GrainField::size, 40.0f));

                expectWithinAbsoluteError (resolved.targets.grainDurationMs, 40.0f, 1.0e-4f);
                expect (juce::exactlyEqual (resolved.targets.grainOverlap, morph.grainOverlap));
                expect (juce::exactlyEqual (resolved.targets.grainDensityHz, 0.0f),
                        "density must stay on the legacy cadence when only duration is manual");
            }

            beginTest ("a manual density sets the spawn rate and derives the overlap from duration");
            {
                const auto morph = deriveMorphTargets (0.35f);
                const auto resolved = GrainTargetResolver::resolve (morph,
                    withManual (GrainField::density, 40.0f));

                expectWithinAbsoluteError (resolved.targets.grainDensityHz, 40.0f, 1.0e-4f);
                // Hop = 25 ms. With MORPH's 80+ ms grain, overlap is 1 - 25/duration.
                const auto duration = resolved.targets.grainDurationMs;
                expectWithinAbsoluteError (resolved.targets.grainOverlap,
                                           juce::jlimit (0.0f, 0.95f, 1.0f - 0.025f / (duration * 0.001f)), 1.0e-4f);
                expectWithinAbsoluteError (resolved.concurrency, duration * 0.001f * 40.0f, 1.0e-4f);
            }

            beginTest ("duration and density both manual derive the overlap from them");
            {
                auto settings = withManual (GrainField::size, 100.0f);
                settings.values[static_cast<std::size_t> (GrainField::density)] = 20.0f;
                settings.follow[static_cast<std::size_t> (GrainField::density)] = false;

                const auto resolved = GrainTargetResolver::resolve (deriveMorphTargets (0.9f), settings);
                // Hop = 50 ms, duration = 100 ms, so overlap is one half.
                expectWithinAbsoluteError (resolved.targets.grainOverlap, 0.5f, 1.0e-5f);
                expectWithinAbsoluteError (resolved.concurrency, 2.0f, 1.0e-5f);
                expect (! resolved.oversubscribed);
            }

            beginTest ("values outside the documented ranges are clamped, and non-finite values use defaults");
            {
                GrainControlSettings wild;
                wild.values = { 10000.0f, -5.0f, 7.0f, std::nanf (""), -3.0f };
                const auto clean = sanitiseGrainControls (wild);

                expectWithinAbsoluteError (clean.values[0], params::grainSizeMaxMs, 1.0e-4f);
                expectWithinAbsoluteError (clean.values[1], params::grainDensityMinHz, 1.0e-4f);
                expectWithinAbsoluteError (clean.values[2], params::grainJitterMax, 1.0e-4f);
                expectWithinAbsoluteError (clean.values[3], params::grainDetuneDefaultCents, 1.0e-4f);
                expectWithinAbsoluteError (clean.values[4], params::grainPanSpreadMin, 1.0e-4f);

                for (const auto& value : clean.values)
                    expect (std::isfinite (value));
            }

            beginTest ("the pool limit is reported when the requested concurrency exceeds it");
            {
                GrainControlSettings heavy;
                heavy.values[static_cast<std::size_t> (GrainField::size)] = 500.0f;
                heavy.follow[static_cast<std::size_t> (GrainField::size)] = false;
                heavy.values[static_cast<std::size_t> (GrainField::density)] = 200.0f;
                heavy.follow[static_cast<std::size_t> (GrainField::density)] = false;

                const auto resolved = GrainTargetResolver::resolve (deriveMorphTargets (0.5f), heavy);
                expect (resolved.oversubscribed);
                expectWithinAbsoluteError (resolved.concurrency, 100.0f, 1.0e-3f);

                const auto modest = GrainTargetResolver::resolve (deriveMorphTargets (0.5f),
                    withManual (GrainField::density, 30.0f));
                expect (! modest.oversubscribed);
            }

            beginTest ("every Follow combination yields finite targets within their ranges");
            {
                for (int mask = 0; mask < 32; ++mask)
                {
                    GrainControlSettings settings;

                    for (std::size_t i = 0; i < numGrainFields; ++i)
                    {
                        settings.follow[i] = (mask & (1 << static_cast<int> (i))) != 0;
                        settings.values[i] = i == 0 ? 500.0f : (i == 1 ? 200.0f : (i == 2 ? 1.0f : (i == 3 ? 100.0f : 1.0f)));
                    }

                    for (const float morph : { 0.0f, 0.5f, 1.0f })
                    {
                        const auto resolved = GrainTargetResolver::resolve (deriveMorphTargets (morph), settings);
                        expect (isFiniteTargets (resolved.targets), "non-finite target for mask " + juce::String (mask));
                        expect (resolved.targets.grainOverlap >= 0.0f && resolved.targets.grainOverlap <= 0.95f);
                        expect (resolved.targets.positionJitter >= 0.0f && resolved.targets.positionJitter <= 0.3f + 1.0e-6f);
                        expect (resolved.targets.panSpread >= 0.0f && resolved.targets.panSpread <= 1.0f + 1.0e-6f);
                    }
                }
            }

            beginTest ("an enabled manual value is ramped over 20 ms and then settles exactly");
            {
                GrainTargetResolver resolver;
                resolver.prepare (grainRate);

                const auto morph = deriveMorphTargets (0.0f);
                const auto follow = allFollow();
                resolver.advance (morph, follow, grainBlock);

                const auto manual = withManual (GrainField::detune, 80.0f);
                const auto first = resolver.advance (morph, manual, grainBlock);
                expect (first.targets.detuneCents > morph.detuneCents, "the ramp must move toward the goal");
                expect (first.targets.detuneCents < 80.0f, "the ramp must not jump to the goal");

                GrainResolution last = first;
                const int blocksForRamp = static_cast<int> (std::ceil (GrainTargetResolver::rampSeconds * grainRate / grainBlock));

                for (int block = 0; block < blocksForRamp + 2; ++block)
                    last = resolver.advance (morph, manual, grainBlock);

                expect (juce::exactlyEqual (last.targets.detuneCents, 80.0f), "the value must settle on the manual goal");
            }

            beginTest ("switching back to Follow restores the MORPH value without a ramp");
            {
                GrainTargetResolver resolver;
                resolver.prepare (grainRate);

                const auto morph = deriveMorphTargets (0.7f);
                resolver.advance (morph, withManual (GrainField::jitter, 0.0f), grainBlock);
                const auto back = resolver.advance (morph, allFollow(), grainBlock);
                expect (juce::exactlyEqual (back.targets.positionJitter, morph.positionJitter));
            }

            beginTest ("follow-all output is identical across host block partitions");
            {
                GrainTargetResolver whole;
                GrainTargetResolver split;
                whole.prepare (grainRate);
                split.prepare (grainRate);

                const auto morph = deriveMorphTargets (0.42f);
                const auto whole512 = whole.advance (morph, allFollow(), 512);
                auto acc = split.advance (morph, allFollow(), 100);
                acc = split.advance (morph, allFollow(), 412);
                expect (sameTargets (whole512.targets, morph));
                expect (sameTargets (acc.targets, morph));
            }

            beginTest ("the follow change plan writes the value before clearing Follow, and only the flag when engaging");
            {
                const auto disengage = planGrainFollowChange (GrainField::size, false, 137.0f);
                expectEquals (disengage.count, 2);
                expect (std::string_view (disengage.writes[0].parameterID) == params::grainSizeMs);
                expectWithinAbsoluteError (disengage.writes[0].value, 137.0f, 1.0e-6f);
                expect (std::string_view (disengage.writes[1].parameterID) == params::grainSizeFollow);
                expect (juce::exactlyEqual (disengage.writes[1].value, 0.0f));

                const auto engage = planGrainFollowChange (GrainField::size, true, 137.0f);
                expectEquals (engage.count, 1);
                expect (std::string_view (engage.writes[0].parameterID) == params::grainSizeFollow);
                expect (juce::exactlyEqual (engage.writes[0].value, 1.0f));

                // An effective value the manual range cannot hold is clamped, never stored raw.
                const auto clamped = planGrainFollowChange (GrainField::density, false, 1600.0f);
                expectWithinAbsoluteError (clamped.writes[0].value, params::grainDensityMaxHz, 1.0e-4f);

                const auto nonFinite = planGrainFollowChange (GrainField::pan, false, std::nanf (""));
                expect (std::isfinite (nonFinite.writes[0].value));
            }

            beginTest ("Gentle Mutate may change a grain value only while its Follow flag is off");
            {
                auto settings = allFollow();
                expect (! isGrainParameterMutable (params::grainSizeMs, settings));
                expect (! isGrainParameterMutable (params::grainSizeFollow, settings));

                settings.follow[static_cast<std::size_t> (GrainField::size)] = false;
                expect (isGrainParameterMutable (params::grainSizeMs, settings));
                expect (! isGrainParameterMutable (params::grainSizeFollow, settings),
                        "Follow flags are switches and are never mutated");
                expect (isGrainParameterMutable (params::glideMs, settings),
                        "parameters outside the grain group are not gated here");
            }

            beginTest ("the lock registry places every grain control in the grain group");
            {
                for (const auto* id : { params::grainSizeMs, params::grainDensityHz, params::grainJitter,
                                        params::grainDetuneCents, params::grainPanSpread })
                {
                    const auto* field = LockPolicy::findField (id);
                    expect (field != nullptr && field->group == LockGroup::grain, id);
                    expect (field != nullptr && field->mutableByDefault, id);
                }

                for (const auto* id : { params::grainSizeFollow, params::grainDensityFollow,
                                        params::grainJitterFollow, params::grainDetuneFollow,
                                        params::grainPanFollow })
                {
                    const auto* field = LockPolicy::findField (id);
                    expect (field != nullptr && field->group == LockGroup::grain, id);
                    expect (field != nullptr && ! field->mutableByDefault, id);
                }
            }

            beginTest ("preset ranges reject out-of-range grain values and non-switch Follow values");
            {
                expect (! PresetManager::isValidParameterValue (params::grainSizeMs, 4.0));
                expect (! PresetManager::isValidParameterValue (params::grainSizeMs, 501.0));
                expect (PresetManager::isValidParameterValue (params::grainSizeMs, 500.0));
                expect (! PresetManager::isValidParameterValue (params::grainDensityHz, 0.5));
                expect (! PresetManager::isValidParameterValue (params::grainJitter, 1.5));
                expect (! PresetManager::isValidParameterValue (params::grainDetuneCents, 101.0));
                expect (! PresetManager::isValidParameterValue (params::grainPanSpread, -0.1));
                expect (! PresetManager::isValidParameterValue (params::grainPanFollow, 2.0));
                expect (! PresetManager::isValidParameterValue (params::grainSizeFollow, 0.5));
                expect (PresetManager::isValidParameterValue (params::grainDensityFollow, 0.0));
            }

            beginTest ("a state written before F13 reads Follow MORPH for every grain control");
            {
                juce::ValueTree parameters ("Parameters");
                juce::ValueTree oldState ("Param");
                oldState.setProperty ("id", juce::String (params::morph), nullptr);
                parameters.appendChild (oldState, nullptr);

                const auto missing = StateSchema::missingParameterIDs (parameters);
                for (const auto* id : { params::grainSizeMs, params::grainSizeFollow, params::grainDensityFollow,
                                        params::grainPanFollow })
                    expect (missing.contains (id), juce::String (id) + " must be defaulted when absent");

                for (const auto* id : { params::grainSizeFollow, params::grainDensityFollow,
                                        params::grainJitterFollow, params::grainDetuneFollow,
                                        params::grainPanFollow })
                    expectWithinAbsoluteError (params::defaultValueFor (id), 1.0f, 1.0e-6f);
            }

            beginTest ("a manual grain patch matches its archived render");
            {
                // Fixed script: manual size, density, jitter, detune and pan, two notes and a release.
                GrainControlSettings manual;
                manual.values = { 60.0f, 25.0f, 0.3f, 20.0f, 0.6f };
                manual.follow = { false, false, false, false, false };

                GrainTargetResolver resolver;
                resolver.prepare (grainRate);

                GranularEngine engine;
                engine.prepare (grainRate, grainBlock);
                engine.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);
                engine.setRandomConfig (0, 0x5445585455524531ULL);

                const auto sample = makeTestSample (grainRate, 2.0, 220.0);
                juce::AudioBuffer<float> buffer (2, grainBlock);
                std::vector<std::array<float, 3>> checkpoints;

                const auto block = [&]
                {
                    buffer.clear();
                    const auto targets = resolver.advance (deriveMorphTargets (0.35f), manual, grainBlock).targets;
                    engine.renderNextBlock (sample, targets, buffer, 0, grainBlock);

                    double sum = 0.0;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < grainBlock; ++i)
                            sum += static_cast<double> (buffer.getSample (ch, i)) * buffer.getSample (ch, i);

                    checkpoints.push_back ({ buffer.getSample (0, 37), buffer.getSample (1, 101),
                                             static_cast<float> (std::sqrt (sum / (2.0 * grainBlock))) });
                };

                engine.noteOn (1, 60, 0.8f);
                for (int i = 0; i < 12; ++i) block();
                engine.noteOn (1, 64, 0.7f);
                for (int i = 0; i < 12; ++i) block();
                engine.noteOff (1, 60);
                engine.noteOff (1, 64);
                for (int i = 0; i < 12; ++i) block();

                // Checkpoints at blocks 0, 11, 23, 30 and 35, captured from this build.
                // A change to the manual path must update them with the reason in PROGRESS.md.
                const std::array<std::array<float, 3>, 5> expected {{
                    { 0.000003f, -0.000130f, 0.000105f },
                    { -0.016954f, 0.015987f, 0.016244f },
                    { -0.054242f, 0.019906f, 0.036250f },
                    { -0.012417f, 0.019287f, 0.026625f },
                    { 0.000755f, 0.052481f, 0.039846f }
                }};
                const std::array<int, 5> blocks { 0, 11, 23, 30, 35 };
                expectEquals (static_cast<int> (checkpoints.size()), 36);

                for (std::size_t i = 0; i < blocks.size(); ++i)
                {
                    const auto& got = checkpoints[static_cast<std::size_t> (blocks[i])];
                    const auto& want = expected[i];
                    expect (std::abs (got[0] - want[0]) <= 1.0e-5f
                                && std::abs (got[1] - want[1]) <= 1.0e-5f
                                && std::abs (got[2] - want[2]) <= 1.0e-5f,
                            juce::String ("manual grain checkpoint at block ") + juce::String (blocks[i])
                                + " actual L=" + juce::String (got[0], 9) + " R=" + juce::String (got[1], 9)
                                + " rms=" + juce::String (got[2], 9));
                }
            }

            beginTest ("a manual grain density spawns at its interval, counted in absolute samples");
            {
                // 10 Hz over two seconds: spawns at 0, 0.1, ... 1.9 s, so exactly 20.
                auto targets = deriveMorphTargets (0.35f);
                targets.grainDurationMs = 20.0f;
                targets.grainDensityHz = 10.0f;
                targets.grainOverlap = 0.0f;

                const auto count = countSpawns (targets, 2.0);
                expectEquals (static_cast<int> (count.starts), 20);
            }

            beginTest ("the legacy cadence is unchanged when density is not manual");
            {
                auto targets = deriveMorphTargets (0.35f);
                const auto spawns = countSpawns (targets, 2.0);
                const auto hop = static_cast<double> (std::max (8, juce::roundToInt (targets.grainDurationMs * 0.001 * grainRate)))
                               * (1.0 - std::min (0.95, static_cast<double> (targets.grainOverlap)));
                const auto expected = static_cast<std::uint64_t> (2.0 * grainRate / std::max (1.0, hop));
                expect (spawns.starts + 2 >= expected && spawns.starts <= expected + 2,
                        "legacy spawn count drifted: " + juce::String (static_cast<int> (spawns.starts)));
            }

            beginTest ("longer manual duration with the MORPH overlap spawns proportionally less often");
            {
                auto shortGrains = deriveMorphTargets (0.6f);
                shortGrains.grainDurationMs = 20.0f;
                auto longGrains = shortGrains;
                longGrains.grainDurationMs = 40.0f;

                const auto shortCount = countSpawns (shortGrains, 2.0).starts;
                const auto longCount = countSpawns (longGrains, 2.0).starts;
                expect (longCount * 2 >= shortCount - 3 && longCount * 2 <= shortCount + 3,
                        "half the spawn rate expected, got " + juce::String (static_cast<int> (longCount))
                        + " against " + juce::String (static_cast<int> (shortCount)));
            }

            beginTest ("oversubscription never exceeds the pool and never steals a grain");
            {
                auto targets = deriveMorphTargets (0.5f);
                targets.grainDurationMs = 500.0f;
                targets.grainDensityHz = 200.0f;
                targets.grainOverlap = 0.0f;

                const auto count = countSpawns (targets, 1.0);
                expect (count.peakActive <= Voice::maxGrains, "grains exceeded the pool");
                expect (count.peakActive == Voice::maxGrains, "the pool should be full under oversubscription");
                expect (count.starts < 200, "spawns beyond capacity must be skipped, not queued");
            }
        }
    };

    static GrainControlTests grainControlTests;
}
