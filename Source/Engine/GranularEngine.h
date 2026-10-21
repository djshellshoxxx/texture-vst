#pragma once

#include "MonoNoteStack.h"
#include "SamplePositionController.h"
#include "Voice.h"
#include "../Utility/VersionedRandomStreams.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace texture
{
    /** Voice pool, MIDI dispatch and grain summing.

        The engine holds no ownership of the sample: the processor hands it a
        SampleBufferPtr at the top of every block, which is what makes hot-swapping
        a sample lock-free.
    */
    class GranularEngine
    {
    public:
        /** Hard limit from 10_CONSTRAINTS.md. */
        static constexpr int maxVoices = 16;

        GranularEngine();

        /** Allocates the scratch workspace and resets every voice. Message thread. */
        void prepare (double sampleRate, int maximumBlockSize);

        /** Frees the workspace. Message thread. */
        void releaseResources();

        /** Silences everything immediately. */
        void reset();

        /** Voice mode, note priority and glide (F11). Poly is the default and keeps the
            original behaviour exactly. Called once per block; a changed mode is applied
            at that block boundary. Audio thread only. */
        void setVoiceConfig (const MonoConfig& config);
        VoiceMode getVoiceMode() const noexcept { return voiceMode; }

        // --- MIDI ------------------------------------------------------------
        void noteOn (int midiChannel, int midiNote, float velocity, bool auditionOwned = false);

        /** Where new notes start reading, as a fraction of the sample (waveform click). */
        void setSampleStart (float normalised) noexcept { sampleStart.store (normalised, std::memory_order_relaxed); }
        float getSampleStart() const noexcept { return sampleStart.load (std::memory_order_relaxed); }
        void noteOff (int midiChannel, int midiNote, bool auditionOwned = false);
        void setSustainPedal (int midiChannel, bool isDown);
        /** Convenience overload for offline helpers and legacy tests (channel 1). */
        void setSustainPedal (bool isDown) { setSustainPedal (1, isDown); }
        void setPitchBend (int pitchWheelValue);   // 0 .. 16383, 8192 = centre
        void allNotesOff();
        void allNotesOff (int midiChannel);
        void allSoundOff();
        void allSoundOff (int midiChannel);

        // --- Per-block parameters -------------------------------------------
        void setAdsrParameters (float attackMs, float decayMs, float sustainLevel, float releaseMs);

        /** Selects the deterministic stream mode for subsequently started notes. */
        void setRandomConfig (int mode, std::uint64_t seed) noexcept
        { randomMode = mode < 0 || mode > 2 ? 0 : mode; randomSeed = seed; }
        void resetEvolution() noexcept { evolutionCounter = 0; }
        /** Scan and freeze for this layer (F10). Set once per block. Audio thread only. */
        void setScanSettings (const ScanSettings& settings) noexcept { scanSettings = sanitiseScanSettings (settings); }

        /** If a freeze began since the last call, returns true with its captured centre. The
            processor writes that into scan_position on the message thread. */
        bool takeFreezeLatch (double& centre) noexcept
        {
            if (! freezeLatchPending.exchange (false, std::memory_order_acq_rel))
                return false;

            centre = freezeLatchValue.load (std::memory_order_relaxed);
            return true;
        }

        /** Per-layer pitch calibration. Root is captured by voices at note-on;
            fine tuning is smoothed by each active voice. Audio thread only. */
        void setSourcePitchCalibration (int rootMidiNote, float fineCents) noexcept;

        /** Renders `numSamples` of the voice pool into a cleared stereo buffer.

            Real-time safe. Returns the number of voices that produced audio.

            Takes a raw pointer rather than the SampleBufferPtr: the engine owns
            nothing, and the audio thread must not touch a reference count. The
            caller guarantees the buffer outlives the call.
        */
        int renderNextBlock (const SampleBuffer* source,
                             const MorphTargets& targets,
                             juce::AudioBuffer<float>& output,
                             int startSample,
                             int numSamples) noexcept;

        /** Convenience for the message-thread callers (offline render, tests). */
        int renderNextBlock (const SampleBufferPtr& source,
                             const MorphTargets& targets,
                             juce::AudioBuffer<float>& output,
                             int startSample,
                             int numSamples) noexcept
        {
            return renderNextBlock (source.get(), targets, output, startSample, numSamples);
        }

        // --- Introspection (message thread) ----------------------------------
        int getActiveVoiceCount() const noexcept;

        /** Grains sounding across the pool, and grains started since construction. */
        int getActiveGrainCount() const noexcept;
        std::uint64_t getGrainStartCount() const noexcept;

        /** Read head of the most recently started voice, in [0, 1). */
        float getDisplayReadPosition() const noexcept { return displayReadPosition.load (std::memory_order_relaxed); }

        bool isSustainPedalDown() const noexcept
        {
            return std::any_of (sustainPedalsDown.begin(), sustainPedalsDown.end(),
                                [] (bool isDown) { return isDown; });
        }

        float getPitchBendRatio() const noexcept { return pitchBendRatio; }

        /** Introspection for the monophonic line (tests and the GUI). -1 when silent. */
        int getMonoLineNote() const noexcept;
        int getMonoLineVoiceIndex() const noexcept { return monoVoiceIndex; }
        /** Instantaneous pitch of the line in semitones, including any glide in progress. */
        float getMonoLinePitch() const noexcept;
        /** Velocity the line is sounding at (the value captured when its key was pressed). */
        float getMonoLineVelocity() const noexcept;

        /** Read-only view of the performance state, for tests. */
        const MonoNoteStack& getMonoNoteStack() const noexcept { return monoStack; }

    private:
        int findVoiceToUse (int midiNote, bool auditionOwned) noexcept;

        // --- Monophonic line (F11). All state below is audio-thread owned.
        void monoNoteOn (int midiChannel, int midiNote, float velocity);
        void monoNoteOff (int midiChannel, int midiNote);
        void applyMonoDecision (const MonoNoteStack::Note* after, bool hadSelection,
                                bool previousHeld, bool pressEvent);
        void reselectMonoLine (bool pressEvent);
        bool isLineSounding() const noexcept;
        bool lineSoundsKey (const MonoNoteStack::Note& note) const noexcept;
        void restartMonoLine (const MonoNoteStack::Note& note, bool glide);
        void retargetMonoLine (const MonoNoteStack::Note& note, bool glide);
        void releaseMonoLine() noexcept;
        void changeVoiceMode (VoiceMode newMode);
        void startPolyVoice (int midiChannel, int midiNote, float velocity, bool sustainHeld);
        int lineVoiceForStart (int midiNote) noexcept;

        std::array<Voice, maxVoices> voices {};
        juce::AudioBuffer<float> scratch;

        // Scan and freeze (F10). Audio thread state; the latch is handed to the message thread.
        ScanSettings scanSettings;
        SamplePositionController scanController;
        const SampleBuffer* lastScanSource { nullptr };
        std::atomic<bool> freezeLatchPending { false };
        std::atomic<double> freezeLatchValue { 0.0 };

        VoiceMode voiceMode { VoiceMode::poly };
        NotePriority notePriority { NotePriority::last };
        GlideMode glideMode { GlideMode::overlapOnly };
        int glideSamples { 0 };
        MonoNoteStack monoStack;
        int monoVoiceIndex { -1 };
        float lastMonoPitch { 0.0f };
        bool hasLastMonoPitch { false };
        std::uint64_t monoPressSequence { 0 };

        double preparedSampleRate { 44100.0 };
        int preparedBlockSize { 512 };

        // MIDI is omni, but key ownership and CC64 state remain channel-specific.
        std::array<bool, 16> sustainPedalsDown {};
        float pitchBendRatio { 1.0f };
        int sourceRootMidiNote { 60 };
        float sourceFineCents { 0.0f };
        std::atomic<float> sampleStart { 0.0f };
        juce::int64 noteCounter { 0 };
        std::uint64_t evolutionCounter { 0 };
        int randomMode { 0 };
        std::uint64_t randomSeed { 0 };

        float attackMs { 250.0f };
        float decayMs { 500.0f };
        float sustainLevel { 0.8f };
        float releaseMs { 1200.0f };

        std::atomic<float> displayReadPosition { 0.0f };
        std::atomic<int> activeVoiceCount { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranularEngine)
    };
}
