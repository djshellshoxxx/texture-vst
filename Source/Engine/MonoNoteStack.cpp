#include "MonoNoteStack.h"

namespace texture
{
    namespace
    {
        constexpr auto toIndex (int index) noexcept
        {
            return static_cast<std::size_t> (index);
        }

        /** True when a sits ahead of b under the priority mode. Sequences are unique, so this is a total order. */
        template <typename Entry>
        bool isPreferred (const Entry& a, const Entry& b, MonoNoteStack::Priority priority) noexcept
        {
            if (priority == MonoNoteStack::Priority::high && a.note != b.note)
                return a.note > b.note;

            if (priority == MonoNoteStack::Priority::low && a.note != b.note)
                return a.note < b.note;

            return a.sequence > b.sequence;
        }
    }

    bool MonoNoteStack::noteOn (int channel, int note, int velocity) noexcept
    {
        if (channel < minimumChannel || channel > maximumChannel
            || note < minimumNote || note > maximumNote
            || velocity < 0 || velocity > maximumVelocity)
            return false;

        if (velocity == 0)
        {
            noteOff (channel, note);
            return true;
        }

        const auto existing = indexOf (channel, note);
        if (existing >= 0)
        {
            auto& entry = entries[toIndex (existing)];
            if (entry.presses < maximumPressCount)
                ++entry.presses;

            entry.retained = false;
            entry.velocity = velocity;
            entry.sequence = ++nextSequence;
            return true;
        }

        if (count >= capacity)
        {
            const auto victim = oldestRetained();
            if (victim < 0)
                return false;

            removeAt (victim);
        }

        auto& entry = entries[toIndex (count)];
        entry.channel = channel;
        entry.note = note;
        entry.velocity = velocity;
        entry.sequence = ++nextSequence;
        entry.presses = 1;
        entry.retained = false;
        ++count;
        return true;
    }

    void MonoNoteStack::noteOff (int channel, int note) noexcept
    {
        const auto index = indexOf (channel, note);
        if (index < 0)
            return;

        auto& entry = entries[toIndex (index)];
        if (entry.presses == 0)
            return; // Already released and retained by sustain; nothing to decrement.

        --entry.presses;
        if (entry.presses > 0)
            return;

        if (sustainDown)
            entry.retained = true;
        else
            removeAt (index);
    }

    void MonoNoteStack::setSustain (bool down) noexcept
    {
        sustainDown = down;
        if (down)
            return;

        for (int i = count - 1; i >= 0; --i)
            if (entries[toIndex (i)].presses == 0)
                removeAt (i);
    }

    void MonoNoteStack::clear() noexcept
    {
        count = 0;
        sustainDown = false;
    }

    std::optional<MonoNoteStack::Note> MonoNoteStack::select (Priority priority) const noexcept
    {
        if (count == 0)
            return std::nullopt;

        int best = 0;
        for (int i = 1; i < count; ++i)
            if (isPreferred (entries[toIndex (i)], entries[toIndex (best)], priority))
                best = i;

        const auto& chosen = entries[toIndex (best)];
        return Note { chosen.channel, chosen.note, chosen.velocity };
    }

    bool MonoNoteStack::contains (int channel, int note) const noexcept
    {
        return indexOf (channel, note) >= 0;
    }

    bool MonoNoteStack::isPhysicallyHeld (int channel, int note) const noexcept
    {
        const auto index = indexOf (channel, note);
        return index >= 0 && entries[toIndex (index)].presses > 0;
    }

    int MonoNoteStack::pressCount (int channel, int note) const noexcept
    {
        const auto index = indexOf (channel, note);
        return index >= 0 ? static_cast<int> (entries[toIndex (index)].presses) : 0;
    }

    int MonoNoteStack::indexOf (int channel, int note) const noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& entry = entries[toIndex (i)];
            if (entry.channel == channel && entry.note == note)
                return i;
        }

        return -1;
    }

    int MonoNoteStack::oldestRetained() const noexcept
    {
        int oldest = -1;
        for (int i = 0; i < count; ++i)
        {
            const auto& entry = entries[toIndex (i)];
            if (entry.presses != 0)
                continue;

            if (oldest < 0 || entry.sequence < entries[toIndex (oldest)].sequence)
                oldest = i;
        }

        return oldest;
    }

    void MonoNoteStack::removeAt (int index) noexcept
    {
        const auto last = count - 1;
        if (index != last)
            entries[toIndex (index)] = entries[toIndex (last)];

        entries[toIndex (last)] = Entry {};
        --count;
    }
}
