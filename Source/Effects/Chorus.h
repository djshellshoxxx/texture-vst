#pragma once

#include <juce_dsp/juce_dsp.h>

namespace texture
{
    /** Two-voice stereo chorus wrapping juce::dsp::Chorus.

        Rate, delay centre and modulation depth are fixed by 07_EFFECTS_CHAIN.md;
        only the depth follows MORPH. The wet signal is summed in parallel at -6 dB.
    */
    class Chorus
    {
    public:
        void prepare (double sampleRate, int maximumBlockSize, int numChannels);
        void reset();

        /** Depth in [0, 1], driven by MORPH. */
        void setDepth (float newDepth) noexcept { targetDepth = juce::jlimit (0.0f, 1.0f, newDepth); }

        float getDepth() const noexcept { return targetDepth; }

        /** Processes in place. Real-time safe. */
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    private:
        static constexpr float fixedRateHz = 0.4f;
        static constexpr float centreDelayMs = 12.0f;
        static constexpr float feedbackAmount = 0.0f;   // 2-voice, no regeneration
        static constexpr float wetMixLevel = 0.5f;      // -6 dB parallel wet

        juce::dsp::Chorus<float> chorus;
        juce::AudioBuffer<float> wetBuffer;
        float targetDepth { 0.0f };
        bool isPrepared { false };
    };
}
