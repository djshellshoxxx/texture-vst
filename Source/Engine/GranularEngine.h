#pragma once

#include "Voice.h"

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
        void noteOn (int midiChannel, int midiNote, float velocity);
        void noteOff (int midiChannel, int midiNote);
        void setSustainPedal (bool isDown);
        void setPitchBend (int pitchWheelValue);   // 0 .. 16383, 8192 = centre
        void allNotesOff();
        void allSoundOff();

        // --- Per-block parameters -------------------------------------------
        void setAdsrParameters (float attackMs, float decayMs, float sustainLevel, float releaseMs);

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

        bool isSustainPedalDown() const noexcept { return sustainPedalDown; }

        float getPitchBendRatio() const noexcept { return pitchBendRatio; }

    private:
        int findVoiceToUse (int midiNote) noexcept;

        std::array<Voice, maxVoices> voices {};
        juce::AudioBuffer<float> scratch;

        double preparedSampleRate { 44100.0 };
        int preparedBlockSize { 512 };

        bool sustainPedalDown { false };
        float pitchBendRatio { 1.0f };
        juce::int64 noteCounter { 0 };

        float attackMs { 250.0f };
        float decayMs { 500.0f };
        float sustainLevel { 0.8f };
        float releaseMs { 1200.0f };

        std::atomic<float> displayReadPosition { 0.0f };
        std::atomic<int> activeVoiceCount { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranularEngine)
    };
}
