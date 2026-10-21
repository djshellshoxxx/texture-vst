#pragma once

#include <cmath>

namespace texture
{
    /** Fixed-time pitch glide, linear in semitones, advanced once per sample.

        A glide lasts round(glideMs * sampleRate / 1000) samples regardless of the interval
        crossed, and its endpoints are exact. A new target starts from the instantaneous
        value, so retargeting mid-glide is continuous. A zero duration, including a
        non-positive or non-finite time or an unprepared envelope, jumps immediately.

        Real-time safe: no allocation, no locks, no exceptions.
    */
    class GlideEnvelope final
    {
    public:
        /** Sets the sample rate and settles at the current value, so pitch does not jump. */
        void prepare (double newSampleRate) noexcept
        {
            sampleRate = (std::isfinite (newSampleRate) && newSampleRate > 0.0) ? newSampleRate : 0.0;
            setImmediate (current);
        }

        /** Jumps to the given pitch in semitones and cancels any glide in progress. */
        void setImmediate (float semitones) noexcept
        {
            current = semitones;
            start = semitones;
            target = semitones;
            duration = 0;
            position = 0;
        }

        /** Glides from the instantaneous pitch to the target over glideMs. */
        void setTarget (float semitones, float glideMs) noexcept
        {
            const auto samples = glideSamples (glideMs);
            if (samples <= 0)
            {
                setImmediate (semitones);
                return;
            }

            start = current;
            target = semitones;
            duration = samples;
            position = 0;
        }

        /** Advances one sample and returns the pitch in semitones. Exactly the target once the glide ends. */
        float next() noexcept
        {
            if (position >= duration)
            {
                current = target;
                return current;
            }

            ++position;
            if (position >= duration)
            {
                current = target;
                return current;
            }

            const auto fraction = static_cast<float> (position) / static_cast<float> (duration);
            current = start + (target - start) * fraction;
            return current;
        }

        /** Current pitch in semitones, the value the last next() call returned. */
        float value() const noexcept { return current; }

        bool isGliding() const noexcept { return position < duration; }

    private:
        static constexpr int maximumGlideSamples = 1 << 30;

        int glideSamples (float glideMs) const noexcept
        {
            if (! (glideMs > 0.0f) || ! std::isfinite (glideMs) || sampleRate <= 0.0)
                return 0;

            const auto samples = std::round (static_cast<double> (glideMs) * sampleRate / 1000.0);
            if (samples < 1.0)
                return 0;

            return samples > static_cast<double> (maximumGlideSamples)
                     ? maximumGlideSamples
                     : static_cast<int> (samples);
        }

        double sampleRate = 0.0;
        float current = 0.0f;
        float start = 0.0f;
        float target = 0.0f;
        int duration = 0;
        int position = 0;
    };
}
