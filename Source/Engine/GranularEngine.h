#pragma once

#include "Voice.h"
#include "../Utility/VersionedRandomStreams.h"

#include <algorithm>
#include <array>
#include <atomic>

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

        /** Read head of the most recently started voice, in [0, 1). */
        float getDisplayReadPosition() const noexcept { return displayReadPosition.load (std::memory_order_relaxed); }

        bool isSustainPedalDown() const noexcept
        {
            return std::any_of (sustainPedalsDown.begin(), sustainPedalsDown.end(),
                                [] (bool isDown) { return isDown; });
        }

        float getPitchBendRatio() const noexcept { return pitchBendRatio; }

    private:
        int findVoiceToUse (int midiNote, bool auditionOwned) noexcept;

        std::array<Voice, maxVoices> voices {};
        juce::AudioBuffer<float> scratch;

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
