#include "EffectsChain.h"

#include <cmath>

namespace texture
{
    void EffectsChain::prepare (double sampleRate, int maximumBlockSize, int numChannels)
    {
        preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        preparedChannels = juce::jlimit (1, maxSupportedChannels, numChannels);
        preparedBlockSize = juce::jmax (1, maximumBlockSize);

        saturator.prepare (preparedSampleRate, preparedBlockSize);
        chorus.prepare (preparedSampleRate, preparedBlockSize, preparedChannels);
        reverb.prepare (preparedSampleRate, preparedBlockSize, preparedChannels);
        secret.prepare (preparedSampleRate, preparedChannels);

        // Allocated here, once, so the filter stage never allocates while rendering.
        toneScratch.setSize (maxSupportedChannels, preparedBlockSize, false, true, true);

        for (auto& filter : highShelf)
            filter.reset();

        for (auto& filter : toneHighPass)
            filter.reset();

        for (auto& filter : toneLowPass)
            filter.reset();

        currentTiltDb = 1.0e9f;
        updateHighShelf (0.0f);

        outputGain.reset (preparedSampleRate, gainSmoothingSeconds);
        outputGain.setCurrentAndTargetValue (1.0f);

        for (auto& blocker : dcBlockers)
            blocker.reset();

        router.prepare (preparedSampleRate);
        router.reset (morphTargets);
        applied = router.advance (morphTargets, toneSettings, 0);

        tailEnergy.prepare (preparedSampleRate, 1.5);
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
        secret.reset();

        for (auto& filter : highShelf)
            filter.reset();

        for (auto& filter : toneHighPass)
            filter.reset();

        for (auto& filter : toneLowPass)
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
        secret.reset();

        for (auto& filter : highShelf)
            filter.reset();

        for (auto& filter : toneHighPass)
            filter.reset();

        for (auto& filter : toneLowPass)
            filter.reset();

        for (auto& blocker : dcBlockers)
            blocker.reset();

        tailEnergy.reset();
        lastBlockPeak = 0.0f;
    }

    void EffectsChain::setMorphTargets (const MorphTargets& targets)
    {
        morphTargets = targets;

        // Zero samples: a settled Follow MORPH setting takes effect immediately, exactly
        // as before; a pending mode transition does not move until process() runs.
        applyToneSpace (0);
    }

    void EffectsChain::setToneSpace (const ToneSpaceSettings& settings) noexcept
    {
        toneSettings = sanitiseToneSpace (settings);
    }

    void EffectsChain::applyToneSpace (int numSamples) noexcept
    {
        applied = router.advance (morphTargets, toneSettings, numSamples);

        saturator.setDrive (applied.saturatorDrive);
        chorus.setDepth (applied.chorusDepth);
        reverb.setWetLevel (applied.reverbSend);
        updateHighShelf (applied.highShelfTiltDb);
    }

    void EffectsChain::updateHighShelf (float tiltDb) noexcept
    {
        if (juce::approximatelyEqual (tiltDb, currentTiltDb))
            return;

        currentTiltDb = tiltDb;

        const auto coefficients = dsp::makeBiquad (dsp::BiquadShape::highShelf, preparedSampleRate,
                                                   highShelfFrequency, highShelfQ, tiltDb);

        for (auto& filter : highShelf)
            filter.setCoefficients (coefficients);
    }

    void EffectsChain::setOutputGainDb (float db) noexcept
    {
        outputGain.setTargetValue (dsp::dbToGain (juce::jlimit (params::gainMinDb, params::gainMaxDb, db)));
    }

    void EffectsChain::processToneFilter (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                          float startMix, float endMix, float startHp, float startLp,
                                          float endHp, float endLp) noexcept
    {
        const auto numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);

        // Wet copy in scratch; the buffer keeps the dry signal until the final mix.
        for (int ch = 0; ch < numChannels; ++ch)
            toneScratch.copyFrom (ch, 0, buffer, ch, startSample, numSamples);

        const auto logStartHp = std::log2 (static_cast<double> (startHp));
        const auto logEndHp = std::log2 (static_cast<double> (endHp));
        const auto logStartLp = std::log2 (static_cast<double> (startLp));
        const auto logEndLp = std::log2 (static_cast<double> (endLp));

        // Cutoffs move in log frequency; coefficients refresh every chunk. Each section
        // is a fixed-Q 12 dB/oct RBJ biquad whose cutoff is clamped below 0.45 * rate.
        for (int offset = 0; offset < numSamples; offset += toneFilterChunk)
        {
            const auto length = juce::jmin (toneFilterChunk, numSamples - offset);
            const auto t = static_cast<double> (offset) / static_cast<double> (numSamples);
            const auto hp = std::exp2 (logStartHp + (logEndHp - logStartHp) * t);
            const auto lp = std::exp2 (logStartLp + (logEndLp - logStartLp) * t);

            const auto highPassCoefficients = dsp::makeBiquad (dsp::BiquadShape::highPass, preparedSampleRate,
                                                               hp, dsp::butterworthQ);
            const auto lowPassCoefficients = dsp::makeBiquad (dsp::BiquadShape::lowPass, preparedSampleRate,
                                                              lp, dsp::butterworthQ);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* wet = toneScratch.getWritePointer (ch, 0);
                toneHighPass[static_cast<size_t> (ch)].setCoefficients (highPassCoefficients);
                toneLowPass[static_cast<size_t> (ch)].setCoefficients (lowPassCoefficients);

                for (int i = offset; i < offset + length; ++i)
                    wet[i] = toneLowPass[static_cast<size_t> (ch)].processSample (
                        toneHighPass[static_cast<size_t> (ch)].processSample (wet[i]));
            }
        }

        // Dry/wet blend ramps per sample across the block: no step in the mix.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = buffer.getWritePointer (ch, startSample);
            const auto* wet = toneScratch.getReadPointer (ch, 0);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto mix = startMix + (endMix - startMix) * static_cast<float> (i + 1) / static_cast<float> (numSamples);
                out[i] = out[i] + (wet[i] - out[i]) * mix;
            }
        }
    }

    void EffectsChain::processWidth (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                     float startWidth, float endWidth) noexcept
    {
        if (buffer.getNumChannels() < 2)
            return;   // a mono bus has no side signal to widen

        auto* left = buffer.getWritePointer (0, startSample);
        auto* right = buffer.getWritePointer (1, startSample);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto width = startWidth + (endWidth - startWidth) * static_cast<float> (i + 1) / static_cast<float> (numSamples);
            const auto mid = (left[i] + right[i]) * 0.5f;
            const auto side = (left[i] - right[i]) * 0.5f;
            left[i] = mid + width * side;
            right[i] = mid - width * side;
        }
    }

    void EffectsChain::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        if (! isPrepared || numSamples <= 0)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);

        if (numChannels <= 0)
            return;

        // Values at the start of this block, then the routed values at its end.
        const auto startMix = applied.filterMix;
        const auto startWidth = applied.width;
        const auto startHp = applied.hpHz;
        const auto startLp = applied.lpHz;

        applyToneSpace (numSamples);

        const auto filterActive = startMix > 0.0f || applied.filterMix > 0.0f;
        const auto widthActive = ! juce::exactlyEqual (startWidth, 1.0f) || ! juce::exactlyEqual (applied.width, 1.0f);

        // 0. Independent tone filter, before the saturator. Bypassed exactly when idle.
        if (filterActive && numSamples <= toneScratch.getNumSamples())
            processToneFilter (buffer, startSample, numSamples, startMix, applied.filterMix,
                               startHp, startLp, applied.hpHz, applied.lpHz);
        else if (! filterActive)
        {
            for (auto& filter : toneHighPass)
                filter.reset();

            for (auto& filter : toneLowPass)
                filter.reset();
        }

        // 1. Saturator
        saturator.process (buffer, startSample, numSamples);

        // 2. Chorus
        chorus.process (buffer, startSample, numSamples);

        // 3. Reverb
        reverb.process (buffer, startSample, numSamples);

        // 4. The hidden effect, if it has been found.
        secret.process (buffer, startSample, numSamples);

        // 5. Independent stereo width, after GRIT and before the shelf.
        if (widthActive)
            processWidth (buffer, startSample, numSamples, startWidth, applied.width);

        // 6. High shelf air + 7. DC blocker, fused into one pass over the samples.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch, startSample);
            auto& filter = highShelf[static_cast<size_t> (ch)];
            auto& blocker = dcBlockers[static_cast<size_t> (ch)];

            for (int i = 0; i < numSamples; ++i)
                data[i] = blocker.process (filter.processSample (data[i]));
        }

        // 8. Output gain, smoothed so automation never zips.
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
        tailEnergy.push (peak, numSamples);
    }
}
