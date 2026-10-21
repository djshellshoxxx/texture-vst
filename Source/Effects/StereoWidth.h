#pragma once

#include "../Utility/DspHelpers.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace texture
{
    /** Mid/side width control. M = (L+R)/2, S = (L-R)/2, out L = M + w*S, R = M - w*S.

        Width 0 gives mono, 1 leaves the signal untouched and 2 doubles the side
        signal. Width is smoothed over 20 ms. While it sits exactly at 1 the samples are
        not rewritten at all, so the default is bit-identical. No gain compensation is
        applied, per F07. The maths runs in double so extreme inputs cannot overflow
        before the final clamp.
    */
    class StereoWidth
    {
    public:
        static constexpr double minimumWidth = 0.0;
        static constexpr double maximumWidth = 2.0;
        static constexpr double smoothingSeconds = 0.02;

        void prepare (double sampleRate) noexcept
        {
            const auto rate = sampleRate > 0.0 ? sampleRate : 44100.0;

            // Quarter of the transition as the time constant, so 98% of a step lands in 20 ms.
            smoothingCoefficient = std::exp (-1.0 / (smoothingSeconds * 0.25 * rate));
            reset();
        }

        void reset() noexcept
        {
            currentWidth = targetWidth;
        }

        /** Requests a width in [0, 2]. Non-finite values are ignored. */
        void setWidth (float newWidth) noexcept
        {
            if (std::isfinite (newWidth))
                targetWidth = std::clamp (static_cast<double> (newWidth), minimumWidth, maximumWidth);
        }

        float getWidth() const noexcept { return static_cast<float> (targetWidth); }

        /** Processes in place. Real-time safe, no allocation. Non-finite input becomes 0. */
        void process (float* l, float* r, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                currentWidth = targetWidth + (currentWidth - targetWidth) * smoothingCoefficient;

                if (std::abs (currentWidth - targetWidth) < snapEpsilon)
                    currentWidth = targetWidth;

                const auto left = dsp::sanitise (l[i]);
                const auto right = dsp::sanitise (r[i]);

                // Settled at unity: leave the samples exactly as they are.
                if (isExactly (currentWidth, 1.0))
                {
                    l[i] = left;
                    r[i] = right;
                    continue;
                }

                const auto dl = static_cast<double> (left);
                const auto dr = static_cast<double> (right);
                const auto mid = 0.5 * (dl + dr);
                const auto side = 0.5 * (dl - dr) * currentWidth;

                l[i] = clampToFloat (mid + side);
                r[i] = clampToFloat (mid - side);
            }
        }

    private:
        static constexpr double snapEpsilon = 1.0e-6;

        /** True only for equal, non-NaN values. Written as ordering tests so that
            -Wfloat-equal stays quiet. */
        static bool isExactly (double a, double b) noexcept
        {
            return ! (a < b) && ! (a > b);
        }

        static float clampToFloat (double value) noexcept
        {
            constexpr auto limit = static_cast<double> (std::numeric_limits<float>::max());
            return static_cast<float> (std::clamp (value, -limit, limit));
        }

        double smoothingCoefficient { 0.0 };
        double targetWidth { 1.0 };
        double currentWidth { 1.0 };
    };
}
