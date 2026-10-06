#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Utility/DspHelpers.h"

namespace texture
{
    /** Algorithmic reverb wrapping juce::dsp::Reverb.

        Room size, damping and width are fixed by 07_EFFECTS_CHAIN.md; the wet level
        follows MORPH and the dry level is derived from it so the signal is never
        fully dry-killed.
    */
    class Reverb
    {
    public:
        void prepare (double sampleRate, int maximumBlockSize, int numChannels);
        void reset();

        /** Clears the tail so no energy survives an all-sound-off. */
        void clearTail();

        /** Wet level in [0, 1], driven by MORPH (0.15 .. 0.65). */
        void setWetLevel (float newWetLevel) noexcept;

        void setEnabled (bool shouldBeEnabled) noexcept;
        bool isEnabled() const noexcept { return enabled; }

        float getWetLevel() const noexcept { return wetLevel; }
        float getEnableMix() const noexcept { return enableMix.getCurrentValue(); }
        bool hasTailEnergy() const noexcept { return ! tailEnergy.isSilent(); }

        /** Processes in place. When disabled the buffer is left untouched, with no
            dry-level modification either (07_EFFECTS_CHAIN.md).
        */
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    private:
        void updateParameters() noexcept;

        static constexpr float fixedRoomSize = 0.85f;
        static constexpr float fixedDamping = 0.4f;
        static constexpr float fixedWidth = 1.0f;
        static constexpr float fixedFreezeMode = 0.0f;
        static constexpr double bypassSmoothingSeconds = 0.02;

        juce::dsp::Reverb reverb;
        juce::dsp::Reverb::Parameters parameters;
        juce::AudioBuffer<float> wetBuffer;
        juce::SmoothedValue<float> enableMix;
        dsp::EnergyTracker tailEnergy;
        float wetLevel { 0.15f };
        bool enabled { true };
        bool isPrepared { false };
        int preparedChannels { 2 };
    };
}
