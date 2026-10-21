#pragma once

#include <array>
#include <cstdint>

namespace texture
{
    /** Counts keys that are physically held, across all channels (F14, First note mode).

        Sustain does not count: a key that is released under the pedal is no longer held.
        A repeated note-on for a key already held changes nothing, and out-of-range channels
        or notes are ignored. Fixed storage, so it is safe on the audio thread. */
    class HeldNoteCounter final
    {
    public:
        /** Returns true when the total goes from zero to one: the first key of a chord. */
        bool noteOn (int midiChannel, int midiNote) noexcept
        {
            const auto bit = bitIndex (midiChannel, midiNote);

            if (bit < 0)
                return false;

            const auto word = static_cast<std::size_t> (bit / 64);
            const auto mask = std::uint64_t { 1 } << static_cast<unsigned> (bit % 64);

            if ((words[word] & mask) != 0u)
                return false;

            words[word] |= mask;
            ++held;
            return held == 1;
        }

        void noteOff (int midiChannel, int midiNote) noexcept
        {
            const auto bit = bitIndex (midiChannel, midiNote);

            if (bit < 0)
                return;

            const auto word = static_cast<std::size_t> (bit / 64);
            const auto mask = std::uint64_t { 1 } << static_cast<unsigned> (bit % 64);

            if ((words[word] & mask) != 0u)
            {
                words[word] &= ~mask;
                --held;
            }
        }

        void clear() noexcept
        {
            words.fill (0u);
            held = 0;
        }

        /** All-notes-off on one channel: releases that channel's keys only. */
        void clearChannel (int midiChannel) noexcept
        {
            for (int note = 0; note < 128; ++note)
                noteOff (midiChannel, note);
        }

        int count() const noexcept { return held; }

    private:
        static int bitIndex (int midiChannel, int midiNote) noexcept
        {
            if (midiChannel < 1 || midiChannel > 16 || midiNote < 0 || midiNote > 127)
                return -1;

            return (midiChannel - 1) * 128 + midiNote;
        }

        std::array<std::uint64_t, 32> words {};   // 16 channels x 128 notes = 2048 bits
        int held { 0 };
    };
}
