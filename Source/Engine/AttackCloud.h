#pragma once

#include "../Utility/ParameterIDs.h"

#include <cmath>
#include <string_view>

namespace texture
{
    /** Attack-to-cloud settings (F17). Off by default, so the legacy path is untouched. */
    struct AttackCloudSettings
    {
        bool on { false };
        float holdMs { params::attackCloudHoldDefaultMs };
        float transitionMs { params::attackCloudTransitionDefaultMs };
        float original { params::attackCloudOriginalDefault };
    };

    /** The gain plan for one note's attack-to-cloud crossfade (F17). Pure.

        A direct read of the sample's start plays for `hold` at the requested amount. Then a
        smoothstep crossfade over `transition` moves from the direct read to the granular cloud:
        direct = (1 - t) x amount, cloud = t. Times are latched at note-on, in samples, so a later
        edit cannot move a crossfade already in progress. Gains are linear, not equal-power,
        because the two paths are strongly correlated (they read the same sample).
    */
    class AttackCloudPlan
    {
    public:
        /** A direct read that reaches the end of its sample fades out over this many frames. */
        static constexpr int endFadeFrames = 256;

        /** Smoothstep on [0, 1]: zero slope at both ends, so no click at either edge. */
        static float smoothstep (float u) noexcept
        {
            const auto x = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
            return x * x * (3.0f - 2.0f * x);
        }

        void latch (const AttackCloudSettings& settings, double sampleRate) noexcept
        {
            const auto rate = (sampleRate > 0.0 && std::isfinite (sampleRate)) ? sampleRate : 44100.0;
            const auto clampMs = [] (float value, float low, float high)
            {
                if (! std::isfinite (value))
                    return low;

                return value < low ? low : (value > high ? high : value);
            };

            on = settings.on;
            holdSamples = juce::roundToInt (static_cast<double> (clampMs (settings.holdMs,
                                                  params::attackCloudHoldMinMs, params::attackCloudHoldMaxMs)) * 0.001 * rate);
            transitionSamples = juce::jmax (1, juce::roundToInt (static_cast<double> (clampMs (settings.transitionMs,
                                                  params::attackCloudTransitionMinMs, params::attackCloudTransitionMaxMs)) * 0.001 * rate));
        }

        bool isOn() const noexcept { return on; }
        int getHoldSamples() const noexcept { return holdSamples; }
        int getTransitionSamples() const noexcept { return transitionSamples; }

        /** Samples from the note-on until the crossfade completes. */
        int getTotalSamples() const noexcept { return holdSamples + transitionSamples; }

        /** Gains at an elapsed sample count since the note-on, before the amount and end fade.
            The direct gain is zero once the crossfade has finished. */
        void gainsAt (int elapsedSamples, float& cloud, float& direct) const noexcept
        {
            if (elapsedSamples < holdSamples)
            {
                cloud = 0.0f;
                direct = 1.0f;
                return;
            }

            const auto into = elapsedSamples - holdSamples;

            if (into >= transitionSamples)
            {
                cloud = 1.0f;
                direct = 0.0f;
                return;
            }

            const auto t = smoothstep (static_cast<float> (into) / static_cast<float> (transitionSamples));
            cloud = t;
            direct = 1.0f - t;
        }

    private:
        bool on { false };
        int holdSamples { 0 };
        int transitionSamples { 1 };
    };

    /** Whether Gentle Mutate may move an attack-to-cloud control now. The switch is never mutated.
        The times and amount move only while the feature is on, since otherwise they change nothing. */
    inline bool isAttackCloudParameterMutable (std::string_view id, bool on) noexcept
    {
        if (id == params::attackCloudOn)
            return false;

        if (id == params::attackCloudHoldMs || id == params::attackCloudTransitionMs || id == params::attackCloudOriginal)
            return on;

        return true;   // not an attack-to-cloud control: this gate does not apply
    }
}
