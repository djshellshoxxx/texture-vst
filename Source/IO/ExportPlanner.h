#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace texture
{
    /** Pure timing for offline export (F06). No DSP, file or GUI dependencies.

        Legacy two-hit export keeps its original schedule exactly, so archived renders
        are unchanged. The new modes validate every value and reject a render that would
        exceed the duration limit, rather than cutting it short. */
    struct ExportPlanner final
    {
        enum class Mode
        {
            legacyTwoHit = 0,
            singleNote,
            chord,
            repeatedPhrase
        };

        /** Hard limit for the new modes (F06). Legacy keeps its own 60 s guard. */
        static constexpr double maxDurationSeconds = 120.0;
        static constexpr double legacyMaxSeconds = 60.0;
        static constexpr int maxRepeats = 16;
        static constexpr int maxEvents = maxRepeats * 4 * 2;

        struct Request
        {
            Mode mode { Mode::legacyTwoHit };
            int note { 60 };                   // 0 to 127, C3 by default
            int velocity { 96 };               // 1 to 127
            double holdSeconds { 2.0 };        // 0.1 to 30
            double tailSeconds { 4.0 };        // 0 to 30, after the release has finished
            int repeats { 1 };                 // 1 to 16
            double gapSeconds { 0.5 };         // 0 to 5, between one hit's note-off and the next note-on
            int chordShape { 1 };              // AuditionChords index; 0 is a single note
            double sampleRate { 48000.0 };
        };

        struct NoteEvent
        {
            juce::int64 sample { 0 };
            int note { 60 };
            float velocity { 0.0f };
            bool on { false };
        };

        struct Plan
        {
            bool valid { false };
            juce::String message;
            juce::int64 totalSamples { 0 };
            double durationSeconds { 0.0 };
            /** Sorted by sample; at equal positions note-offs come first. */
            std::vector<NoteEvent> events;
        };

        /** Planning for the new modes. `releaseSeconds` comes from the patch. */
        static Plan plan (const Request& request, double releaseSeconds);

        /** The original two-hit layout. `velocity` is the patch's own 0 to 1 value. */
        static Plan planLegacy (double sampleRate, double holdSeconds, double tailSeconds,
                                double gapSeconds, double releaseSeconds, int note, float velocity);

        static juce::String describe (Mode mode);
    };
}
