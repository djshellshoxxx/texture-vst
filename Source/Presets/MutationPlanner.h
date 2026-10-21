#pragma once

#include "LockPolicy.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <map>
#include <vector>

namespace texture
{
    struct ParamChange
    {
        juce::String id;
        float value;
    };

    /** A parameter the planner may move, with its real range and mapping. */
    struct MutableParameter
    {
        juce::String id;
        float min;
        float max;
        bool logarithmic;   // musical/log mapping for time and frequency parameters
    };

    /** Every parameter the planner may move, in the fixed order its draws are taken. */
    const std::vector<MutableParameter>& mutableParameters();

    /** Descriptor for one ID, or nullptr when the planner never moves it. */
    const MutableParameter* findMutableParameter (const juce::String& parameterID);

    /** Largest normalised move at strength 1 (12% of the range). */
    inline constexpr float maxNormalisedStep = 0.12f;

    /** Maps a real-world value into [0, 1] using the parameter's mapping. Clamps the input. */
    float toNormalised (const MutableParameter& parameter, float value) noexcept;

    /** Inverse of toNormalised. The result is clamped to the parameter's real range. */
    float fromNormalised (const MutableParameter& parameter, float normalised) noexcept;

    /** Plans a gentle random walk around `current`. Only unlocked, mutable parameters that
        are present in `current` can change; a change is returned only when its value differs.
        Deterministic for a given (current, locks, amount, seed). amount is clamped to [0, 1];
        zero, negative or non-finite amounts return no changes.
    */
    std::vector<ParamChange> planMutation (const std::map<juce::String, float>& current,
                                           const LockSet& locks,
                                           float amount,
                                           std::uint64_t seed);
}
