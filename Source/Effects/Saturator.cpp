#include "Saturator.h"

#include "../Utility/DspHelpers.h"

#include <cmath>

namespace texture
{
    namespace
    {
        /** Drive is smoothed over roughly this long so a MORPH sweep never steps. */
        constexpr double driveSmoothingSeconds = 0.02;
        constexpr float minimumDrive = 0.01f;
    }

    void Saturator::prepare (double sampleRate, int maximumBlockSize)
    {
        juce::ignoreUnused (maximumBlockSize);

        const auto rate = sampleRate > 0.0 ? sampleRate : 44100.0;

        // One-pole smoothing applied once per block, so the time constant is in blocks
        // only insofar as the caller uses a fixed block size; using the sample rate
        // keeps it stable enough for a parameter this gentle.
        smoothingCoefficient = static_cast<float> (
            std::exp (-1.0 / (driveSmoothingSeconds * rate / 64.0)));

        currentDrive = targetDrive;
        updateCoefficients();
    }

    void Saturator::reset()
    {
        currentDrive = targetDrive;
        updateCoefficients();
    }

    void Saturator::setDrive (float newDrive) noexcept
    {
        targetDrive = juce::jmax (minimumDrive, newDrive);
    }

    void Saturator::updateCoefficients() noexcept
    {
        const auto t = std::tanh (currentDrive);
        inverseTanhDrive = t > 1.0e-6f ? 1.0f / t : 1.0f;
    }

    float Saturator::processSample (float x) const noexcept
    {
        return dsp::softClip (x, currentDrive, inverseTanhDrive);
    }

    void Saturator::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        currentDrive = targetDrive + (currentDrive - targetDrive) * smoothingCoefficient;
        updateCoefficients();

        const auto drive = currentDrive;
        const auto normalise = inverseTanhDrive;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer (ch, startSample);

            // First stage of the chain, so it also scrubs NaN/Inf. Unscrubbed, an
            // infinite sample leaves tanh() as 1/tanh(drive) - up to 100x full
            // scale at low drive - and that spike then rings through the reverb.
            for (int i = 0; i < numSamples; ++i)
                data[i] = dsp::softClip (dsp::sanitise (data[i]), drive, normalise);
        }
    }
}
