#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace texture
{
    /** Bounded, message-thread-only history of generated sound variations (F09).

        Holds up to `capacity` entries, oldest first. When full, the oldest unpinned
        entry is evicted; pins are limited to `maxPinned`, so a history whose every
        entry is pinned rejects a new variation rather than growing. A cursor walks the
        entries for Previous and Next, and walking adds nothing.

        Pure: no JUCE and no per-add allocation beyond the bounded vector.
    */
    template <typename Payload>
    class VariationHistory
    {
    public:
        static constexpr std::size_t capacity = 32;
        static constexpr std::size_t maxPinned = 8;
        static constexpr std::size_t noCursor = static_cast<std::size_t> (-1);

        struct Entry
        {
            Payload payload;
            std::string operation;
            std::int64_t createdAtMs { 0 };
            std::uint64_t sequence { 0 };
            bool pinned { false };
        };

        enum class AddResult { added, rejectedAllPinned };

        /** Appends a variation and makes it the cursor. When full, evicts the oldest
            unpinned entry first; if every entry is pinned, nothing changes. */
        AddResult add (Payload payload, std::string operation, std::int64_t createdAtMs)
        {
            if (entries.size() >= capacity && ! evictOldestUnpinned())
                return AddResult::rejectedAllPinned;

            Entry entry { std::move (payload), std::move (operation), createdAtMs, ++nextSequence, false };
            entries.push_back (std::move (entry));
            cursor = entries.size() - 1;
            ++revisionCount;
            return AddResult::added;
        }

        /** Removes the oldest entry that is not pinned. Used on overflow and by the media
            budget. Returns false when nothing can be evicted. */
        bool evictOldestUnpinned()
        {
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (entries[i].pinned)
                    continue;

                eraseAt (i);
                return true;
            }

            return false;
        }

        /** Pins or unpins an entry. Refused for an unknown index or a ninth pin. */
        bool setPinned (std::size_t index, bool shouldPin)
        {
            if (index >= entries.size())
                return false;

            auto& entry = entries[index];

            if (entry.pinned == shouldPin)
                return true;

            if (shouldPin && pinnedCount() >= maxPinned)
                return false;

            entry.pinned = shouldPin;
            ++revisionCount;
            return true;
        }

        /** Empties the history and its pins and cursor. */
        void clear()
        {
            entries.clear();
            cursor = noCursor;
            ++revisionCount;
        }

        std::size_t size() const noexcept { return entries.size(); }
        bool empty() const noexcept { return entries.empty(); }
        const Entry& at (std::size_t index) const { return entries[index]; }
        const Entry* find (std::size_t index) const { return index < entries.size() ? &entries[index] : nullptr; }

        std::size_t pinnedCount() const noexcept
        {
            std::size_t count = 0;

            for (const auto& entry : entries)
                count += entry.pinned ? 1u : 0u;

            return count;
        }

        /** Index of the entry the user is looking at, or noCursor. */
        std::size_t getCursor() const noexcept { return cursor; }

        /** Moves to the next newer entry. No wrap, and nothing is added. */
        bool moveNext()
        {
            if (entries.empty())
                return false;

            if (cursor == noCursor)
                cursor = 0;
            else if (cursor + 1 < entries.size())
                ++cursor;
            else
                return false;

            ++revisionCount;
            return true;
        }

        /** Moves to the previous older entry. No wrap, and nothing is added. */
        bool movePrevious()
        {
            if (entries.empty())
                return false;

            if (cursor == noCursor)
                cursor = entries.size() - 1;
            else if (cursor > 0)
                --cursor;
            else
                return false;

            ++revisionCount;
            return true;
        }

        /** Changes every time the list, pins or cursor change. The GUI polls it. */
        std::uint64_t revision() const noexcept { return revisionCount; }

    private:
        void eraseAt (std::size_t index)
        {
            entries.erase (entries.begin() + static_cast<std::ptrdiff_t> (index));

            if (cursor == index)
                cursor = noCursor;
            else if (cursor != noCursor && cursor > index)
                --cursor;

            ++revisionCount;
        }

        std::vector<Entry> entries;
        std::size_t cursor { noCursor };
        std::uint64_t nextSequence { 0 };
        std::uint64_t revisionCount { 0 };
    };
}
