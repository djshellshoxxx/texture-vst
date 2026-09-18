#pragma once

#include "SampleBuffer.h"

namespace texture
{
    /** One windowed, resampled slice of the sample buffer.

        A Grain owns no memory: it is a handful of floats living inside a Voice's
        fixed-size array, so starting and stopping grains never allocates.
    */
    class Grain
    {
    public:
        /** Shortest taper applied to either end of a grain, as a fraction of its
            length. Enough to stop a click, short enough that a non-overlapped grain
            still reads as continuous playback.
        */
        static constexpr float minimumTaper = 0.02f;

        Grain() = default;

        /** Arms the grain. durationSamples must be > 0 or the grain stays idle.

            @param startPositionInSamples  read head in the source, may be out of range
            @param durationSamples         total lifetime in output samples
            @param playbackRatio           resample ratio (pitch)
            @param panPosition             -1 hard left .. +1 hard right
            @param amplitude               linear gain applied under the window
            @param windowTaper             fraction of the grain spent fading, in
                                           [minimumTaper, 1]. At 1 the window is a
                                           true Hann; below that it is a Tukey window
                                           with a flat top, which is what keeps
                                           MORPH=0 sounding like clean playback rather
                                           than amplitude-modulated grains.
        */
        void start (double startPositionInSamples,
                    int durationSamples,
                    float playbackRatio,
                    float panPosition,
                    float amplitude,
                    float windowTaper) noexcept;

        /** Silences the grain immediately. */
        void stop() noexcept { active = false; }

        bool isActive() const noexcept { return active; }

        /** Fraction of the grain's window already consumed, in [0, 1]. */
        float getProgress() const noexcept;

        /** Adds this grain's contribution to the stereo output.

            Real-time safe: no allocation, no locking, no branching on host state.
            `source` must outlive the call; the Voice holds the owning shared_ptr for
            the whole block.
        */
        void renderAdding (const SampleBuffer& source,
                           float* leftOut,
                           float* rightOut,
                           int numSamples) noexcept;

        /** Window amplitude for a normalised position in [0, 1] at a given taper. */
        static float windowValue (float normalisedPosition, float taper) noexcept;

    private:
        bool active { false };
        double readPosition { 0.0 };   // in source samples, wrapped on read
        double increment { 1.0 };      // pitch ratio, applied per output sample
        int samplesRemaining { 0 };
        int totalDuration { 0 };
        float leftGain { 0.7071f };
        float rightGain { 0.7071f };
        float level { 1.0f };
        float taper { 1.0f };
    };
}
