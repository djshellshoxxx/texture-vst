#pragma once

#include "Grain.h"
#include "AttackCloud.h"
#include "GrainClock.h"
#include "SourcePitchModel.h"
#include "../Modulation/EnvelopeADSR.h"
#include "../Utility/ParameterIDs.h"
#include "../Utility/VersionedRandomStreams.h"

#include <array>
#include <atomic>
#include <cstdint>

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
            sample; it is resolved against the source length on the first render.

            `glideSamples` > 0 starts the pitch at `glideFromSemitones` (absolute, in
            semitones) and moves it linearly to `midiNote` over that many samples. */
        void startNote (int midiNote, float velocity, int midiChannel, juce::int64 noteStamp,
                        float startNormalised = 0.0f, bool auditionOwned = false,
                        int randomMode = params::randomModeDefault,
                        std::uint64_t randomSeed = 0,
                        std::uint64_t randomNoteEvent = 0,
                        int sourceRootMidiNote = sourcePitch::defaultRootMidiNote,
                        float sourceFineCents = 0.0f,
                        float glideFromSemitones = 0.0f,
                        int glideSamples = 0);

        /** Monophonic legato: moves this voice to a new note without restarting its
            envelope, source position or grains. Velocity ramps over `velocityRampSamples`.
            `glideSamples` > 0 glides from the instantaneous pitch; zero jumps. */
        void retarget (int midiNote, float velocity, int midiChannel, juce::int64 noteStamp,
                       int glideSamples, int velocityRampSamples);

        /** Moves the read head to a normalised position on the next block (F10 unfreeze). */
        void requestReadJump (float normalised) noexcept
        {
            pendingStart = juce::jlimit (0.0f, 1.0f, normalised);
            startPending = true;
        }

        /** Begins the release stage. The voice keeps rendering until the tail ends. */
        void stopNote();

        /** Kills the voice immediately (all-sound-off / voice steal). */
        void hardStop();

        /** Fades the voice out over `fadeSamples`, then stops it. Used when a voice mode
            change retires voices, so they never cut off with a click. */
        void beginRetire (int fadeSamples) noexcept;
        bool isRetiring() const noexcept { return retireTotal > 0; }

        /** Instantaneous pitch in semitones: the note plus any glide still to travel. */
        float getPitchSemitones() const noexcept { return static_cast<float> (currentNote) + glideOffset; }

        /** Velocity captured at the last note-on or retarget, used when a key is restored. */
        float getStartVelocity() const noexcept { return startVelocity; }

        bool isActive() const noexcept { return active; }
        bool isReleasing() const noexcept { return active && envelope.isInReleasePhase(); }

        int getMidiNote() const noexcept { return currentNote; }
        int getMidiChannel() const noexcept { return currentChannel; }
        juce::int64 getNoteStamp() const noexcept { return stamp; }
        bool isAuditionOwned() const noexcept { return auditionOwned; }

        /** Envelope level multiplied by velocity: the voice-stealing score. */
        float getStealScore() const noexcept { return lastEnvelopeLevel * velocityGain; }

        /** True while a sustain pedal is holding this voice after key-up. */
        bool isHeldBySustain() const noexcept { return sustainHeld; }
        void setHeldBySustain (bool shouldHold) noexcept { sustainHeld = shouldHold; }

        void setAdsrParameters (float attackMs, float decayMs, float sustainLevel, float releaseMs);

        /** Attack-to-cloud (F17). Latched at note-on by armAttackCloud; a retarget (mono legato)
            never re-arms it, a retrigger does. setAttackOriginal follows the live amount. */
        void armAttackCloud (const AttackCloudSettings& settings) noexcept;
        void setAttackOriginal (float amount) noexcept;

        /** Pitch bend as a multiplicative ratio, shared by all voices. */
        void setPitchBendRatio (float ratio) noexcept { pitchBendRatio = ratio; }

        /** Grains currently sounding in this voice. Diagnostic and test introspection. */
        int getActiveGrainCount() const noexcept;

        /** Grains started since construction. Diagnostic; relaxed reads from any thread. */
        std::uint64_t getGrainStartCount() const noexcept { return grainStarts.load (std::memory_order_relaxed); }

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
                           int numSamples,
                           const RhythmSpan* rhythm = nullptr) noexcept;

    private:
        void spawnGrain (const SampleBuffer& source,
                         const MorphTargets& targets,
                         int grainDurationSamples,
                         float taper,
                         float glideMultiplier,
                         float amplitudeOverride = 0.0f) noexcept;

        /** Direct read of the sample start, crossfading into the grains (F17). Audio thread. */
        void applyAttackCloud (const SampleBuffer& source, float* scratchLeft, float* scratchRight,
                               int numSamples, double readIncrement) noexcept;

        /** Rhythmic path (F19): grains start on the scheduler's events, not on the legacy hop. */
        void renderRhythmicSpan (const SampleBuffer& source, const MorphTargets& targets,
                                 const RhythmSpan& rhythm, int manualDurationSamples, bool canSpawn,
                                 float* scratchLeft, float* scratchRight,
                                 int begin, int end, float multiplier, int& eventIndex) noexcept;
        void renderGrainsOnly (const SampleBuffer& source, float* scratchLeft, float* scratchRight,
                               int from, int to) noexcept;
        void spawnRhythmGrain (const SampleBuffer& source, const MorphTargets& targets,
                               int durationSamples, float intervalSamples, float multiplier) noexcept;
        /** One probability draw per logical grid event, keyed by the voice's seed (which holds the
            layer and the note) and the grid index, so it repeats across renders. */
        float rhythmDraw (std::int64_t gridIndex) const noexcept;

        int findFreeGrainSlot() noexcept;
        float nextRandom (VersionedRandomStreams::Lane lane) noexcept;
        void renderSpan (const SampleBuffer& source, const MorphTargets& targets,
                         int durationSamples, float taper, double hop, bool canSpawn,
                         float* scratchLeft, float* scratchRight,
                         int begin, int end, float multiplier) noexcept;
        void updateGrainPitch (float basePitch, float multiplier) noexcept;

        std::array<Grain, maxGrains> grains {};
        std::atomic<std::uint64_t> grainStarts { 0 };
        /** Each grain's pitch without the note and glide factors, so glide can rescale it. */
        std::array<float, maxGrains> grainPitchFactor {};
        EnvelopeADSR envelope;

        bool active { false };
        bool sustainHeld { false };
        bool auditionOwned { false };
        int currentNote { -1 };
        int currentChannel { 1 };
        int currentSourceRootMidiNote { sourcePitch::defaultRootMidiNote };
        juce::int64 stamp { 0 };

        float velocityGain { 1.0f };
        float velocityTarget { 1.0f };
        float velocityStep { 0.0f };
        int velocityRampRemaining { 0 };
        float startVelocity { 0.0f };
        float pitchBendRatio { 1.0f };

        // Glide: pitch offset in semitones above the note, falling linearly to zero.
        float glideOffset { 0.0f };
        float glideStartOffset { 0.0f };
        int glideTotal { 0 };
        int glideRemaining { 0 };
        bool grainPitchDirty { false };

        // Rhythmic grains (F19). The first grain of a note may start at once.
        bool rhythmFirstPending { false };

        // Attack-to-cloud (F17). The direct read head is independent of the grains.
        AttackCloudPlan attackPlan;
        bool attackArmed { false };
        bool attackFinished { true };
        bool attackReaderDone { true };
        bool attackPositionPending { false };
        int attackElapsed { 0 };
        int attackEarlyShift { 0 };       // moves the crossfade forward when the sample ends first
        double attackPosition { 0.0 };    // direct read head, in source samples
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> attackAmount;

        // Retirement: a linear fade before the voice is stopped.
        int retireTotal { 0 };
        int retireRemaining { 0 };

        float currentTuneSemitones { 0.0f };
        float fineTuneForCurrentBlock { 0.0f };
        float lastEnvelopeLevel { 0.0f };
        float normalisedReadPosition { 0.0f };

        double readPosition { 0.0 };       // in source samples
        float pendingStart { 0.0f };
        bool startPending { false };
        double samplesUntilNextGrain { 0.0 };
        double preparedSampleRate { 44100.0 };

        juce::Random voiceRandom;
        int randomMode { params::randomModeDefault };
        std::uint64_t randomSeed { 0 };
        std::uint64_t randomEvent { 0 };
        std::array<std::uint64_t, 7> randomDrawCounts {};
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> fineTuneCents;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Voice)
    };
}
