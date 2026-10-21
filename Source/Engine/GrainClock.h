#pragma once

#include "../Utility/ParameterIDs.h"
#include "../Utility/VersionedRandomStreams.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace texture
{
    /** Settings for the rhythmic grain scheduler (F19). Plain values: the processor reads the
        parameters once per block and the tests build this directly. */
    struct RhythmSettings
    {
        bool rhythmic { false };                                  // Legacy when false
        int division { params::grainDivisionDefault };            // index into grainDivisionNames
        float swing { params::grainSwingDefault };                // 0 .. 0.75 of a grid step
        float probability { params::grainProbabilityDefault };    // chance per grid event
        float gate { params::grainGateDefault };                  // grain length as a fraction of its subinterval
        int burst { params::grainBurstDefault };                  // grains per grid event, subdividing it
        bool firstGrainImmediate { params::grainFirstImmediateDefault };
    };

    /** One grain start, as a sample offset inside the span it was generated for. */
    struct GrainEvent
    {
        int offset { 0 };                 // [0, span length)
        std::int64_t gridIndex { 0 };     // logical grid event: the probability draw is keyed by it
        int subIndex { 0 };               // burst subdivision; 0 is the grid event itself
        int durationSamples { 1 };        // gate-derived length, bounded to the supported grain range
        float intervalSamples { 1.0f };   // subdivided interval the grain sits in
    };

    /** A bounded, preallocated list of grain starts for one span. Overflow is counted and the
        newest event is dropped, so the audio thread never allocates or grows the list. */
    struct GrainEventList
    {
        static constexpr int capacity = 128;

        std::array<GrainEvent, capacity> events {};
        int count { 0 };
        std::uint64_t dropped { 0 };      // diagnostic: events that did not fit

        void clear() noexcept { count = 0; }

        bool push (const GrainEvent& event) noexcept
        {
            if (count >= capacity)
            {
                ++dropped;
                return false;
            }

            events[static_cast<std::size_t> (count++)] = event;
            return true;
        }
    };

    /** Where the host is, at the first sample of a span. */
    struct TransportSpan
    {
        bool hasPlayhead { false };   // false: no host transport, so the clock free-runs
        bool hasPpq { false };        // false: free-run from the internal sample accumulator
        bool playing { false };       // meaningful only when hasPlayhead
        double ppq { 0.0 };           // beats at the span start, when hasPpq
        double bpm { 0.0 };           // non-finite or <= 0 keeps the last valid tempo
    };

    /** What the voices need for one span. `events` is owned by the caller and is valid for the
        duration of the render call. */
    struct RhythmSpan
    {
        bool active { false };                 // rhythmic scheduling is in force for this span
        bool clockRunning { false };           // false while the transport is stopped
        bool gateSetsDuration { true };        // Follow size: the gate sets length; a manual size overrides it (F13)
        bool firstGrainImmediate { true };
        float probability { 1.0f };
        int immediateDurationSamples { 1 };    // gate-derived length of a note's first grain
        float immediateIntervalSamples { 1.0f };
        const GrainEventList* events { nullptr };
    };

    namespace rhythm
    {
        inline constexpr double transitionSeconds = 0.02;     // a division change is held this long
        inline constexpr double minGrainSeconds = 0.005;      // the engine's supported grain range (F13)
        inline constexpr double maxGrainSeconds = 0.5;
        inline constexpr double defaultBpm = 120.0;
        inline constexpr double minBpm = 20.0;
        inline constexpr double maxBpm = 999.0;
        /** Callers render spans of at most this many samples; the event list holds a span. */
        inline constexpr int maxSpanSamples = 512;

        /** Beats in one grid step for a division index. Out-of-range indices use the default. */
        double divisionBeats (int index) noexcept;

        /** True for the rhythmic parameters that Gentle Mutate may move. Each depends on the
            grid being in use, so a change while Legacy is selected would be inaudible. */
        bool isRhythmParameterMutable (std::string_view parameterID, bool rhythmic) noexcept;
    }

    /** Pure grid scheduler (F19). It turns a transport position into sample-accurate grain starts.

        Grid point n sits at n x D beats, and odd points are delayed by swing x D, so each pair of
        steps keeps its total length. Bursts subdivide the interval after a grid point. Positions
        are double beats; offsets are whole samples, rounded up so a start is never early.

        Realignment: after a seek, a loop, a stop or a change of grid, the cursor moves to the first
        start at or after the current position. Nothing before that point is replayed, so there is
        no catch-up burst.

        Free-running: without a host transport the clock advances its own sample accumulator at the
        last valid tempo (120 BPM until the host reports one). A stopped transport creates no grains.
    */
    class GrainClock
    {
    public:
        void prepare (double sampleRate) noexcept;

        /** Clears the cursor and the free-run position. The next span realigns to the grid. */
        void reset() noexcept;

        /** Writes the grain starts in [span start, span start + numSamples) into `out`. Clears
            `out` first. Real-time safe: no allocation, no locks. */
        void generate (const RhythmSettings& settings, const TransportSpan& transport,
                       int numSamples, GrainEventList& out) noexcept;

        /** Unswung grid step in samples at the last tempo, for the first-grain level. */
        double getGridIntervalSamples() const noexcept { return lastGridSamples; }

        /** Tempo the clock is using, for the panel readout. */
        double getTempoBpm() const noexcept { return lastBpm; }

        /** Beat position of grid point n for a grid step D and swing s. Pure, for tests. */
        static double gridPointBeats (std::int64_t n, double stepBeats, double swing) noexcept;

        /** Beats from grid point n to point n + 1. Pure, for tests. */
        static double gridStepBeats (std::int64_t n, double stepBeats, double swing) noexcept;

    private:
        struct Cursor
        {
            std::int64_t n { 0 };
            int k { 0 };
        };

        double spawnBeats (const Cursor& cursor, double stepBeats, double swing, int burst) const noexcept;
        void advance (Cursor& cursor, int burst) const noexcept;
        void realign (double fromBeats, double stepBeats, double swing, int burst) noexcept;

        double sampleRate { 44100.0 };
        double lastBpm { rhythm::defaultBpm };
        double lastGridSamples { 0.0 };
        double freeBeats { 0.0 };
        std::uint64_t sampleCounter { 0 };

        bool cursorValid { false };
        Cursor cursor;
        int activeDivision { params::grainDivisionDefault };
        int pendingDivision { -1 };
        std::uint64_t pendingSinceSample { 0 };
        bool divisionInitialised { false };
        double lastSwing { -1.0 };
        int lastBurst { 0 };
    };
}
