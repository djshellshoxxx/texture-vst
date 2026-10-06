#include "Reverb.h"

namespace texture
{
    void Reverb::prepare (double sampleRate, int maximumBlockSize, int numChannels)
    {
        preparedChannels = juce::jmax (1, numChannels);

        const juce::dsp::ProcessSpec spec {
            sampleRate > 0.0 ? sampleRate : 44100.0,
            static_cast<juce::uint32> (juce::jmax (1, maximumBlockSize)),
            static_cast<juce::uint32> (preparedChannels)
        };

        reverb.prepare (spec);
        wetBuffer.setSize (preparedChannels, juce::jmax (1, maximumBlockSize), false, true, true);
        enableMix.reset (spec.sampleRate, bypassSmoothingSeconds);
        enableMix.setCurrentAndTargetValue (enabled ? 1.0f : 0.0f);
        updateParameters();
        reverb.reset();
        isPrepared = true;
    }

    void Reverb::reset()
    {
        if (isPrepared)
            reverb.reset();

        wetBuffer.clear();
        enableMix.setCurrentAndTargetValue (enabled ? 1.0f : 0.0f);
    }

    void Reverb::clearTail()
    {
        // juce::dsp::Reverb::reset zeroes every comb and allpass buffer, which is
        // exactly what CC120 (all sound off) asks for.
        reset();
    }

    void Reverb::setEnabled (bool shouldBeEnabled) noexcept
    {
        if (enabled == shouldBeEnabled)
            return;

        enabled = shouldBeEnabled;
        enableMix.setTargetValue (enabled ? 1.0f : 0.0f);
    }

    void Reverb::setWetLevel (float newWetLevel) noexcept
    {
        const auto clamped = juce::jlimit (0.0f, 1.0f, newWetLevel);

        if (juce::approximatelyEqual (clamped, wetLevel))
            return;

        wetLevel = clamped;
        updateParameters();
    }

    void Reverb::updateParameters() noexcept
    {
        parameters.roomSize = fixedRoomSize;
        parameters.damping = fixedDamping;
        parameters.wetLevel = wetLevel;

        // Never fully dry-killed (07_EFFECTS_CHAIN.md).
        parameters.dryLevel = juce::jlimit (0.0f, 1.0f, 1.0f - wetLevel * 0.5f);
        parameters.width = fixedWidth;
        parameters.freezeMode = fixedFreezeMode;

        reverb.setParameters (parameters);
    }

    void Reverb::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        if (! isPrepared || numSamples <= 0)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);

        if (numChannels <= 0 || wetBuffer.getNumSamples() < numSamples)
            return;

        for (int ch = 0; ch < numChannels; ++ch)
            wetBuffer.copyFrom (ch, 0, buffer, ch, startSample, numSamples);

        juce::dsp::AudioBlock<float> block (wetBuffer.getArrayOfWritePointers(),
                                            static_cast<size_t> (numChannels),
                                            0,
                                            static_cast<size_t> (numSamples));

        juce::dsp::ProcessContextReplacing<float> context (block);
        reverb.process (context);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto mix = enableMix.getNextValue();
            const auto dryMix = 1.0f - mix;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* dry = buffer.getWritePointer (ch, startSample);
                const auto wet = wetBuffer.getSample (ch, i);
                dry[i] = dry[i] * dryMix + wet * mix;
            }
        }
    }
}
