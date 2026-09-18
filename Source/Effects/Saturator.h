#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace texture
{
    /** tanh soft clipper, normalised so unity in stays unity out.

        Drive is MORPH-controlled only (0.1 .. 0.35) and is never exposed to the
        user, per 07_EFFECTS_CHAIN.md. At those depths it adds warmth, not grit.
    */
    class Saturator
    {
    public:
        void prepare (double sampleRate, int maximumBlockSize);
        void reset();

        /** Sets the drive for the coming block. Cheap: the reciprocal of tanh(drive)
            is only recomputed when the drive actually moves.
        */
        void setDrive (float newDrive) noexcept;

        float getDrive() const noexcept { return targetDrive; }

        /** Processes in place. Real-time safe. */
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

        /** Single-sample helper, used by the unit tests. */
        float processSample (float x) const noexcept;

    private:
        void updateCoefficients() noexcept;

        float targetDrive { 0.1f };
        float currentDrive { 0.1f };
        float inverseTanhDrive { 1.0f };
        float smoothingCoefficient { 0.0f };
    };
}
