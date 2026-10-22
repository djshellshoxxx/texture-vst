#pragma once

#include "GrainClock.h"
#include "../Modulation/PersonalityMap.h"

namespace texture
{
    /** Everything the grain-level features need from the user for one block. */
    struct GrainRenderInputs
    {
        RhythmSettings rhythm;
        float morph { params::morphDefault };                       // effective MORPH, after the LFO
        Personality personality { Personality::legacy };            // F16
        float personalityIntensity { params::personalityIntensityDefault };
    };

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

        /** Once per block, before the grain overrides resolve. Applies the personality to
            `targets` (crossfaded over 20 ms when the style changes) and sets the grid, which
            Pulse can engage. `gateSetsDuration` is true when the grain size follows MORPH (F13);
            a manual size overrides the gate. The transport is the position at the block start. */
        void beginBlock (const GrainRenderInputs& inputs, MorphTargets& targets,
                         bool gateSetsDuration, const TransportSpan& transport, int numSamples) noexcept;

        /** Rhythm only, with no personality. Used where no MORPH mapping is in play. */
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
        void beginRhythm (const RhythmSettings& rhythmSettings, bool gateSetsDuration,
                          const TransportSpan& transport) noexcept;

        GrainClock clock;
        PersonalityRamp personalityRamp;
        RhythmSettings rhythm;
        bool gateSetsDuration { true };
        TransportSpan blockTransport;
        double sampleRate { 44100.0 };
        double blockTempoBpm { rhythm::defaultBpm };
        GrainEventList events;
        RhythmSpan span;
    };
}
