#include "TestHelpers.h"

#include "../Source/Engine/GrainRenderController.h"
#include "../Source/Engine/GranularEngine.h"
#include "../Source/Engine/GrainTargetResolver.h"
#include "../Source/Modulation/PersonalityMap.h"
#include "../Source/Presets/LockPolicy.h"

#include <cmath>
#include <vector>

// F16 MORPH personalities: the endpoints, the intensity blend, the discrete Pulse regions, the
// 20 ms style crossfade, the overrides that sit above the mapping, and the rules that connect the
// controls to presets, locks and Gentle Mutate.

namespace texture::tests
{
    namespace
    {
        constexpr double personalityRate = 48000.0;
        constexpr int personalityBlock = 512;

        bool sameTargets (const MorphTargets& a, const MorphTargets& b)
        {
            return juce::exactlyEqual (a.grainDurationMs, b.grainDurationMs)
                && juce::exactlyEqual (a.positionJitter, b.positionJitter)
                && juce::exactlyEqual (a.detuneCents, b.detuneCents)
                && juce::exactlyEqual (a.panSpread, b.panSpread)
                && juce::exactlyEqual (a.reverseChance, b.reverseChance)
                && juce::exactlyEqual (a.grainOverlap, b.grainOverlap)
                && juce::exactlyEqual (a.grainDensityHz, b.grainDensityHz);
        }

        TransportSpan transportNow()
        {
            TransportSpan transport;
            transport.bpm = 120.0;
            return transport;
        }
    }

    class PersonalityMapTests : public juce::UnitTest
    {
    public:
        PersonalityMapTests()
            : juce::UnitTest ("MORPH personalities", "texture-personality") {}

        void runTest() override
        {
            beginTest ("the legacy style and zero intensity return the MORPH mapping exactly, at every position");
            {
                for (int step = 0; step <= 1000; ++step)
                {
                    const auto morph = static_cast<float> (step) / 1000.0f;
                    const auto legacy = deriveMorphTargets (morph);

                    for (int style = 0; style < params::personalityCount; ++style)
                    {
                        auto targets = legacy;
                        const auto mapped = PersonalityMap::map (morph, static_cast<Personality> (style), 0.0f, legacy, 0.5f);
                        PersonalityMap::apply (targets, mapped);
                        expect (mapped.identity, "zero intensity must be the identity");
                        expect (sameTargets (targets, legacy), "zero intensity changed a target");
                    }

                    auto targets = legacy;
                    const auto legacyMapped = PersonalityMap::map (morph, Personality::legacy, 1.0f, legacy, 0.5f);
                    PersonalityMap::apply (targets, legacyMapped);
                    expect (sameTargets (targets, legacy), "the legacy style changed a target at intensity 1");
                }
            }

            beginTest ("every style stays finite and inside its bounds at 1001 positions and five intensities");
            {
                bool finite = true;
                bool bounded = true;

                for (int style = 0; style < params::personalityCount; ++style)
                {
                    for (const auto intensity : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                    {
                        for (int step = 0; step <= 1000; ++step)
                        {
                            const auto morph = static_cast<float> (step) / 1000.0f;
                            const auto legacy = deriveMorphTargets (morph);
                            const auto mapped = PersonalityMap::map (morph, static_cast<Personality> (style),
                                                                     intensity, legacy, 0.5f);
                            const float values[] { mapped.grainDurationMs, mapped.positionJitter, mapped.detuneCents,
                                                   mapped.panSpread, mapped.reverseOffset, mapped.pulseGate };

                            for (const auto value : values)
                                finite = finite && std::isfinite (value);

                            bounded = bounded && mapped.grainDurationMs >= 5.0f && mapped.grainDurationMs <= 500.0f;
                            bounded = bounded && mapped.positionJitter >= 0.0f && mapped.positionJitter <= 0.4f;
                            bounded = bounded && mapped.detuneCents >= 0.0f && mapped.detuneCents <= 100.0f;
                            bounded = bounded && mapped.panSpread >= 0.0f && mapped.panSpread <= 1.0f;
                            bounded = bounded && mapped.reverseOffset >= 0.0f && mapped.reverseOffset <= 0.5f;
                            bounded = bounded && mapped.pulseGate >= 0.05f && mapped.pulseGate <= 1.0f;
                            bounded = bounded && mapped.pulseDivision >= 0 && mapped.pulseDivision <= 3;
                        }
                    }
                }

                expect (finite, "a personality produced a non-finite target");
                expect (bounded, "a personality target left its bounds");
            }

            beginTest ("the Cloud, Fracture and Swarm endpoints match the specification at intensity 1");
            {
                const auto at = [] (Personality style, float morph)
                {
                    return PersonalityMap::map (morph, style, 1.0f, deriveMorphTargets (morph), 0.5f);
                };

                expectWithinAbsoluteError (at (Personality::cloud, 0.0f).grainDurationMs, 30.0f, 1.0e-3f);
                expectWithinAbsoluteError (at (Personality::cloud, 1.0f).grainDurationMs, 250.0f, 1.0e-3f);
                expectWithinAbsoluteError (at (Personality::cloud, 1.0f).positionJitter, 0.15f, 1.0e-6f);
                expectWithinAbsoluteError (at (Personality::cloud, 1.0f).detuneCents, 15.0f, 1.0e-4f);
                expectWithinAbsoluteError (at (Personality::cloud, 1.0f).panSpread, 0.8f, 1.0e-6f);

                expectWithinAbsoluteError (at (Personality::fracture, 0.0f).grainDurationMs, 60.0f, 1.0e-3f);
                expectWithinAbsoluteError (at (Personality::fracture, 1.0f).grainDurationMs, 8.0f, 1.0e-3f);
                expectWithinAbsoluteError (at (Personality::fracture, 1.0f).reverseOffset, 0.5f, 1.0e-6f);
                expectWithinAbsoluteError (at (Personality::fracture, 1.0f).positionJitter, 0.4f, 1.0e-6f);
                expectWithinAbsoluteError (at (Personality::fracture, 1.0f).panSpread, 0.7f, 1.0e-6f);

                expectWithinAbsoluteError (at (Personality::swarm, 0.0f).grainDurationMs, 40.0f, 1.0e-3f);
                expectWithinAbsoluteError (at (Personality::swarm, 1.0f).grainDurationMs, 100.0f, 1.0e-3f);
                expectWithinAbsoluteError (at (Personality::swarm, 1.0f).detuneCents, 40.0f, 1.0e-4f);
                expectWithinAbsoluteError (at (Personality::swarm, 1.0f).panSpread, 1.0f, 1.0e-6f);
            }

            beginTest ("intensity blends linearly from the MORPH mapping toward the style");
            {
                const auto legacy = deriveMorphTargets (0.0f);
                const auto full = PersonalityMap::map (0.0f, Personality::cloud, 1.0f, legacy, 0.5f);
                const auto half = PersonalityMap::map (0.0f, Personality::cloud, 0.5f, legacy, 0.5f);
                expectWithinAbsoluteError (half.grainDurationMs,
                                           0.5f * (legacy.grainDurationMs + full.grainDurationMs), 1.0e-3f);
                expectWithinAbsoluteError (half.positionJitter,
                                           0.5f * (legacy.positionJitter + full.positionJitter), 1.0e-6f);
            }

            beginTest ("Pulse picks one of four labelled regions and engages the grid only above zero intensity");
            {
                const auto legacy = deriveMorphTargets (0.0f);
                const auto divisionAt = [&legacy] (float morph)
                {
                    return PersonalityMap::map (morph, Personality::pulse, 1.0f, legacy, 0.5f).pulseDivision;
                };

                expectEquals (divisionAt (0.10f), 0);   // 1/4
                expectEquals (divisionAt (0.30f), 1);   // 1/8
                expectEquals (divisionAt (0.60f), 2);   // 1/16
                expectEquals (divisionAt (0.90f), 3);   // 1/32
                expectEquals (divisionAt (1.00f), 3);

                expect (PersonalityMap::map (0.5f, Personality::pulse, 1.0f, legacy, 0.5f).pulse, "Pulse should engage the grid");
                expect (! PersonalityMap::map (0.5f, Personality::pulse, 0.0f, legacy, 0.5f).pulse,
                        "Pulse at zero intensity must not engage the grid");
                expect (! PersonalityMap::map (0.5f, Personality::cloud, 1.0f, legacy, 0.5f).pulse,
                        "only Pulse engages the grid");
            }

            beginTest ("the Pulse gate runs from 0.9 to 0.3 and blends with the user's gate by intensity");
            {
                const auto legacy = deriveMorphTargets (0.0f);
                expectWithinAbsoluteError (PersonalityMap::map (0.0f, Personality::pulse, 1.0f, legacy, 0.2f).pulseGate,
                                           0.9f, 1.0e-6f);
                expectWithinAbsoluteError (PersonalityMap::map (1.0f, Personality::pulse, 1.0f, legacy, 0.2f).pulseGate,
                                           0.3f, 1.0e-6f);
                expectWithinAbsoluteError (PersonalityMap::map (1.0f, Personality::pulse, 0.5f, legacy, 0.2f).pulseGate,
                                           0.25f, 1.0e-6f);
            }

            beginTest ("the reverse offset adds to the user's chance and clamps, without replacing the parameter");
            {
                auto targets = deriveMorphTargets (1.0f);
                targets.reverseChance = 0.2f;
                PersonalityMap::apply (targets, PersonalityMap::map (1.0f, Personality::fracture, 1.0f,
                                                                     deriveMorphTargets (1.0f), 0.5f));
                expectWithinAbsoluteError (targets.reverseChance, 0.7f, 1.0e-6f);

                targets.reverseChance = 0.8f;
                PersonalityMap::apply (targets, PersonalityMap::map (1.0f, Personality::fracture, 1.0f,
                                                                     deriveMorphTargets (1.0f), 0.5f));
                expectWithinAbsoluteError (targets.reverseChance, 1.0f, 1.0e-6f);
            }

            beginTest ("explicit grain sizes beat the personality");
            {
                auto targets = deriveMorphTargets (0.5f);
                PersonalityMap::apply (targets, PersonalityMap::map (0.5f, Personality::cloud, 1.0f,
                                                                     deriveMorphTargets (0.5f), 0.5f));

                GrainControlSettings settings;
                settings.values[static_cast<std::size_t> (GrainField::size)] = 42.0f;
                settings.follow[static_cast<std::size_t> (GrainField::size)] = false;

                const auto resolved = GrainTargetResolver::resolve (targets, settings);
                expectWithinAbsoluteError (resolved.targets.grainDurationMs, 42.0f, 1.0e-4f);
            }

            beginTest ("a style change crossfades over 20 ms and the scheduler holds its choice for that time");
            {
                PersonalityRamp ramp;
                ramp.prepare (personalityRate);
                const auto legacy = deriveMorphTargets (0.6f);

                const auto before = ramp.advance (0.6f, Personality::legacy, 1.0f, legacy, 0.5f, personalityBlock);
                expect (before.identity, "the legacy style must start as the identity");

                // Switch to Pulse: the first block is partway through the crossfade.
                auto first = ramp.advance (0.6f, Personality::pulse, 1.0f, legacy, 0.5f, personalityBlock);
                expect (ramp.isCrossfading(), "a style change must start a crossfade");
                expect (! first.pulse, "the grid must not engage during the first 20 ms");

                // Run past 20 ms and the target style is in full force, with the grid on.
                PersonalityTargets settled;
                for (int b = 0; b < 4; ++b)
                    settled = ramp.advance (0.6f, Personality::pulse, 1.0f, legacy, 0.5f, personalityBlock);

                expect (! ramp.isCrossfading(), "the crossfade should be complete after 20 ms");
                expect (settled.pulse, "the grid should engage once the crossfade is complete");

                const auto target = PersonalityMap::map (0.6f, Personality::pulse, 1.0f, legacy, 0.5f);
                expectWithinAbsoluteError (settled.positionJitter, target.positionJitter, 1.0e-6f);
            }

            beginTest ("a crossfade changes continuous targets smoothly, without a jump between blocks");
            {
                PersonalityRamp ramp;
                ramp.prepare (personalityRate);
                const auto legacy = deriveMorphTargets (0.5f);

                ramp.advance (0.5f, Personality::legacy, 1.0f, legacy, 0.5f, personalityBlock);
                float previous = legacy.grainDurationMs;
                float largestStep = 0.0f;

                for (int b = 0; b < 8; ++b)
                {
                    const auto mapped = ramp.advance (0.5f, Personality::cloud, 1.0f, legacy, 0.5f, personalityBlock);
                    largestStep = juce::jmax (largestStep, std::abs (mapped.grainDurationMs - previous));
                    previous = mapped.grainDurationMs;
                }

                // The whole change is about 150 ms of duration over 20 ms: no block may take more than the full range.
                expect (largestStep < 150.0f, "a crossfade block jumped " + juce::String (largestStep) + " ms");
            }

            beginTest ("the personality parameters follow Gentle Mutate's rules");
            {
                expect (! isPersonalityParameterMutable (params::morphPersonality, 2), "the style is never mutated");
                expect (! isPersonalityParameterMutable (params::personalityIntensity, 0), "intensity moved while Legacy");
                expect (isPersonalityParameterMutable (params::personalityIntensity, 3), "intensity not mutable under Fracture");
                expect (isPersonalityParameterMutable (params::morph, 0), "non-personality controls are not gated");
            }

            beginTest ("the personality indices map to styles, and out-of-range values are Legacy");
            {
                expect (personality::fromIndex (0) == Personality::legacy, "index 0");
                expect (personality::fromIndex (2) == Personality::pulse, "index 2");
                expect (personality::fromIndex (4) == Personality::swarm, "index 4");
                expect (personality::fromIndex (5) == Personality::legacy, "index 5 must be Legacy");
                expect (personality::fromIndex (-1) == Personality::legacy, "index -1 must be Legacy");
            }

            beginTest ("presets accept personality values in range and refuse the rest");
            {
                expect (PresetManager::isValidParameterValue (params::morphPersonality, 4.0), "swarm refused");
                expect (! PresetManager::isValidParameterValue (params::morphPersonality, 5.0), "index 5 accepted");
                expect (! PresetManager::isValidParameterValue (params::morphPersonality, 1.5), "fractional style accepted");
                expect (PresetManager::isValidParameterValue (params::personalityIntensity, 1.0), "intensity 1 refused");
                expect (! PresetManager::isValidParameterValue (params::personalityIntensity, 1.1), "intensity above 1 accepted");
            }

            beginTest ("the controller applies the personality to targets and engages the grid for Pulse");
            {
                GrainRenderController controller;
                controller.prepare (personalityRate);

                GrainRenderInputs inputs;
                inputs.morph = 0.6f;
                inputs.personality = Personality::pulse;
                inputs.personalityIntensity = 1.0f;
                inputs.rhythm.rhythmic = false;   // Legacy clock: Pulse still engages the grid

                auto targets = deriveMorphTargets (0.6f);
                const auto legacyJitter = targets.positionJitter;

                // Pulse holds the previous (legacy) choice for 20 ms, so take a few blocks first.
                for (int b = 0; b < 4; ++b)
                    controller.beginBlock (inputs, targets, true, transportNow(), personalityBlock);

                expect (controller.isRhythmic(), "Pulse must engage the grid on a Legacy clock once settled");
                expect (! juce::exactlyEqual (targets.positionJitter, legacyJitter), "the style did not reach the targets");
            }

            beginTest ("the controller's render equals a hand-applied mapping, sample for sample");
            {
                const auto sample = makeTestSample (personalityRate, 2.0, 220.0);

                GrainRenderInputs inputs;
                inputs.morph = 0.6f;
                inputs.personality = Personality::swarm;
                inputs.personalityIntensity = 1.0f;

                // Settle the crossfade first, without rendering, so both paths start from the same
                // steady targets. The crossfade runs from Legacy, so this takes four blocks.
                GrainRenderController controller;
                controller.prepare (personalityRate);
                for (int warm = 0; warm < 4; ++warm)
                {
                    auto warmTargets = deriveMorphTargets (0.6f);
                    controller.beginBlock (inputs, warmTargets, true, transportNow(), personalityBlock);
                }

                GranularEngine viaController;
                viaController.prepare (personalityRate, personalityBlock);
                viaController.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);
                viaController.noteOn (1, 60, 0.8f);

                GranularEngine byHand;
                byHand.prepare (personalityRate, personalityBlock);
                byHand.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);
                byHand.noteOn (1, 60, 0.8f);

                juce::AudioBuffer<float> a (2, personalityBlock);
                juce::AudioBuffer<float> b (2, personalityBlock);
                bool identical = true;

                for (int blockIndex = 0; blockIndex < 40; ++blockIndex)
                {
                    auto targetsA = deriveMorphTargets (0.6f);
                    controller.beginBlock (inputs, targetsA, true, transportNow(), personalityBlock);
                    a.clear();
                    viaController.renderNextBlock (sample, targetsA, a, 0, personalityBlock,
                                                   &controller.renderSpan (0, personalityBlock));

                    auto targetsB = deriveMorphTargets (0.6f);
                    PersonalityMap::apply (targetsB, PersonalityMap::map (0.6f, Personality::swarm, 1.0f,
                                                                          deriveMorphTargets (0.6f), 0.5f));
                    b.clear();
                    byHand.renderNextBlock (sample, targetsB, b, 0, personalityBlock);

                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < personalityBlock; ++i)
                            identical = identical && juce::exactlyEqual (a.getSample (ch, i), b.getSample (ch, i));
                }

                expect (identical, "the controller's render differs from the hand-applied mapping");
            }

            beginTest ("default inputs render bit-identically through the controller and the original path");
            {
                const auto sample = makeTestSample (personalityRate, 2.0, 220.0);

                GrainRenderController controller;
                controller.prepare (personalityRate);

                GranularEngine viaController;
                viaController.prepare (personalityRate, personalityBlock);
                viaController.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);
                viaController.noteOn (1, 60, 0.8f);
                viaController.noteOn (1, 64, 0.7f);

                GranularEngine original;
                original.prepare (personalityRate, personalityBlock);
                original.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);
                original.noteOn (1, 60, 0.8f);
                original.noteOn (1, 64, 0.7f);

                GrainRenderInputs defaults;
                defaults.morph = 0.35f;

                juce::AudioBuffer<float> a (2, personalityBlock);
                juce::AudioBuffer<float> b (2, personalityBlock);
                bool identical = true;

                for (int blockIndex = 0; blockIndex < 60; ++blockIndex)
                {
                    auto targetsA = deriveMorphTargets (0.35f);
                    controller.beginBlock (defaults, targetsA, true, transportNow(), personalityBlock);
                    a.clear();
                    viaController.renderNextBlock (sample, targetsA, a, 0, personalityBlock,
                                                   &controller.renderSpan (0, personalityBlock));

                    const auto targetsB = deriveMorphTargets (0.35f);
                    b.clear();
                    original.renderNextBlock (sample, targetsB, b, 0, personalityBlock);

                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < personalityBlock; ++i)
                            identical = identical && juce::exactlyEqual (a.getSample (ch, i), b.getSample (ch, i));
                }

                expect (identical, "default inputs changed the rendered audio");
            }

            beginTest ("the default inputs leave the controller's targets and grid exactly as before");
            {
                GrainRenderController controller;
                controller.prepare (personalityRate);

                GrainRenderInputs inputs;
                inputs.morph = 0.37f;

                auto targets = deriveMorphTargets (0.37f);
                const auto before = targets;
                controller.beginBlock (inputs, targets, true, transportNow(), personalityBlock);

                expect (sameTargets (targets, before), "default personality changed the targets");
                expect (! controller.isRhythmic(), "default inputs engaged the grid");
            }
        }
    };

    static PersonalityMapTests personalityMapTests;
}
