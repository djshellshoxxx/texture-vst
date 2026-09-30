#pragma once

#include "Grain.h"
#include "../Modulation/EnvelopeADSR.h"
#include "../Utility/ParameterIDs.h"

#include <array>

namespace texture
{
    /** One polyphonic voice: an ADSR, a read head, and a small pool of grains.

        Voices never allocate. The grain pool is a fixed std::array sized to the
        hard limit from 10_CONSTRAINTS.md, and the scratch mix buffer is owned by
        the GranularEngine and handed in per block.
    */
    class Voice
    {
    public:
        /** Hard limit from 10_CONSTRAINTS.md. */
        static constexpr int maxGrains = 8;

        /** Samples are assumed to be tuned to C3 = MIDI 60 (04_SYNTH_ENGINE.md). */
        static constexpr int rootMidiNote = 60;

        Voice() = default;

        void prepare (double sampleRate);

        /** Clears the envelope and all grains without a release stage. */
        void reset();

        /** `startNormalised` is where the read head begins, as a fraction of the
            sample; it is resolved against the source length on the first render. */
        void startNote (int midiNote, float velocity, int midiChannel, juce::int64 noteStamp,
                        float startNormalised = 0.0f);

        /** Begins the release stage. The voice keeps rendering until the tail ends. */
        void stopNote();

        /** Kills the voice immediately (all-sound-off / voice steal). */
        void hardStop();

        bool isActive() const noexcept { return active; }
        bool isReleasing() const noexcept { return active && envelope.isInReleasePhase(); }

        int getMidiNote() const noexcept { return currentNote; }
        int getMidiChannel() const noexcept { return currentChannel; }
        juce::int64 getNoteStamp() const noexcept { return stamp; }

        /** Envelope level multiplied by velocity: the voice-stealing score. */
        float getStealScore() const noexcept { return lastEnvelopeLevel * velocityGain; }

        /** True while a sustain pedal is holding this voice after key-up. */
        bool isHeldBySustain() const noexcept { return sustainHeld; }
        void setHeldBySustain (bool shouldHold) noexcept { sustainHeld = shouldHold; }

        void setAdsrParameters (float attackMs, float decayMs, float sustainLevel, float releaseMs);

        /** Pitch bend as a multiplicative ratio, shared by all voices. */
        void setPitchBendRatio (float ratio) noexcept { pitchBendRatio = ratio; }

        /** Normalised read-head position in [0, 1), for the waveform playhead. */
        float getNormalisedReadPosition() const noexcept { return normalisedReadPosition; }

        /** Renders one block, adding the enveloped result to the stereo output.

            Real-time safe. `targets` is derived once per block by the engine, and
            `scratch` is the engine-owned stereo workspace (already sized to the
            maximum block size in prepareToPlay) that grains are summed into before
            the envelope is applied.
        */
        void renderAdding (const SampleBuffer& source,
                           const MorphTargets& targets,
                           juce::AudioBuffer<float>& scratch,
                           float* leftOut,
                           float* rightOut,
                           int numSamples) noexcept;

    private:
        void spawnGrain (const SampleBuffer& source,
                         const MorphTargets& targets,
                         int grainDurationSamples,
                         float taper) noexcept;

        int findFreeGrainSlot() noexcept;

        std::array<Grain, maxGrains> grains {};
        EnvelopeADSR envelope;

        bool active { false };
        bool sustainHeld { false };
        int currentNote { -1 };
        int currentChannel { 1 };
        juce::int64 stamp { 0 };

        float velocityGain { 1.0f };
        float basePitchRatio { 1.0f };
        float pitchBendRatio { 1.0f };
        float lastEnvelopeLevel { 0.0f };
        float normalisedReadPosition { 0.0f };

        double readPosition { 0.0 };       // in source samples
        float pendingStart { 0.0f };
        bool startPending { false };
        double samplesUntilNextGrain { 0.0 };
        double preparedSampleRate { 44100.0 };

        juce::Random voiceRandom;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Voice)
    };
}
