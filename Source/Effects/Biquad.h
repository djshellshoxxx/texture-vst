#pragma once

#include <cmath>

namespace texture::dsp
{
    /** Second-order IIR section (RBJ cookbook), in transposed direct form II with
        double-precision state.

        Coefficients are plain values: recomputing them never allocates, which is
        what lets the tone and shelf filters move on the audio thread.
    */
    struct BiquadCoefficients
    {
        double b0 { 1.0 };
        double b1 { 0.0 };
        double b2 { 0.0 };
        double a1 { 0.0 };
        double a2 { 0.0 };
    };

    enum class BiquadShape { lowPass, highPass, highShelf };

    /** Butterworth Q: a single second-order section, i.e. a 12 dB/oct cut. */
    inline constexpr double butterworthQ = 0.70710678118654752440;

    /** Computes RBJ coefficients. `frequencyHz` is clamped to (1 Hz, 0.45 * rate)
        so the section stays stable for any input. `shelfGainDb` applies to the
        high shelf only. Inputs must be finite; callers sanitise them first. */
    inline BiquadCoefficients makeBiquad (BiquadShape shape, double sampleRate,
                                          double frequencyHz, double q, double shelfGainDb = 0.0) noexcept
    {
        const auto nyquistLimit = 0.45 * sampleRate;
        const auto frequency = frequencyHz < 1.0 ? 1.0 : (frequencyHz > nyquistLimit ? nyquistLimit : frequencyHz);
        const auto omega = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
        const auto cosine = std::cos (omega);
        const auto sine = std::sin (omega);
        const auto alpha = sine / (2.0 * q);

        BiquadCoefficients c;

        if (shape == BiquadShape::highShelf)
        {
            // A = sqrt(linear gain), so the high-frequency gain equals the requested dB.
            const auto amplitude = std::pow (10.0, shelfGainDb / 40.0);
            const auto root = 2.0 * std::sqrt (amplitude) * alpha;
            const auto a0 = (amplitude + 1.0) - (amplitude - 1.0) * cosine + root;

            c.b0 = amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine + root) / a0;
            c.b1 = -2.0 * amplitude * ((amplitude - 1.0) + (amplitude + 1.0) * cosine) / a0;
            c.b2 = amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine - root) / a0;
            c.a1 = 2.0 * ((amplitude - 1.0) - (amplitude + 1.0) * cosine) / a0;
            c.a2 = ((amplitude + 1.0) - (amplitude - 1.0) * cosine - root) / a0;
            return c;
        }

        const auto a0 = 1.0 + alpha;
        const auto numeratorEdge = shape == BiquadShape::lowPass ? (1.0 - cosine) : (1.0 + cosine);
        const auto sign = shape == BiquadShape::lowPass ? 1.0 : -1.0;

        c.b0 = 0.5 * numeratorEdge / a0;
        c.b1 = sign * numeratorEdge / a0;
        c.b2 = c.b0;
        c.a1 = -2.0 * cosine / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    class Biquad
    {
    public:
        void setCoefficients (const BiquadCoefficients& coefficients) noexcept { c = coefficients; }
        void reset() noexcept { z1 = 0.0; z2 = 0.0; }

        float processSample (float input) noexcept
        {
            const auto x = static_cast<double> (input);
            const auto y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;

            // Flush sub-audible state so long silences cannot leave denormals behind.
            if (std::abs (z1) < 1.0e-30) z1 = 0.0;
            if (std::abs (z2) < 1.0e-30) z2 = 0.0;

            return static_cast<float> (y);
        }

    private:
        BiquadCoefficients c;
        double z1 { 0.0 };
        double z2 { 0.0 };
    };
}
