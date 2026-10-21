#include "LockPolicy.h"
#include "../Utility/ParameterIDs.h"

namespace texture
{
    namespace
    {
        struct ParameterGroupEntry
        {
            const char* parameterID;
            LockGroup group;
        };

        constexpr ParameterGroupEntry parameterGroups[] = {
            { params::attack,        LockGroup::envelope },
            { params::decay,         LockGroup::envelope },
            { params::sustain,       LockGroup::envelope },
            { params::release,       LockGroup::envelope },
            { params::morph,         LockGroup::morph },
            { params::tune,          LockGroup::grain },
            { params::reverse,       LockGroup::grain },
            { params::shimmer,       LockGroup::grain },
            { params::lfoRate,       LockGroup::lfo },
            { params::lfoDepth,      LockGroup::lfo },
            { params::lfoSync,       LockGroup::lfo },
            { params::lfoDivision,   LockGroup::lfo },
            { params::layerBlend,    LockGroup::layerBlend },
            { params::reverbOn,      LockGroup::fx },
            { params::gainDb,        LockGroup::fx },
            { params::toneFilterOn,  LockGroup::tone },
            { params::toneHpHz,      LockGroup::tone },
            { params::toneLpHz,      LockGroup::tone },
            { params::spaceWidth,    LockGroup::tone },
            { params::voiceMode,     LockGroup::play },
            { params::notePriority,  LockGroup::play },
            { params::glideMs,       LockGroup::play }
        };

        struct GroupName
        {
            LockGroup group;
            const char* name;
        };

        constexpr GroupName groupNames[] = {
            { LockGroup::envelope,   "envelope" },
            { LockGroup::morph,      "morph" },
            { LockGroup::grain,      "grain" },
            { LockGroup::lfo,        "lfo" },
            { LockGroup::layerBlend, "layer_blend" },
            { LockGroup::fx,         "fx" },
            { LockGroup::tone,       "tone" },
            { LockGroup::play,       "play" }
        };

        constexpr std::uint32_t bitFor (LockGroup group) noexcept
        {
            return 1u << static_cast<std::uint32_t> (group);
        }
    }

    juce::String lockGroupName (LockGroup group)
    {
        for (const auto& entry : groupNames)
            if (entry.group == group)
                return juce::String (entry.name);

        return {};
    }

    std::optional<LockGroup> lockGroupForParameter (const juce::String& parameterID)
    {
        for (const auto& entry : parameterGroups)
            if (parameterID == entry.parameterID)
                return entry.group;

        return std::nullopt;
    }

    bool isCalibrationParameter (const juce::String& parameterID)
    {
        return parameterID == params::tune
            || parameterID == params::layer1Root
            || parameterID == params::layer2Root
            || parameterID == params::layer1FineCents
            || parameterID == params::layer2FineCents;
    }

    void LockSet::lock (LockGroup group) noexcept     { mask |= bitFor (group); }
    void LockSet::unlock (LockGroup group) noexcept   { mask &= ~bitFor (group); }
    void LockSet::unlockAll() noexcept                { mask = 0; }

    bool LockSet::isGroupLocked (LockGroup group) const noexcept
    {
        return (mask & bitFor (group)) != 0;
    }

    bool LockSet::isLocked (const juce::String& parameterID) const
    {
        if (isCalibrationParameter (parameterID))
            return true;

        const auto group = lockGroupForParameter (parameterID);
        return group.has_value() && isGroupLocked (*group);
    }

    juce::String LockSet::toString() const
    {
        juce::StringArray names;

        for (const auto group : allLockGroups)
            if (isGroupLocked (group))
                names.add (lockGroupName (group));

        return names.joinIntoString (",");
    }

    LockSet LockSet::fromString (const juce::String& text)
    {
        LockSet result;
        const auto tokens = juce::StringArray::fromTokens (text, ",", "");

        for (const auto& token : tokens)
        {
            const auto name = token.trim().toLowerCase();

            for (const auto group : allLockGroups)
                if (name == lockGroupName (group))
                    result.lock (group);
        }

        return result;
    }
}
