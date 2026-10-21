#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace texture
{
    /** Bounded, allocation-free stack of physically held and sustain-retained notes.

        Keys are (channel, note). Each key carries a press count that saturates at 255
        and never goes below zero: noteOff on a key with no presses is ignored, and a
        saturated key needs 255 noteOffs to release. A key whose count reaches zero is
        kept only while sustain is down, and is removed on pedal-up.

        When the stack is full and a new key arrives, the oldest sustain-retained key is
        evicted to make room. If every slot is physically held, the new note is rejected.

        This is pure event logic with no JUCE dependency and no heap allocation.
    */
    class MonoNoteStack final
    {
    public:
        static constexpr int capacity = 64;
        static constexpr int minimumChannel = 1;
        static constexpr int maximumChannel = 16;
        static constexpr int minimumNote = 0;
        static constexpr int maximumNote = 127;
        static constexpr int maximumVelocity = 127;
        static constexpr std::uint8_t maximumPressCount = 255;

        enum class Priority : std::uint8_t
        {
            last = 0,
            high = 1,
            low = 2
        };

        struct Note
        {
            int channel = 0;
            int note = 0;
            int velocity = 0;
        };

        /** Returns false if the event is out of range or the stack has no room.
            A zero velocity is treated as noteOff and always returns true.
        */
        bool noteOn (int channel, int note, int velocity) noexcept;

        /** Releases one press. While sustain is down the key is retained instead of removed. */
        void noteOff (int channel, int note) noexcept;

        /** Pedal-down retains released keys. Pedal-up removes every retained key. */
        void setSustain (bool down) noexcept;

        /** Drops every key and resets sustain, as for a panic or host recall. */
        void clear() noexcept;

        /** Chooses among held and retained notes. Ties break toward the newest press. */
        std::optional<Note> select (Priority priority) const noexcept;

        int size() const noexcept { return count; }
        bool contains (int channel, int note) const noexcept;
        bool isPhysicallyHeld (int channel, int note) const noexcept;
        int pressCount (int channel, int note) const noexcept;
        bool isSustainDown() const noexcept { return sustainDown; }

    private:
        struct Entry
        {
            int channel = 0;
            int note = 0;
            int velocity = 0;
            std::uint64_t sequence = 0;
            std::uint8_t presses = 0;
            bool retained = false;
        };

        int indexOf (int channel, int note) const noexcept;
        int oldestRetained() const noexcept;
        void removeAt (int index) noexcept;

        std::array<Entry, static_cast<std::size_t> (capacity)> entries{};
        int count = 0;
        std::uint64_t nextSequence = 0;
        bool sustainDown = false;
    };
}
