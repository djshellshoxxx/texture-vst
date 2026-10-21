#include "ToneFilter.h"

#include "../Utility/DspHelpers.h"

#include <algorithm>
#include <cmath>

namespace texture
{
    namespace
    {
        constexpr double twoPiDouble = 6.28318530717958647692;
        constexpr double butterworthQ = 0.70710678118654752440;
        constexpr double defaultSampleRate = 44100.0;

        /** One-pole log-domain smoothing with a time constant of a quarter of the
            transition, so the cutoff covers about 98% of a step within 20 ms. */
        constexpr double cutoffTimeConstantSeconds = 0.005;

        /** Log-domain distance below which a cutoff snaps onto its target. */
        constexpr double cutoffSnapEpsilon = 1.0e-9;

        /** Coefficients are refreshed at this interval while the cutoffs move. */
        constexpr int coefficientInterval = 16;

        /** Far above any real signal. Keeps extreme finite input from overflowing the
            biquads, and bounds the output to a finite range. */
        constexpr double maximumFilterInput = 1.0e6;

        /** Flushes tiny state to zero so decaying tails cannot fall into the slow
            denormal range. */
        constexpr double denormalFloor = 1.0e-30;

        double flushDenormal (double value) noexcept
        {
            return std::abs (value) < denormalFloor ? 0.0 : value;
        }
    }

    void ToneFilter::prepare (double newSampleRate, int maximumBlockSize)
    {
        juce::ignoreUnused (maximumBlockSize);

        sampleRate = newSampleRate > 0.0 ? newSampleRate : defaultSampleRate;
        crossfadeStep = 1.0 / (transitionSeconds * sampleRate);
        reset();
    }

    void ToneFilter::reset() noexcept
    {
        currentHighPassLog = std::log (clampedCutoff (requestedHighPassHz));
        currentLowPassLog = std::log (clampedCutoff (requestedLowPassHz));
        wetAmount = enabled ? 1.0 : 0.0;

        highPass.channels = {};
        lowPass.channels = {};

        refreshCoefficients();
    }

    void ToneFilter::setHighPassHz (float hz) noexcept
    {
        if (std::isfinite (hz))
            requestedHighPassHz = hz;
    }

    void ToneFilter::setLowPassHz (float hz) noexcept
    {
        if (std::isfinite (hz))
            requestedLowPassHz = hz;
    }

    void ToneFilter::setEnabled (bool shouldBeEnabled) noexcept
    {
        enabled = shouldBeEnabled;
    }

    float ToneFilter::getHighPassHz() const noexcept
    {
        return static_cast<float> (clampedCutoff (requestedHighPassHz));
    }

    float ToneFilter::getLowPassHz() const noexcept
    {
        return static_cast<float> (clampedCutoff (requestedLowPassHz));
    }

    double ToneFilter::clampedCutoff (float hz) const noexcept
    {
        const auto upper = maximumCutoffRatio * sampleRate;
        const auto value = static_cast<double> (hz);
        return std::min (upper, std::max (static_cast<double> (minimumCutoffHz), value));
    }

    void ToneFilter::advanceCutoffs (int numSamples) noexcept
    {
        const auto keep = std::exp (-static_cast<double> (numSamples)
                                    / (cutoffTimeConstantSeconds * sampleRate));

        const auto highTarget = std::log (clampedCutoff (requestedHighPassHz));
        const auto lowTarget = std::log (clampedCutoff (requestedLowPassHz));

        currentHighPassLog = highTarget + (currentHighPassLog - highTarget) * keep;
        currentLowPassLog = lowTarget + (currentLowPassLog - lowTarget) * keep;

        if (std::abs (currentHighPassLog - highTarget) < cutoffSnapEpsilon)
            currentHighPassLog = highTarget;

        if (std::abs (currentLowPassLog - lowTarget) < cutoffSnapEpsilon)
            currentLowPassLog = lowTarget;

        refreshCoefficients();
    }

    void ToneFilter::refreshCoefficients() noexcept
    {
        designButterworth (highPass.coefficients, std::exp (currentHighPassLog), true, sampleRate);
        designButterworth (lowPass.coefficients, std::exp (currentLowPassLog), false, sampleRate);
    }

    void ToneFilter::designButterworth (Coefficients& c, double cutoffHz, bool isHighPass, double rate) noexcept
    {
        const auto omega = twoPiDouble * cutoffHz / rate;
        const auto cosine = std::cos (omega);
        const auto alpha = std::sin (omega) / (2.0 * butterworthQ);
        const auto a0 = 1.0 + alpha;

        const auto edge = isHighPass ? (1.0 + cosine) * 0.5 : (1.0 - cosine) * 0.5;
        const auto centre = isHighPass ? -(1.0 + cosine) : (1.0 - cosine);

        c.b0 = edge / a0;
        c.b1 = centre / a0;
        c.b2 = edge / a0;
        c.a1 = -2.0 * cosine / a0;
        c.a2 = (1.0 - alpha) / a0;
    }

    double ToneFilter::runBiquad (const Coefficients& c, Channel& state, double x) noexcept
    {
        const auto y = c.b0 * x + state.z1;

        if (! std::isfinite (y))
        {
            state = {};
            return 0.0;
        }

        state.z1 = flushDenormal (c.b1 * x - c.a1 * y + state.z2);
        state.z2 = flushDenormal (c.b2 * x - c.a2 * y);
        return y;
    }

    double ToneFilter::filterSample (double x, std::size_t channel) noexcept
    {
        const auto clamped = std::max (-maximumFilterInput, std::min (maximumFilterInput, x));
        const auto afterHighPass = runBiquad (highPass.coefficients, highPass.channels[channel], clamped);
        return runBiquad (lowPass.coefficients, lowPass.channels[channel], afterHighPass);
    }

    float ToneFilter::mix (float dry, double filtered, double amount) noexcept
    {
        if (amount <= 0.0)
            return dry;

        if (amount >= 1.0)
            return dsp::sanitise (static_cast<float> (filtered));

        const auto blended = static_cast<double> (dry) * (1.0 - amount) + filtered * amount;
        return dsp::sanitise (static_cast<float> (blended));
    }

    void ToneFilter::process (float* left, float* right, int numSamples) noexcept
    {
        if (left == nullptr || right == nullptr)
            return;

        for (int start = 0; start < numSamples; start += coefficientInterval)
        {
            const auto count = std::min (coefficientInterval, numSamples - start);
            advanceCutoffs (count);

            for (int i = start; i < start + count; ++i)
            {
                wetAmount = enabled ? std::min (1.0, wetAmount + crossfadeStep)
                                    : std::max (0.0, wetAmount - crossfadeStep);

                // Scrubbed before anything else. Finite samples pass through unchanged,
                // which keeps the bypass path bit-identical.
                const auto dryLeft = dsp::sanitise (left[i]);
                const auto dryRight = dsp::sanitise (right[i]);

                // Filters always run, so their state stays continuous across bypass.
                const auto filteredLeft = filterSample (static_cast<double> (dryLeft), 0);
                const auto filteredRight = filterSample (static_cast<double> (dryRight), 1);

                left[i] = mix (dryLeft, filteredLeft, wetAmount);
                right[i] = mix (dryRight, filteredRight, wetAmount);
            }
        }
    }
}
