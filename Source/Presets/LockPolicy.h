#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace texture
{
    /** Editing groups for lock toggles and Gentle Mutate's group checkboxes (F02).
        The values are stable: they are bit positions in UI masks. */
    enum class LockGroup : std::uint8_t
    {
        pitchCalibration = 0,
        envelope,
        grain,
        motion,
        toneSpace,
        layers,
        performance   // F11 voice and glide controls, appended after the original six
    };

    inline constexpr int numLockGroups = 7;

    /** Named user actions. Only Randomize and Gentle Mutate (and, once implemented,
        preset browsing with its preference enabled) consult locks. Automation, MIDI,
        modulation, project restore, undo/redo, explicit edits and Reset ignore them. */
    enum class EditAction : std::uint8_t
    {
        randomize,
        mutate,
        reset,
        automation,
        projectRestore,
        undoRedo,
        explicitEdit
    };

    /** Pure lock model: no JUCE, no processor, no GUI. */
    class LockPolicy final
    {
    public:
        /** Upper bound on the persisted lock list. Larger lists reject the state. */
        static constexpr std::size_t maxLocks = 64;
        static constexpr std::size_t maxIdLength = 64;

        /** Registry of lockable fields, in the same order as params::allIDs. */
        static constexpr std::size_t numFields = 64;

        struct FieldInfo
        {
            std::string_view id;
            LockGroup group;
            /** Gentle Mutate may change this field when its group is selected. */
            bool mutableByDefault;
            /** Capability-gated: Gentle Mutate skips it on the Free tier. */
            bool proOnly;
        };

        static const std::array<FieldInfo, numFields>& fields() noexcept;
        static const FieldInfo* findField (std::string_view id) noexcept;
        static bool isKnownField (std::string_view id) noexcept { return findField (id) != nullptr; }
        static const char* groupName (LockGroup group) noexcept;
        static int countFieldsIn (LockGroup group) noexcept;

        static bool respectsLocks (EditAction action) noexcept;
        enum class GroupState : std::uint8_t { none, some, all };

        /** An ordered, duplicate-free set of locked field IDs. Message thread only. */
        class LockSet final
        {
        public:
            bool isLocked (std::string_view id) const noexcept;

            /** False when the ID is unknown or the set is full. */
            bool setLocked (std::string_view id, bool shouldLock);

            /** Locks or unlocks every field in the group. */
            void setGroupLocked (LockGroup group, bool shouldLock);

            GroupState groupState (LockGroup group) const noexcept;
            int countLocked (LockGroup group) const noexcept;
            int countLockedTotal() const noexcept { return static_cast<int> (ids.size()); }

            void clear() noexcept { ids.clear(); }
            bool empty() const noexcept { return ids.empty(); }
            const std::vector<std::string>& lockedIds() const noexcept { return ids; }

            bool operator== (const LockSet& other) const noexcept { return ids == other.ids; }
            bool operator!= (const LockSet& other) const noexcept { return ! (*this == other); }

        private:
            std::vector<std::string> ids; // sorted, unique, all known
        };

        /** Parses a persisted list. Unknown IDs are dropped and reported; duplicates
            collapse. `accepted` is false for an oversized list or an empty/overlong ID,
            which must reject the whole proposed state. */
        struct ParseResult
        {
            bool accepted { true };
            LockSet locks;
            std::vector<std::string> ignoredIds;
        };

        static ParseResult parseIds (const std::vector<std::string>& rawIds);

        /** True when the action may change this field, given the locks captured for it. */
        static bool isEditable (EditAction action, std::string_view id, const LockSet& locks) noexcept;
    };

    /** Bounded undo for lock toggles. Separate from sound undo (F02 "editor undo state"). */
    class LockHistory final
    {
    public:
        static constexpr std::size_t maxDepth = 32;

        /** Records the lock set as it was before a change. */
        void record (const LockPolicy::LockSet& before);
        bool canUndo() const noexcept { return ! steps.empty(); }
        bool undo (LockPolicy::LockSet& restored);
        void clear() noexcept { steps.clear(); }

    private:
        std::vector<LockPolicy::LockSet> steps;
    };
}
