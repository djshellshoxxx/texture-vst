#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace texture
{
    /** Single sine LFO that offsets the MORPH parameter (Pro only).

        Free-running by default; when sync is engaged the rate is derived from the
        host tempo and the phase locks to musical time, per 08_MODULATION.md.
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

        /** Feeds host transport information. Call once per block before advanceBlock(). */
        void updateFromPlayHead (double bpm, bool isPlaying, double ppqPosition);

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

    private:
        void recalculateEffectiveRate() noexcept;

        double preparedSampleRate { 44100.0 };
        double phase { 0.0 };            // normalised [0, 1)
        float requestedRate { 0.2f };
        float effectiveRate { 0.2f };
        float depth { 0.0f };
        bool syncEnabled { false };
        int divisionIndex { 2 };
        double hostBpm { 120.0 };
        bool wasPlaying { false };
        float currentValue { 0.0f };
    };
}
