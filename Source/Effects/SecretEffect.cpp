#include "SecretEffect.h"

#include "../Utility/DspHelpers.h"

#include <cmath>

namespace texture
{
    void SecretEffect::prepare (double sampleRate, int numChannels)
    {
        juce::ignoreUnused (sampleRate);

        preparedChannels = juce::jlimit (1, maxChannels, numChannels);
        setBitDepth (bitDepth);
        reset();
    }

    void SecretEffect::reset()
    {
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            holdValue[ch] = 0.0f;
            phase[ch] = 0.0f;
        }
    }

    void SecretEffect::setBitDepth (float bits) noexcept
    {
        bitDepth = juce::jlimit (minBits, maxBits, bits);

        // Quantisation step for a signal in [-1, 1] at this word length.
        const auto levels = std::pow (2.0f, bitDepth) - 1.0f;
        quantisationStep = 2.0f / juce::jmax (1.0f, levels);
    }

    void SecretEffect::setDownsample (float factor) noexcept
    {
        downsample = juce::jlimit (minDownsample, maxDownsample, factor);
    }

    void SecretEffect::setMix (float newMix) noexcept
    {
        mix = juce::jlimit (0.0f, 1.0f, newMix);
    }

    void SecretEffect::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        if (! isAudible() || numSamples <= 0)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);

        if (numChannels <= 0)
            return;

        const auto step = quantisationStep;
        const auto inverseStep = 1.0f / step;
        const auto rate = 1.0f / downsample;
        const auto wet = mix;
        const auto dry = 1.0f - mix;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch, startSample);
            auto& held = holdValue[ch];
            auto& channelPhase = phase[ch];

            for (int i = 0; i < numSamples; ++i)
            {
                const auto input = data[i];

                // Sample-and-hold decimation.
                channelPhase += rate;

                if (channelPhase >= 1.0f)
                {
                    channelPhase -= std::floor (channelPhase);

                    // Quantise to the chosen word length.
                    held = std::round (input * inverseStep) * step;
                }

                data[i] = dsp::sanitise (input * dry + held * wet);
            }
        }
    }
}
