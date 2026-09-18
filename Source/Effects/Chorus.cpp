#include "Chorus.h"

namespace texture
{
    void Chorus::prepare (double sampleRate, int maximumBlockSize, int numChannels)
    {
        const juce::dsp::ProcessSpec spec {
            sampleRate > 0.0 ? sampleRate : 44100.0,
            static_cast<juce::uint32> (juce::jmax (1, maximumBlockSize)),
            static_cast<juce::uint32> (juce::jmax (1, numChannels))
        };

        chorus.prepare (spec);
        chorus.setRate (fixedRateHz);
        chorus.setCentreDelay (centreDelayMs);
        chorus.setFeedback (feedbackAmount);
        chorus.setDepth (targetDepth);

        // juce::dsp::Chorus does its own dry/wet, but we want a fixed parallel wet
        // level independent of depth, so the chorus runs fully wet into a side buffer.
        chorus.setMix (1.0f);

        wetBuffer.setSize (juce::jmax (1, numChannels), juce::jmax (1, maximumBlockSize), false, true, true);
        isPrepared = true;
    }

    void Chorus::reset()
    {
        if (isPrepared)
            chorus.reset();

        wetBuffer.clear();
    }

    void Chorus::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        if (! isPrepared || numSamples <= 0)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), wetBuffer.getNumChannels());

        if (numChannels <= 0 || wetBuffer.getNumSamples() < numSamples)
            return;

        // Depth 0 means the chorus is inaudible; skip the work entirely.
        if (targetDepth <= 0.0001f)
        {
            chorus.setDepth (0.0f);
            return;
        }

        chorus.setDepth (targetDepth);

        for (int ch = 0; ch < numChannels; ++ch)
            wetBuffer.copyFrom (ch, 0, buffer, ch, startSample, numSamples);

        juce::dsp::AudioBlock<float> block (wetBuffer.getArrayOfWritePointers(),
                                            static_cast<size_t> (numChannels),
                                            0,
                                            static_cast<size_t> (numSamples));

        juce::dsp::ProcessContextReplacing<float> context (block);
        chorus.process (context);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addFrom (ch, startSample, wetBuffer, ch, 0, numSamples, wetMixLevel);
    }
}
