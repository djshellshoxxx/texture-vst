#pragma once

#include "Biquad.h"
#include "Chorus.h"
#include "Reverb.h"
#include "Saturator.h"
#include "SecretEffect.h"
#include "ToneSpace.h"

#include "../Utility/DspHelpers.h"
#include "../Utility/ParameterIDs.h"

#include <array>

namespace texture
{
    /** The fixed post-voice signal path.

        Tone filter (independent mode only) -> Saturator -> Chorus -> Reverb -> GRIT
        -> stereo width (independent mode only) -> high shelf -> DC blocker -> gain.
        The order is not user-rearrangeable (07_EFFECTS_CHAIN.md, F07).

        With Follow MORPH selected the chain renders exactly the MORPH-derived values
        and bypasses both the filter and the width stage.
    */
    class EffectsChain
    {
    public:
        void prepare (double sampleRate, int maximumBlockSize, int numChannels);
        void releaseResources();
        void reset();

        /** Zeroes the reverb tail as well as the filter state (CC120). */
        void clearTails();

        /** Pushes the MORPH-derived settings for the coming block. Real-time safe. */
        void setMorphTargets (const MorphTargets& targets);

        /** Pushes the independent tone and space settings. Real-time safe. */
        void setToneSpace (const ToneSpaceSettings& settings) noexcept;

        /** The values the chain last applied (after mode routing and ramps). */
        ResolvedToneSpace getAppliedToneSpace() const noexcept { return applied; }

        void setReverbEnabled (bool shouldBeEnabled) noexcept { reverb.setEnabled (shouldBeEnabled); }
        bool isReverbEnabled() const noexcept { return reverb.isEnabled(); }

        /** Output gain in decibels; smoothed over 30 ms internally. */
        void setOutputGainDb (float db) noexcept;

        /** The hidden effect, sitting after the reverb and before the DC blocker.
            Inaudible and free until someone finds it.
        */
        SecretEffect& getSecretEffect() noexcept { return secret; }
        const SecretEffect& getSecretEffect() const noexcept { return secret; }

        /** Processes in place. Real-time safe. */
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

        /** Peak magnitude of the most recent processed block, for the level LED. */
        float getLastBlockPeak() const noexcept { return lastBlockPeak; }
        float getCurrentOutputGain() const noexcept { return outputGain.getCurrentValue(); }

        /** True while the reverb tail still carries audible energy. */
        bool hasTailEnergy() const noexcept { return ! tailEnergy.isSilent() || reverb.hasTailEnergy(); }

        const Saturator& getSaturator() const noexcept { return saturator; }
        const Chorus& getChorus() const noexcept { return chorus; }
        const Reverb& getReverb() const noexcept { return reverb; }

    private:
        void applyToneSpace (int numSamples) noexcept;
        void updateHighShelf (float tiltDb) noexcept;
        void processToneFilter (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                float startMix, float endMix, float startHp, float startLp,
                                float endHp, float endLp) noexcept;
        void processWidth (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                           float startWidth, float endWidth) noexcept;

        static constexpr double gainSmoothingSeconds = 0.03;
        static constexpr float highShelfFrequency = 6000.0f;
        static constexpr float highShelfQ = 0.707f;
        static constexpr int maxSupportedChannels = 2;
        static constexpr int toneFilterChunk = 32;

        Saturator saturator;
        Chorus chorus;
        Reverb reverb;
        SecretEffect secret;

        std::array<dsp::Biquad, maxSupportedChannels> highShelf {};
        std::array<dsp::Biquad, maxSupportedChannels> toneHighPass {};
        std::array<dsp::Biquad, maxSupportedChannels> toneLowPass {};
        std::array<dsp::DcBlocker, maxSupportedChannels> dcBlockers {};

        /** Scratch for the wet tone-filter signal. Sized in prepare(), never in process(). */
        juce::AudioBuffer<float> toneScratch;

        juce::SmoothedValue<float> outputGain;
        dsp::EnergyTracker tailEnergy;

        MorphTargets morphTargets { deriveMorphTargets (params::morphDefault) };
        ToneSpaceSettings toneSettings;
        ToneSpaceRouter router;
        ResolvedToneSpace applied;

        double preparedSampleRate { 44100.0 };
        int preparedChannels { 2 };
        int preparedBlockSize { 512 };
        bool isPrepared { false };
        float currentTiltDb { 1.0e9f };   // forces the first shelf update
        float lastBlockPeak { 0.0f };
    };
}
