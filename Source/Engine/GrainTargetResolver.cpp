#include "GrainTargetResolver.h"

#include "Voice.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace texture
{
    namespace
    {
        // MORPH's own maximum for jitter. The manual control is 0..1 and multiplies it, so a
        // Follow-to-manual switch does not change the scale. Pan spread and detune are the
        // same units on both sides, so they need no scale.
        constexpr float jitterScale = 0.3f;

        /** Voice clamps overlap to this value, so the taper and hop agree with it. */
        constexpr float maxOverlap = 0.95f;

        constexpr float poolCapacity = static_cast<float> (Voice::maxGrains);

        constexpr std::array<const char*, numGrainFields> valueIDs {
            params::grainSizeMs, params::grainDensityHz, params::grainJitter,
            params::grainDetuneCents, params::grainPanSpread
        };

        constexpr std::array<const char*, numGrainFields> followIDs {
            params::grainSizeFollow, params::grainDensityFollow, params::grainJitterFollow,
            params::grainDetuneFollow, params::grainPanFollow
        };

        constexpr std::array<float, numGrainFields> minimums {
            params::grainSizeMinMs, params::grainDensityMinHz, params::grainJitterMin,
            params::grainDetuneMinCents, params::grainPanSpreadMin
        };

        constexpr std::array<float, numGrainFields> maximums {
            params::grainSizeMaxMs, params::grainDensityMaxHz, params::grainJitterMax,
            params::grainDetuneMaxCents, params::grainPanSpreadMax
        };

        constexpr std::array<float, numGrainFields> defaults {
            params::grainSizeDefaultMs, params::grainDensityDefaultHz, params::grainJitterDefault,
            params::grainDetuneDefaultCents, params::grainPanSpreadDefault
        };

        constexpr float clampTo (float value, float low, float high) noexcept
        {
            return value < low ? low : (value > high ? high : value);
        }
    }

    const char* grainValueParameterID (GrainField field) noexcept
    {
        return valueIDs[static_cast<std::size_t> (field)];
    }

    const char* grainFollowParameterID (GrainField field) noexcept
    {
        return followIDs[static_cast<std::size_t> (field)];
    }

    GrainControlSettings sanitiseGrainControls (const GrainControlSettings& settings) noexcept
    {
        GrainControlSettings result = settings;

        for (std::size_t i = 0; i < numGrainFields; ++i)
        {
            const auto value = settings.values[i];
            result.values[i] = std::isfinite (value) ? clampTo (value, minimums[i], maximums[i]) : defaults[i];
        }

        return result;
    }

    void GrainTargetResolver::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        rampSamples = juce::jmax (1, juce::roundToInt (rampSeconds * sampleRate));
        reset();
    }

    void GrainTargetResolver::reset() noexcept
    {
        for (auto& ramp : ramps)
            ramp = Ramp {};
    }

    float GrainTargetResolver::stepRamp (Ramp& ramp, float goal, float followValue, bool follows,
                                         int numSamples) noexcept
    {
        // While following, the MORPH value passes through unchanged and the ramp tracks it,
        // so switching to manual starts from exactly where the sound was.
        if (follows)
        {
            ramp.value = followValue;
            ramp.target = followValue;
            ramp.step = 0.0f;
            ramp.remaining = 0;
            ramp.primed = true;
            return followValue;
        }

        if (! ramp.primed)
        {
            ramp.value = goal;
            ramp.target = goal;
            ramp.step = 0.0f;
            ramp.remaining = 0;
            ramp.primed = true;
            return goal;
        }

        // A new goal restarts the ramp from wherever the value is now.
        if (goal < ramp.target || goal > ramp.target)
        {
            ramp.target = goal;
            ramp.remaining = rampSamples;
            ramp.step = (goal - ramp.value) / static_cast<float> (rampSamples);
        }

        if (ramp.remaining > 0)
        {
            const auto used = juce::jmin (juce::jmax (0, numSamples), ramp.remaining);
            ramp.value += ramp.step * static_cast<float> (used);
            ramp.remaining -= used;

            if (ramp.remaining == 0)
                ramp.value = ramp.target;
        }

        return ramp.value;
    }

    namespace
    {
        GrainResolution compose (const MorphTargets& morph,
                                 const GrainControlSettings& settings,
                                 float jitterValue,
                                 float detuneValue,
                                 float panValue) noexcept
        {
            GrainResolution result;
            auto& targets = result.targets;
            targets = morph;

            const auto& follow = settings.follow;
            const auto index = [] (GrainField field) { return static_cast<std::size_t> (field); };

            // Duration. Following keeps the MORPH value exactly.
            const auto sizeMs = follow[index (GrainField::size)] ? morph.grainDurationMs
                                                                 : settings.values[index (GrainField::size)];
            targets.grainDurationMs = sizeMs;

            // Density and overlap. The legacy cadence is hop = duration x (1 - overlap); a manual
            // density replaces the hop with 1 / density and derives the overlap from it.
            const auto morphOverlap = clampTo (morph.grainOverlap, 0.0f, maxOverlap);
            const auto durationSeconds = sizeMs * 0.001f;
            float densityHz = 0.0f;

            if (! follow[index (GrainField::density)])
            {
                densityHz = settings.values[index (GrainField::density)];
                const auto hopSeconds = 1.0f / densityHz;
                targets.grainOverlap = clampTo (1.0f - hopSeconds / durationSeconds, 0.0f, maxOverlap);
                targets.grainDensityHz = densityHz;
            }
            else
            {
                targets.grainOverlap = morph.grainOverlap;
                targets.grainDensityHz = 0.0f;
                densityHz = 1000.0f / (sizeMs * (1.0f - morphOverlap));
            }

            // Position jitter, detune and pan spread. Smoothed inputs arrive already ramped.
            if (follow[index (GrainField::jitter)])
                targets.positionJitter = morph.positionJitter;
            else
                targets.positionJitter = jitterValue * jitterScale;

            if (follow[index (GrainField::detune)])
                targets.detuneCents = morph.detuneCents;
            else
                targets.detuneCents = detuneValue;

            if (follow[index (GrainField::pan)])
                targets.panSpread = morph.panSpread;
            else
                targets.panSpread = panValue;

            result.effective[index (GrainField::size)] = sizeMs;
            result.effective[index (GrainField::density)] = densityHz;
            result.effective[index (GrainField::jitter)] = follow[index (GrainField::jitter)]
                ? morph.positionJitter / jitterScale : jitterValue;
            result.effective[index (GrainField::detune)] = follow[index (GrainField::detune)]
                ? morph.detuneCents : detuneValue;
            result.effective[index (GrainField::pan)] = follow[index (GrainField::pan)]
                ? morph.panSpread : panValue;

            result.concurrency = durationSeconds * densityHz;
            result.oversubscribed = result.concurrency > poolCapacity + oversubscriptionMargin;
            return result;
        }
    }

    GrainResolution GrainTargetResolver::advance (const MorphTargets& morph,
                                                  const GrainControlSettings& settings,
                                                  int numSamples) noexcept
    {
        const auto sanitised = sanitiseGrainControls (settings);
        const auto follow = [&sanitised] (GrainField field)
        {
            return sanitised.follow[static_cast<std::size_t> (field)];
        };

        const auto jitter = stepRamp (ramps[static_cast<std::size_t> (GrainField::jitter)],
                                      sanitised.values[static_cast<std::size_t> (GrainField::jitter)],
                                      morph.positionJitter / jitterScale,
                                      follow (GrainField::jitter), numSamples);
        const auto detune = stepRamp (ramps[static_cast<std::size_t> (GrainField::detune)],
                                      sanitised.values[static_cast<std::size_t> (GrainField::detune)],
                                      morph.detuneCents,
                                      follow (GrainField::detune), numSamples);
        const auto pan = stepRamp (ramps[static_cast<std::size_t> (GrainField::pan)],
                                   sanitised.values[static_cast<std::size_t> (GrainField::pan)],
                                   morph.panSpread,
                                   follow (GrainField::pan), numSamples);

        return compose (morph, sanitised, jitter, detune, pan);
    }

    GrainResolution GrainTargetResolver::resolve (const MorphTargets& morph,
                                                  const GrainControlSettings& settings) noexcept
    {
        const auto sanitised = sanitiseGrainControls (settings);
        const auto manual = [&sanitised] (GrainField field)
        {
            return sanitised.values[static_cast<std::size_t> (field)];
        };

        return compose (morph, sanitised,
                        sanitised.follow[static_cast<std::size_t> (GrainField::jitter)]
                            ? morph.positionJitter / jitterScale : manual (GrainField::jitter),
                        sanitised.follow[static_cast<std::size_t> (GrainField::detune)]
                            ? morph.detuneCents : manual (GrainField::detune),
                        sanitised.follow[static_cast<std::size_t> (GrainField::pan)]
                            ? morph.panSpread : manual (GrainField::pan));
    }
}

namespace texture
{
    GrainFollowChange planGrainFollowChange (GrainField field, bool shouldFollow, float effectiveValue) noexcept
    {
        GrainFollowChange change;
        const auto index = static_cast<std::size_t> (field);

        if (! shouldFollow)
        {
            // A non-finite effective value would be unsafe to store; the range default is used.
            const auto value = std::isfinite (effectiveValue) ? effectiveValue : defaults[index];
            change.writes[0] = { valueIDs[index], clampTo (value, minimums[index], maximums[index]) };
            change.count = 1;
        }

        change.writes[static_cast<std::size_t> (change.count)] = { followIDs[index], shouldFollow ? 1.0f : 0.0f };
        ++change.count;
        return change;
    }

    bool isGrainParameterMutable (std::string_view id, const GrainControlSettings& settings) noexcept
    {

        for (std::size_t i = 0; i < numGrainFields; ++i)
        {
            if (id == valueIDs[i])
                return ! settings.follow[i];

            if (id == followIDs[i])
                return false;
        }

        return true;   // not a grain parameter: this gate does not apply
    }
}
