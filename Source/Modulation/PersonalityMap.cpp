#include "PersonalityMap.h"

#include "../Utility/DspHelpers.h"

#include <cmath>

namespace texture
{
    namespace
    {
        struct Range
        {
            float atMorphZero;
            float atMorphOne;

            float at (float morph) const noexcept { return atMorphZero + (atMorphOne - atMorphZero) * morph; }
        };

        float clampTo (float value, float low, float high) noexcept
        {
            return value < low ? low : (value > high ? high : value);
        }

        // Bounds for the mapped values. Each stays inside the range its control already uses,
        // except jitter, which the Fracture style extends to 0.4 of the buffer.
        constexpr float durationLow = 5.0f;
        constexpr float durationHigh = 500.0f;
        constexpr float jitterHigh = 0.4f;
        constexpr float detuneHigh = 100.0f;
        constexpr float gateLow = 0.05f;
        constexpr float gateHigh = 1.0f;
    }

    namespace personality
    {
        Personality fromIndex (int index) noexcept
        {
            return (index < 0 || index >= params::personalityCount) ? Personality::legacy
                                                                   : static_cast<Personality> (index);
        }
    }

    PersonalityTargets PersonalityMap::map (float morph, Personality style, float intensity,
                                            const MorphTargets& legacy, float userGate) noexcept
    {
        PersonalityTargets result;
        result.grainDurationMs = legacy.grainDurationMs;
        result.positionJitter = legacy.positionJitter;
        result.detuneCents = legacy.detuneCents;
        result.panSpread = legacy.panSpread;
        result.pulseGate = clampTo (userGate, gateLow, gateHigh);

        const auto m = clampTo (std::isfinite (morph) ? morph : 0.0f, 0.0f, 1.0f);
        const auto i = clampTo (std::isfinite (intensity) ? intensity : 0.0f, 0.0f, 1.0f);

        // Legacy ignores intensity; zero intensity is Legacy as well.
        if (style == Personality::legacy || i <= 0.0f)
            return result;

        result.identity = false;

        // Each style's endpoints at intensity 1. Fields a style does not name keep their legacy value.
        const auto blendTo = [i] (float base, float styled) noexcept { return base + i * (styled - base); };

        float duration = legacy.grainDurationMs;
        float jitter = legacy.positionJitter;
        float detune = legacy.detuneCents;
        float pan = legacy.panSpread;

        switch (style)
        {
            case Personality::cloud:
                duration = blendTo (legacy.grainDurationMs, Range { 30.0f, 250.0f }.at (m));
                jitter = blendTo (legacy.positionJitter, Range { 0.0f, 0.15f }.at (m));
                detune = blendTo (legacy.detuneCents, Range { 0.0f, 15.0f }.at (m));
                pan = blendTo (legacy.panSpread, Range { 0.0f, 0.8f }.at (m));
                break;

            case Personality::pulse:
            {
                // Pulse keeps the legacy length (the gate sets it on the grid) and adds the grid.
                jitter = blendTo (legacy.positionJitter, Range { 0.0f, 0.05f }.at (m));
                result.pulse = true;
                const auto region = juce::jmin (personality::pulseRegionCount - 1,
                                                static_cast<int> (std::floor (m * static_cast<float> (personality::pulseRegionCount))));
                result.pulseDivision = region;
                result.pulseGate = clampTo (blendTo (userGate, Range { 0.9f, 0.3f }.at (m)), gateLow, gateHigh);
                break;
            }

            case Personality::fracture:
                duration = blendTo (legacy.grainDurationMs, Range { 60.0f, 8.0f }.at (m));
                jitter = blendTo (legacy.positionJitter, Range { 0.0f, 0.4f }.at (m));
                pan = blendTo (legacy.panSpread, Range { 0.0f, 0.7f }.at (m));
                result.reverseOffset = i * 0.5f * m;
                break;

            case Personality::swarm:
                duration = blendTo (legacy.grainDurationMs, Range { 40.0f, 100.0f }.at (m));
                jitter = blendTo (legacy.positionJitter, Range { 0.0f, 0.3f }.at (m));
                detune = blendTo (legacy.detuneCents, Range { 0.0f, 40.0f }.at (m));
                pan = blendTo (legacy.panSpread, Range { 0.0f, 1.0f }.at (m));
                break;

            case Personality::legacy:
                break;
        }

        result.grainDurationMs = clampTo (duration, durationLow, durationHigh);
        result.positionJitter = clampTo (jitter, 0.0f, jitterHigh);
        result.detuneCents = clampTo (detune, 0.0f, detuneHigh);
        result.panSpread = clampTo (pan, 0.0f, 1.0f);
        return result;
    }

    void PersonalityMap::apply (MorphTargets& targets, const PersonalityTargets& mapped) noexcept
    {
        if (mapped.identity)
            return;

        targets.grainDurationMs = mapped.grainDurationMs;
        targets.positionJitter = mapped.positionJitter;
        targets.detuneCents = mapped.detuneCents;
        targets.panSpread = mapped.panSpread;

        // The reverse offset is added to the user's own chance and clamped, so the saved
        // parameter is never replaced.
        targets.reverseChance = clampTo (targets.reverseChance + mapped.reverseOffset, 0.0f, 1.0f);
    }

    PersonalityTargets PersonalityMap::blend (const PersonalityTargets& a, const PersonalityTargets& b, float t) noexcept
    {
        if (a.identity && b.identity)
            return b;

        const auto mix = [t] (float from, float to) noexcept { return from + t * (to - from); };

        PersonalityTargets result;
        result.identity = false;
        result.grainDurationMs = mix (a.grainDurationMs, b.grainDurationMs);
        result.positionJitter = mix (a.positionJitter, b.positionJitter);
        result.detuneCents = mix (a.detuneCents, b.detuneCents);
        result.panSpread = mix (a.panSpread, b.panSpread);
        result.reverseOffset = mix (a.reverseOffset, b.reverseOffset);
        result.pulseGate = mix (a.pulseGate, b.pulseGate);
        result.pulse = b.pulse;
        result.pulseDivision = b.pulseDivision;
        return result;
    }

    void PersonalityRamp::prepare (double sampleRate) noexcept
    {
        const auto rate = (sampleRate > 0.0 && std::isfinite (sampleRate)) ? sampleRate : 44100.0;
        rampTotal = juce::jmax (1, juce::roundToInt (personality::transitionSeconds * rate));
        reset();
    }

    void PersonalityRamp::reset() noexcept
    {
        current = Personality::legacy;
        previous = Personality::legacy;
        rampRemaining = 0;
        primed = false;
    }

    PersonalityTargets PersonalityRamp::advance (float morph, Personality style, float intensity,
                                                 const MorphTargets& legacy, float userGate, int numSamples) noexcept
    {
        if (! primed)
        {
            current = style;
            previous = style;
            primed = true;
        }
        else if (style != current)
        {
            previous = current;
            current = style;
            rampRemaining = rampTotal;
        }

        const auto target = PersonalityMap::map (morph, current, intensity, legacy, userGate);

        if (rampRemaining <= 0)
            return target;

        const auto from = PersonalityMap::map (morph, previous, intensity, legacy, userGate);
        rampRemaining = juce::jmax (0, rampRemaining - juce::jmax (0, numSamples));

        if (rampRemaining <= 0)
            return target;

        const auto progress = static_cast<float> (rampTotal - rampRemaining) / static_cast<float> (rampTotal);
        auto blended = PersonalityMap::blend (from, target, progress);

        // Scheduler selection is discrete: the previous choice holds until the crossfade ends.
        blended.pulse = from.pulse;
        blended.pulseDivision = from.pulseDivision;
        return blended;
    }

    bool isPersonalityParameterMutable (std::string_view id, int style) noexcept
    {
        if (id == params::morphPersonality)
            return false;

        if (id == params::personalityIntensity)
            return style != static_cast<int> (Personality::legacy);

        return true;   // not a personality control: this gate does not apply
    }
}
