#pragma once

#include "GrainClock.h"

namespace texture
{
    /** Per-block composition of the grain-level features. The live processor and the offline
        renderer both use it, so a patch schedules and shapes its grains the same way in each.

        Pure: no JUCE audio objects, no processor, no allocation after construction. Call
        prepare() before use. Each block calls beginBlock() once, then renderSpan() for
        contiguous spans that cover the block in order.
    */
    class GrainRenderController
    {
    public:
        void prepare (double sampleRate) noexcept;

        /** Clears the scheduler. Called on prepare, on All Sound Off and on reset. */
        void reset() noexcept;

        /** Once per block. `gateSetsDuration` is true when the grain size follows MORPH (F13);
            a manual size overrides the gate. The transport is the position at the block start. */
        void beginBlock (const RhythmSettings& rhythmSettings, bool gateSetsDuration,
                         const TransportSpan& transport, int numSamples) noexcept;

        /** The schedule for `length` samples starting `offset` samples into the current block.
            The returned span is valid until the next call. */
        const RhythmSpan& renderSpan (int offset, int length) noexcept;

        bool isRhythmic() const noexcept { return rhythm.rhythmic; }

        /** Tempo the scheduler is using, for the panel readout. */
        double getTempoBpm() const noexcept { return clock.getTempoBpm(); }

        /** Grain starts the scheduler dropped because the event list was full. Diagnostic. */
        std::uint64_t getDroppedEventCount() const noexcept { return events.dropped; }

    private:
        GrainClock clock;
        RhythmSettings rhythm;
        bool gateSetsDuration { true };
        TransportSpan blockTransport;
        double sampleRate { 44100.0 };
        double blockTempoBpm { rhythm::defaultBpm };
        GrainEventList events;
        RhythmSpan span;
    };
}
