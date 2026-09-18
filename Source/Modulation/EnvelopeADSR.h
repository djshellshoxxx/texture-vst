#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace texture
{
    /** Amplitude envelope for one voice.

        A thin wrapper over juce::ADSR that accepts times in milliseconds and only
        pushes new parameters into JUCE when they actually change, so the common
        case (nothing moved this block) costs a few float compares.
    */
    class EnvelopeADSR
    {
    public:
        EnvelopeADSR();

        /** Must be called before any processing. Safe to call repeatedly. */
        void prepare (double sampleRate);

        /** Updates times in milliseconds and sustain in [0, 1]. */
        void setParameters (float attackMs, float decayMs, float sustainLevel, float releaseMs);

        void noteOn();
        void noteOff();

        /** Immediately silences without a release stage. */
        void reset();

        /** Returns the next envelope value and advances by one sample. */
        float getNextSample() noexcept { return adsr.getNextSample(); }

        bool isActive() const noexcept { return adsr.isActive(); }
        bool isInReleasePhase() const noexcept { return releasing && adsr.isActive(); }

        /** Release time in seconds; the engine uses it to size its retirement window. */
        float getReleaseSeconds() const noexcept { return current.release; }

    private:
        juce::ADSR adsr;
        juce::ADSR::Parameters current;
        bool releasing { false };
        double preparedSampleRate { 44100.0 };
    };
}
