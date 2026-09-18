#pragma once

#include "Chorus.h"
#include "Reverb.h"
#include "Saturator.h"
#include "SecretEffect.h"

#include "../Utility/DspHelpers.h"
#include "../Utility/ParameterIDs.h"

#include <array>

namespace texture
{
    /** The fixed post-voice signal path.

        Saturator -> Chorus -> Reverb -> high shelf -> DC blocker -> output gain.
        The order is not user-rearrangeable (07_EFFECTS_CHAIN.md).
    */
    class EffectsChain
    {
    public:
        void prepare (double sampleRate, int maximumBlockSize, int numChannels);
        void releaseResources();
        void reset();

        /** Zeroes the reverb tail as well as the filter state (CC120). */
        void clearTails();

        /** Pushes the MORPH-derived settings for the coming block. */
        void setMorphTargets (const MorphTargets& targets);

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

        /** True while the reverb tail still carries audible energy. */
        bool hasTailEnergy() const noexcept { return ! tailEnergy.isSilent(); }

        const Saturator& getSaturator() const noexcept { return saturator; }
        const Chorus& getChorus() const noexcept { return chorus; }
        const Reverb& getReverb() const noexcept { return reverb; }

    private:
        void updateHighShelf (float tiltDb);

        static constexpr double gainSmoothingSeconds = 0.03;
        static constexpr float highShelfFrequency = 6000.0f;
        static constexpr float highShelfQ = 0.707f;
        static constexpr int maxSupportedChannels = 2;

        Saturator saturator;
        Chorus chorus;
        Reverb reverb;
        SecretEffect secret;

        using ShelfFilter = juce::dsp::IIR::Filter<float>;
        using ShelfCoefficients = juce::dsp::IIR::Coefficients<float>;

        std::array<ShelfFilter, maxSupportedChannels> highShelf {};
        std::array<dsp::DcBlocker, maxSupportedChannels> dcBlockers {};

        juce::SmoothedValue<float> outputGain;
        dsp::EnergyTracker tailEnergy;

        double preparedSampleRate { 44100.0 };
        int preparedChannels { 2 };
        bool isPrepared { false };
        float currentTiltDb { -1.0f };   // forces the first update
        float lastBlockPeak { 0.0f };
    };
}
