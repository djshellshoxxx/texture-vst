#pragma once

#include "ModulationCurve.h"
#include "ModulationOscillator.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <optional>

namespace texture
{
    /** Single LFO that offsets the MORPH parameter (Pro only).

        Free-running by default; when sync is engaged the rate is derived from the
        host tempo and the phase locks to musical time, per 08_MODULATION.md.

        F14 adds the waveform (sine, triangle, two random shapes, a user curve), the
        retrigger mode, a phase offset and a slew time. Sine, Legacy, zero phase reproduces
        the original LFO exactly. Time is kept in samples: the phase accumulates per sample
        and the value is read once per block, as before.
    */
    class MorphLFO
    {
    public:
        void prepare (double sampleRate);
        void reset();

        void setRateHz (float hz) noexcept;
        void setDepth (float newDepth) noexcept;
        void setSyncEnabled (bool shouldSync) noexcept;
        void setDivisionIndex (int index) noexcept;

        // --- F14 modulation shapes. Set once per block; audio thread. ----------
        void setShape (LfoShape newShape) noexcept { shape = newShape; }
        void setRetrigger (LfoRetrigger newRetrigger) noexcept;
        /** Shifts the cycle start, as a fraction of a cycle. Values are clamped to [0, 1). */
        void setPhaseOffset (float fraction) noexcept;
        /** Slew time for the random and curve shapes' transitions, 0 to 100 ms. */
        void setSlewMs (float milliseconds) noexcept;
        void setCurve (const ModulationCurve& newCurve) noexcept { curve = newCurve; }
        /** The patch random seed, which the random shapes draw from. */
        void setSeed (std::uint64_t newSeed) noexcept { seed = newSeed; }

        /** Feeds host transport information. Call once per block before advanceBlock().
            An empty position means the host gave none. A negative position is a real pre-roll
            position in Host mode; the original synced mode treats it as unavailable, as before. */
        void updateFromPlayHead (double bpm, bool isPlaying, std::optional<double> ppqPosition);

        /** The first key of a chord went down. Restarts the cycle in First note mode and does
            nothing in other modes. Call only on the zero-to-one held-note transition. */
        void noteStarted() noexcept;

        /** Advances the phase by numSamples and returns the bipolar value at the
            start of the block. Reading once per block keeps grain scheduling coherent.
        */
        float advanceBlock (int numSamples) noexcept;

        /** Last value produced by advanceBlock, in [-1, 1]. */
        float getCurrentValue() const noexcept { return currentValue; }

        float getDepth() const noexcept { return depth; }

        /** Applies the LFO to a base MORPH value using the summation from the spec. */
        float applyTo (float automatedMorph) const noexcept;

        /** Effective rate in Hz, taking sync into account. Shown in the debug panel. */
        float getEffectiveRateHz() const noexcept { return effectiveRate; }

        /** Cycle position in [0, 1) and whole cycles since the last restart. For tests. */
        double getPhase() const noexcept { return phase; }
        std::int64_t getCycle() const noexcept { return cycle; }

    private:
        void recalculateEffectiveRate() noexcept;
        void setPhaseFromCycles (double cycles) noexcept;

        double preparedSampleRate { 44100.0 };
        double phase { 0.0 };            // normalised [0, 1)
        std::int64_t cycle { 0 };        // whole cycles since the last restart
        float requestedRate { 0.2f };
        float effectiveRate { 0.2f };
        float depth { 0.0f };
        bool syncEnabled { false };
        int divisionIndex { 2 };
        double hostBpm { 120.0 };
        bool wasPlaying { false };
        float currentValue { 0.0f };

        LfoShape shape { LfoShape::sine };
        LfoRetrigger retrigger { LfoRetrigger::legacy };
        float phaseOffset { 0.0f };
        float slewMs { 0.0f };
        ModulationCurve curve;
        std::uint64_t seed { 0 };
    };
}
