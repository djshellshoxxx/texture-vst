#include "LockPolicy.h"

#include "../Utility/ParameterIDs.h"

#include <algorithm>

namespace texture
{
    namespace
    {
        // Same order as params::allIDs. Group membership follows the F02 groups:
        // Pitch calibration holds TUNE and source calibration, Layers holds the
        // second-layer blend, Tone/Space holds the reverb send and the output gain.
        constexpr std::array<LockPolicy::FieldInfo, LockPolicy::numFields> registry { {
            { params::morph,           LockGroup::motion,           true,  false },
            { params::attack,          LockGroup::envelope,         true,  false },
            { params::decay,           LockGroup::envelope,         true,  false },
            { params::sustain,         LockGroup::envelope,         true,  false },
            { params::release,         LockGroup::envelope,         true,  false },
            { params::reverbOn,        LockGroup::toneSpace,        true,  false },
            { params::gainDb,          LockGroup::toneSpace,        false, false },
            { params::lfoRate,         LockGroup::motion,           true,  true  },
            { params::lfoDepth,        LockGroup::motion,           true,  true  },
            { params::lfoSync,         LockGroup::motion,           true,  true  },
            { params::lfoDivision,     LockGroup::motion,           true,  true  },
            { params::layerBlend,      LockGroup::layers,           false, true  },
            { params::tune,            LockGroup::pitchCalibration, false, false },
            { params::reverse,         LockGroup::grain,            true,  false },
            { params::shimmer,         LockGroup::grain,            true,  false },
            { params::randomMode,      LockGroup::motion,           false, false },
            { params::layer1Root,      LockGroup::pitchCalibration, false, false },
            { params::layer2Root,      LockGroup::pitchCalibration, false, false },
            { params::layer1FineCents, LockGroup::pitchCalibration, false, false },
            { params::layer2FineCents, LockGroup::pitchCalibration, false, false },
            // F07 tone and space. Lockable as one group; Gentle Mutate leaves them alone,
            // since most of them do nothing audible while Follow MORPH is selected.
            { params::toneSpaceMode,   LockGroup::toneSpace,         false, false },
            { params::toneHpHz,        LockGroup::toneSpace,         false, false },
            { params::toneLpHz,        LockGroup::toneSpace,         false, false },
            { params::toneFilterOn,    LockGroup::toneSpace,         false, false },
            { params::toneTiltDb,      LockGroup::toneSpace,         false, false },
            { params::spaceWidth,      LockGroup::toneSpace,         false, false },
            { params::spaceReverb,     LockGroup::toneSpace,         false, false },
            { params::spaceChorus,     LockGroup::toneSpace,         false, false },
            { params::toneDrive,       LockGroup::toneSpace,         false, false },
            // F11 voice and glide. Not mutable: Gentle Mutate leaves voice mode alone.
            { params::voiceMode,       LockGroup::performance,       false, false },
            { params::notePriority,    LockGroup::performance,       false, false },
            { params::glideMs,         LockGroup::performance,       false, false },
            { params::glideMode,       LockGroup::performance,       false, false },
            // F13 grain controls. Manual values are sound-design values Gentle Mutate may move,
            // but only while their Follow flag is off (see the processor's eligibility check).
            // Follow flags are switches and are never mutated.
            { params::grainSizeMs,         LockGroup::grain,           true,  false },
            { params::grainSizeFollow,     LockGroup::grain,           false, false },
            { params::grainDensityHz,      LockGroup::grain,           true,  false },
            { params::grainDensityFollow,  LockGroup::grain,           false, false },
            { params::grainJitter,         LockGroup::grain,           true,  false },
            { params::grainJitterFollow,   LockGroup::grain,           false, false },
            { params::grainDetuneCents,    LockGroup::grain,           true,  false },
            { params::grainDetuneFollow,   LockGroup::grain,           false, false },
            { params::grainPanSpread,      LockGroup::grain,           true,  false },
            { params::grainPanFollow,      LockGroup::grain,           false, false },
            // F14 modulation shapes. Waveform and retrigger are choices and are not mutated.
            // Phase and slew are continuous and may move under Gentle Mutate. Pro only, like the LFO.
            { params::lfoShape,            LockGroup::motion,          false, true  },
            { params::lfoRetrigger,        LockGroup::motion,          false, true  },
            { params::lfoPhase,            LockGroup::motion,          true,  true  },
            { params::lfoSlewMs,           LockGroup::motion,          true,  true  },
        } };

        static_assert (LockPolicy::numFields == static_cast<std::size_t> (params::numParameters),
                       "Lock registry must cover every registered parameter.");
    }

    const std::array<LockPolicy::FieldInfo, LockPolicy::numFields>& LockPolicy::fields() noexcept
    {
        return registry;
    }

    const LockPolicy::FieldInfo* LockPolicy::findField (std::string_view id) noexcept
    {
        for (const auto& field : registry)
            if (field.id == id)
                return &field;

        return nullptr;
    }

    const char* LockPolicy::groupName (LockGroup group) noexcept
    {
        switch (group)
        {
            case LockGroup::pitchCalibration: return "Pitch calibration";
            case LockGroup::envelope:         return "Envelope";
            case LockGroup::grain:            return "Grain";
            case LockGroup::motion:           return "Motion";
            case LockGroup::toneSpace:        return "Tone/Space";
            case LockGroup::layers:           return "Layers";
            case LockGroup::performance:      return "Performance";
        }

        return "Unknown";
    }

    int LockPolicy::countFieldsIn (LockGroup group) noexcept
    {
        int count = 0;

        for (const auto& field : registry)
            if (field.group == group)
                ++count;

        return count;
    }

    bool LockPolicy::respectsLocks (EditAction action) noexcept
    {
        return action == EditAction::randomize || action == EditAction::mutate;
    }

    bool LockPolicy::isEditable (EditAction action, std::string_view id, const LockSet& locks) noexcept
    {
        if (! respectsLocks (action))
            return true;

        return ! locks.isLocked (id);
    }

    // --- LockSet -------------------------------------------------------------

    bool LockPolicy::LockSet::isLocked (std::string_view id) const noexcept
    {
        return std::binary_search (ids.begin(), ids.end(), id,
                                   [] (const auto& left, const auto& right)
                                   {
                                       return std::string_view (left) < std::string_view (right);
                                   });
    }

    bool LockPolicy::LockSet::setLocked (std::string_view id, bool shouldLock)
    {
        if (! isKnownField (id))
            return false;

        const auto position = std::lower_bound (ids.begin(), ids.end(), id,
                                                [] (const std::string& left, std::string_view right)
                                                {
                                                    return std::string_view (left) < right;
                                                });
        const bool present = position != ids.end() && std::string_view (*position) == id;

        if (shouldLock == present)
            return true;

        if (! shouldLock)
        {
            ids.erase (position);
            return true;
        }

        if (ids.size() >= maxLocks)
            return false;

        ids.insert (position, std::string (id));
        return true;
    }

    void LockPolicy::LockSet::setGroupLocked (LockGroup group, bool shouldLock)
    {
        for (const auto& field : registry)
            if (field.group == group)
                setLocked (field.id, shouldLock);
    }

    int LockPolicy::LockSet::countLocked (LockGroup group) const noexcept
    {
        int count = 0;

        for (const auto& field : registry)
            if (field.group == group && isLocked (field.id))
                ++count;

        return count;
    }

    LockPolicy::GroupState LockPolicy::LockSet::groupState (LockGroup group) const noexcept
    {
        const auto locked = countLocked (group);

        if (locked == 0)
            return GroupState::none;

        return locked == countFieldsIn (group) ? GroupState::all : GroupState::some;
    }

    // --- Parsing -------------------------------------------------------------

    LockPolicy::ParseResult LockPolicy::parseIds (const std::vector<std::string>& rawIds)
    {
        ParseResult result;

        if (rawIds.size() > maxLocks)
        {
            result.accepted = false;
            return result;
        }

        for (const auto& id : rawIds)
        {
            if (id.empty() || id.size() > maxIdLength)
            {
                result.accepted = false;
                return result;
            }
        }

        for (const auto& id : rawIds)
        {
            if (! isKnownField (id))
            {
                result.ignoredIds.push_back (id);
                continue;
            }

            // Duplicates collapse: setLocked is idempotent and the set is bounded by
            // the number of known fields, so it cannot fail here.
            result.locks.setLocked (id, true);
        }

        return result;
    }

    // --- LockHistory ---------------------------------------------------------

    void LockHistory::record (const LockPolicy::LockSet& before)
    {
        steps.push_back (before);

        if (steps.size() > maxDepth)
            steps.erase (steps.begin());
    }

    bool LockHistory::undo (LockPolicy::LockSet& restored)
    {
        if (steps.empty())
            return false;

        restored = steps.back();
        steps.pop_back();
        return true;
    }
}
