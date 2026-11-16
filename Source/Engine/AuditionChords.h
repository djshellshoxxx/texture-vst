#pragma once

#include <array>

namespace texture
{
    /** Chord shapes shared by audition (F01) and chord/phrase export (F06).

        Index 0 is a single note. Offsets are semitones above the root; -1 marks an
        unused slot. The values are part of the command encoding, so do not reorder. */
    struct AuditionChords final
    {
        static constexpr int numShapes = 4;
        static constexpr int maxNotes = 4;

        static constexpr std::array<std::array<int, maxNotes>, numShapes> offsets {{
            {{ 0, -1, -1, -1 }},
            {{ 0, 4, 7, -1 }},
            {{ 0, 3, 7, -1 }},
            {{ 0, 7, -1, -1 }}
        }};

        static constexpr bool isValidShape (int shape) noexcept
        {
            return shape >= 0 && shape < numShapes;
        }

        /** Number of sounding notes in a shape. */
        static constexpr int noteCount (int shape) noexcept
        {
            int count = 0;

            for (const auto offset : offsets[static_cast<size_t> (shape)])
                if (offset >= 0)
                    ++count;

            return count;
        }
    };
}
