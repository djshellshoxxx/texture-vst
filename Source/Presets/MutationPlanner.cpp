#include "MutationPlanner.h"
#include "../Utility/ParameterIDs.h"

#include <algorithm>
#include <cmath>

namespace texture
{
    namespace
    {
        /** Tiny self-contained generator so results never depend on juce::Random. */
        struct SplitMix64
        {
            std::uint64_t state;

            std::uint64_t next() noexcept
            {
                state += 0x9E3779B97F4A7C15ULL;
                auto z = state;
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                return z ^ (z >> 31);
            }

            /** Uniform in [0, 1) from the high 24 bits. */
            float nextUnit() noexcept
            {
                return static_cast<float> (next() >> 40) * (1.0f / 16777216.0f);
            }
        };

        /** Folds a value back into [0, 1] by reflection at either edge. */
        float reflectIntoUnit (float x) noexcept
        {
            if (x < 0.0f) x = -x;
            if (x > 1.0f) x = 2.0f - x;
            return std::clamp (x, 0.0f, 1.0f);
        }
    }

    const std::vector<MutableParameter>& mutableParameters()
    {
        // Continuous parameters only. Excluded on purpose: calibration (tune, layer roots and
        // fine cents), output gain, reverb switch, LFO sync and division, random mode, voice
        // mode, note priority and the tone filter switch.
        static const std::vector<MutableParameter> table {
            { params::morph,        params::morphMin,       params::morphMax,       false },
            { params::attack,       params::attackMinMs,    params::attackMaxMs,    true },
            { params::decay,        params::decayMinMs,     params::decayMaxMs,     true },
            { params::sustain,      params::sustainMin,     params::sustainMax,     false },
            { params::release,      params::releaseMinMs,   params::releaseMaxMs,   true },
            { params::lfoRate,      params::lfoRateMinHz,   params::lfoRateMaxHz,   true },
            { params::lfoDepth,     params::lfoDepthMin,    params::lfoDepthMax,    false },
            { params::layerBlend,   params::layerBlendMin,  params::layerBlendMax,  false },
            { params::reverse,      params::reverseMin,     params::reverseMax,     false },
            { params::shimmer,      params::shimmerMin,     params::shimmerMax,     false },
            { params::glideMs,      params::glideMinMs,     params::glideMaxMs,     false },
            { params::toneHpHz,     params::toneHpMinHz,    params::toneHpMaxHz,    true },
            { params::toneLpHz,     params::toneLpMinHz,    params::toneLpMaxHz,    true },
            { params::spaceWidth,   params::widthMin,       params::widthMax,       false }
        };

        return table;
    }

    const MutableParameter* findMutableParameter (const juce::String& parameterID)
    {
        for (const auto& parameter : mutableParameters())
            if (parameter.id == parameterID)
                return &parameter;

        return nullptr;
    }

    float toNormalised (const MutableParameter& parameter, float value) noexcept
    {
        const float v = std::clamp (value, parameter.min, parameter.max);

        if (parameter.logarithmic)
            return static_cast<float> (std::log (static_cast<double> (v) / static_cast<double> (parameter.min))
                                     / std::log (static_cast<double> (parameter.max) / static_cast<double> (parameter.min)));

        return (v - parameter.min) / (parameter.max - parameter.min);
    }

    float fromNormalised (const MutableParameter& parameter, float normalised) noexcept
    {
        const float u = std::clamp (normalised, 0.0f, 1.0f);

        float v = 0.0f;

        if (parameter.logarithmic)
            v = static_cast<float> (static_cast<double> (parameter.min)
                                  * std::exp (static_cast<double> (u)
                                              * std::log (static_cast<double> (parameter.max) / static_cast<double> (parameter.min))));
        else
            v = parameter.min + u * (parameter.max - parameter.min);

        return std::clamp (v, parameter.min, parameter.max);
    }

    std::vector<ParamChange> planMutation (const std::map<juce::String, float>& current,
                                           const LockSet& locks,
                                           float amount,
                                           std::uint64_t seed)
    {
        std::vector<ParamChange> changes;

        if (! std::isfinite (amount) || amount <= 0.0f)
            return changes;

        const float strength = std::min (amount, 1.0f);
        SplitMix64 rng { seed };

        for (const auto& parameter : mutableParameters())
        {
            // Two draws per parameter, taken whether or not it is eligible, so locking one
            // control never changes the moves drawn for the others.
            const float first = rng.nextUnit();
            const float second = rng.nextUnit();
            const float delta = (first + second) - 1.0f;   // triangular on [-1, 1]

            if (locks.isLocked (parameter.id))
                continue;

            const auto found = current.find (parameter.id);

            if (found == current.end() || ! std::isfinite (found->second))
                continue;

            const float start = toNormalised (parameter, found->second);
            const float target = reflectIntoUnit (start + maxNormalisedStep * strength * delta);
            const float value = fromNormalised (parameter, target);

            if (! juce::exactlyEqual (value, found->second))
                changes.push_back ({ parameter.id, value });
        }

        return changes;
    }
}
