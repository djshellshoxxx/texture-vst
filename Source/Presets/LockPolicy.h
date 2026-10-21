#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>

namespace texture
{
    /** Parameter groups a user can lock (F02). Calibration parameters are not a group:
        they are protected from automated changes whatever the locks say.
    */
    enum class LockGroup : std::uint8_t
    {
        envelope,
        morph,
        grain,      // reverse, shimmer
        lfo,
        layerBlend,
        fx,         // reverb_on, gain_db
        tone,       // tone_*, space_width
        play        // voice_mode, note_priority, glide_ms
    };

    /** Every group, in the order used when serialising. */
    inline constexpr LockGroup allLockGroups[] = {
        LockGroup::envelope, LockGroup::morph, LockGroup::grain, LockGroup::lfo,
        LockGroup::layerBlend, LockGroup::fx, LockGroup::tone, LockGroup::play
    };

    /** Canonical lowercase name used in serialised lock lists, e.g. "envelope". */
    juce::String lockGroupName (LockGroup group);

    /** The group a parameter belongs to, or nullopt when no group covers it. */
    std::optional<LockGroup> lockGroupForParameter (const juce::String& parameterID);

    /** Pitch calibration (tune, layer roots and fine cents). Never randomised or mutated. */
    bool isCalibrationParameter (const juce::String& parameterID);

    /** A set of locked groups. Small, copyable and free of JUCE state. */
    class LockSet
    {
    public:
        LockSet() = default;

        void lock (LockGroup group) noexcept;
        void unlock (LockGroup group) noexcept;
        void unlockAll() noexcept;
        bool isGroupLocked (LockGroup group) const noexcept;

        /** True when the parameter's group is locked, or when it is calibration (always protected). */
        bool isLocked (const juce::String& parameterID) const;

        /** Comma-separated canonical names in fixed order, e.g. "envelope,play". */
        juce::String toString() const;

        /** Parses a comma list. Whitespace is trimmed, case is ignored, duplicates collapse
            and unknown names are skipped.
        */
        static LockSet fromString (const juce::String& text);

        bool operator== (const LockSet&) const noexcept = default;

    private:
        std::uint32_t mask = 0;
    };
}
