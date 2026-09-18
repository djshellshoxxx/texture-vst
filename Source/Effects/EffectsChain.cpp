#include "EffectsChain.h"

#include <cmath>

namespace texture
{
    void EffectsChain::prepare (double sampleRate, int maximumBlockSize, int numChannels)
    {
        preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        preparedChannels = juce::jlimit (1, maxSupportedChannels, numChannels);

        saturator.prepare (preparedSampleRate, maximumBlockSize);
        chorus.prepare (preparedSampleRate, maximumBlockSize, preparedChannels);
        reverb.prepare (preparedSampleRate, maximumBlockSize, preparedChannels);

        const juce::dsp::ProcessSpec spec {
            preparedSampleRate,
            static_cast<juce::uint32> (juce::jmax (1, maximumBlockSize)),
            1u
        };

        for (auto& filter : highShelf)
        {
            filter.prepare (spec);
            filter.reset();
        }

        currentTiltDb = -1.0f;
        updateHighShelf (0.0f);

        outputGain.reset (preparedSampleRate, gainSmoothingSeconds);
        outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());

        for (auto& blocker : dcBlockers)
            blocker.reset();

        tailEnergy.reset();
        lastBlockPeak = 0.0f;
        isPrepared = true;
    }

    void EffectsChain::releaseResources()
    {
        reset();
        isPrepared = false;
    }

    void EffectsChain::reset()
    {
        saturator.reset();
        chorus.reset();
        reverb.reset();

        for (auto& filter : highShelf)
            filter.reset();

        for (auto& blocker : dcBlockers)
            blocker.reset();

        tailEnergy.reset();
        lastBlockPeak = 0.0f;
    }

    void EffectsChain::clearTails()
    {
        reverb.clearTail();
        chorus.reset();

        for (auto& filter : highShelf)
            filter.reset();

        for (auto& blocker : dcBlockers)
            blocker.reset();

        tailEnergy.reset();
        lastBlockPeak = 0.0f;
    }

    void EffectsChain::setMorphTargets (const MorphTargets& targets)
    {
        saturator.setDrive (targets.saturatorDrive);
        chorus.setDepth (targets.chorusDepth);
        reverb.setWetLevel (targets.reverbSend);
        updateHighShelf (targets.highShelfTiltDb);
    }

    void EffectsChain::updateHighShelf (float tiltDb)
    {
        if (juce::approximatelyEqual (tiltDb, currentTiltDb))
            return;

        currentTiltDb = tiltDb;

        const auto linearGain = dsp::dbToGain (tiltDb);
        auto coefficients = ShelfCoefficients::makeHighShelf (preparedSampleRate,
                                                              highShelfFrequency,
                                                              highShelfQ,
                                                              juce::jmax (0.0001f, linearGain));

        for (auto& filter : highShelf)
            filter.coefficients = coefficients;
    }

    void EffectsChain::setOutputGainDb (float db) noexcept
    {
        outputGain.setTargetValue (dsp::dbToGain (juce::jlimit (params::gainMinDb, params::gainMaxDb, db)));
    }

    void EffectsChain::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        if (! isPrepared || numSamples <= 0)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);

        if (numChannels <= 0)
            return;

        // 1. Saturator
        saturator.process (buffer, startSample, numSamples);

        // 2. Chorus
        chorus.process (buffer, startSample, numSamples);

        // 3. Reverb
        reverb.process (buffer, startSample, numSamples);

        // 4. High shelf air + 5. DC blocker, fused into one pass over the samples.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch, startSample);
            auto& filter = highShelf[static_cast<size_t> (ch)];
            auto& blocker = dcBlockers[static_cast<size_t> (ch)];

            for (int i = 0; i < numSamples; ++i)
                data[i] = blocker.process (filter.processSample (data[i]));
        }

        // 6. Output gain, smoothed so automation never zips.
        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto gain = outputGain.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch, startSample);
                const auto value = dsp::sanitise (data[i] * gain);
                data[i] = value;
                peak = juce::jmax (peak, std::abs (value));
            }
        }

        lastBlockPeak = peak;
        tailEnergy.push (peak);
    }
}
