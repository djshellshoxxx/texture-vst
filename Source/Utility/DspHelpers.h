#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <limits>
#include <type_traits>

/** Header-only DSP primitives. Everything here is `constexpr` or `inline` and
    allocation-free so it is safe to call from the audio thread.
*/
namespace texture::dsp
{
    inline constexpr float pi = 3.14159265358979323846f;
    inline constexpr float twoPi = 2.0f * pi;

    /** Silence threshold used for voice retirement and the idle optimisation. */
    inline constexpr float minusSixtyDb = 0.001f;        // 10^(-60/20)
    inline constexpr float minusEightyDb = 0.0001f;      // 10^(-80/20)

    /** Exponential coefficient that reaches targetRatio after targetSeconds.
        Useful for block-size-independent meter and state decay. */
    inline double decayCoefficient (double elapsedSeconds,
                                    double targetSeconds,
                                    double targetRatio) noexcept
    {
        if (elapsedSeconds <= 0.0)
            return 1.0;

        const auto safeSeconds = juce::jmax (1.0e-6, targetSeconds);
        const auto safeRatio = juce::jlimit (1.0e-12, 0.999999, targetRatio);
        return std::exp (std::log (safeRatio) * elapsedSeconds / safeSeconds);
    }

    /** Converts decibels to a linear gain, clamping -100 dB and below to silence. */
    inline float dbToGain (float db) noexcept
    {
        return db <= -100.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
    }

    /** Converts a linear gain to decibels, flooring at -100 dB. */
    inline float gainToDb (float gain) noexcept
    {
        return gain <= 0.00001f ? -100.0f : 20.0f * std::log10 (gain);
    }

    /** Semitone offset to a playback rate ratio. */
    inline float semitonesToRatio (float semitones) noexcept
    {
        return std::pow (2.0f, semitones * (1.0f / 12.0f));
    }

    /** Cents offset to a playback rate ratio. */
    inline float centsToRatio (float cents) noexcept
    {
        return std::pow (2.0f, cents * (1.0f / 1200.0f));
    }

    /** Linear interpolation. */
    template <typename T>
    inline constexpr T lerp (T a, T b, T t) noexcept
    {
        return a + (b - a) * t;
    }

    /** Clamps to [lo, hi] without branching surprises on NaN (NaN maps to lo). */
    template <typename T>
    inline T clamp (T v, T lo, T hi) noexcept
    {
        if constexpr (std::is_floating_point_v<T>)
            if (! std::isfinite (v))
                return lo;

        return v < lo ? lo : (v > hi ? hi : v);
    }

    /** True if the value is finite and therefore safe to write to an output buffer. */
    inline bool isSafe (float v) noexcept
    {
        return std::isfinite (v);
    }

    /** Replaces NaN/Inf with zero. Defensive, per 16_ERROR_HANDLING.md. */
    inline float sanitise (float v) noexcept
    {
        return std::isfinite (v) ? v : 0.0f;
    }

    /** Scrubs a whole buffer of NaN/Inf in place and returns true if anything was fixed. */
    inline bool sanitiseBuffer (juce::AudioBuffer<float>& buffer) noexcept
    {
        bool fixed = false;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if (! std::isfinite (data[i]))
                {
                    data[i] = 0.0f;
                    fixed = true;
                }
            }
        }

        return fixed;
    }

    /** tanh soft clip with unity-normalised output, per 07_EFFECTS_CHAIN.md.
        `invTanhDrive` is 1/tanh(drive), precomputed once per block by the caller
        so no transcendental is evaluated twice per sample.
    */
    inline float softClip (float x, float drive, float invTanhDrive) noexcept
    {
        return std::tanh (drive * x) * invTanhDrive;
    }

    /** Hann window value for a normalised phase in [0, 1]. */
    inline float hannWindow (float phase) noexcept
    {
        return 0.5f - 0.5f * std::cos (twoPi * phase);
    }

    /** Equal-power pan gains for a position in [-1, 1]. */
    inline void panGains (float pan, float& leftGain, float& rightGain) noexcept
    {
        const auto normalised = (clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.5f;
        const auto angle = normalised * (pi * 0.5f);
        leftGain = std::cos (angle);
        rightGain = std::sin (angle);
    }

    /** Two-point linear interpolation read from a channel, wrapping at the end.
        Deliberately 2-point (04_SYNTH_ENGINE.md) — the granular blur masks it.
    */
    inline float readLinear (const float* data, int numSamples, float position) noexcept
    {
        if (numSamples <= 0)
            return 0.0f;

        auto index0 = static_cast<int> (position);
        const auto frac = position - static_cast<float> (index0);

        if (index0 < 0 || index0 >= numSamples)
            index0 = ((index0 % numSamples) + numSamples) % numSamples;

        auto index1 = index0 + 1;

        if (index1 >= numSamples)
            index1 = 0;

        return data[index0] + frac * (data[index1] - data[index0]);
    }

    /** One-pole DC blocker, 0.995 coefficient (07_EFFECTS_CHAIN.md). */
    class DcBlocker
    {
    public:
        void reset() noexcept
        {
            x1 = 0.0f;
            y1 = 0.0f;
        }

        float process (float x) noexcept
        {
            const auto y = x - x1 + coefficient * y1;
            x1 = x;
            y1 = y;
            return y;
        }

    private:
        static constexpr float coefficient = 0.995f;
        float x1 { 0.0f };
        float y1 { 0.0f };
    };

    /** Tracks decaying energy so the processor knows when a reverb tail has died.
        Decay is based on elapsed audio time rather than callback count, so hosts with
        different block sizes retire tails consistently.
    */
    class EnergyTracker
    {
    public:
        void prepare (double sampleRateToUse, double decaySecondsToUse = 1.5) noexcept
        {
            sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 44100.0;
            decaySeconds = decaySecondsToUse > 0.01 ? decaySecondsToUse : 0.01;
        }

        void reset() noexcept { energy = 0.0f; }

        /** Feeds the peak magnitude of a block and decays the running estimate over
            exactly numSamples of elapsed audio time. */
        void push (float blockPeak, int numSamples) noexcept
        {
            const auto elapsed = static_cast<double> (juce::jmax (0, numSamples)) / sampleRate;
            const auto coefficient = static_cast<float> (
                std::exp (std::log (1.0e-4) * elapsed / decaySeconds));
            energy = juce::jmax (blockPeak, energy * coefficient);
        }

        /** Compatibility overload for legacy callers that do not yet supply a block size. */
        void push (float blockPeak) noexcept
        {
            push (blockPeak, 64);
        }

        bool isSilent (float threshold = minusEightyDb) const noexcept { return energy < threshold; }

        float getEnergy() const noexcept { return energy; }

    private:
        double sampleRate { 44100.0 };
        double decaySeconds { 1.5 };
        float energy { 0.0f };
    };

    /** Soft fold used by the LFO summation so overshoot does not step (08_MODULATION.md). */
    inline float softFold (float v, float lo, float hi) noexcept
    {
        const auto range = hi - lo;

        if (range <= 0.0f)
            return lo;

        auto x = v;

        // Reflect repeatedly; converges immediately for the ranges we use.
        for (int guard = 0; guard < 8 && (x < lo || x > hi); ++guard)
        {
            if (x > hi)
                x = hi - (x - hi);
            else if (x < lo)
                x = lo + (lo - x);
        }

        return clamp (x, lo, hi);
    }
}
