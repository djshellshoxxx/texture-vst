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
        updateParameters();
        reverb.reset();
        isPrepared = true;
    }

    void Reverb::reset()
    {
        if (isPrepared)
            reverb.reset();
    }

    void Reverb::clearTail()
    {
        // juce::dsp::Reverb::reset zeroes every comb and allpass buffer, which is
        // exactly what CC120 (all sound off) asks for.
        reset();
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
        if (! isPrepared || ! enabled || numSamples <= 0)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);

        if (numChannels <= 0)
            return;

        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(),
                                            static_cast<size_t> (numChannels),
                                            static_cast<size_t> (startSample),
                                            static_cast<size_t> (numSamples));

        juce::dsp::ProcessContextReplacing<float> context (block);
        reverb.process (context);
    }
}
