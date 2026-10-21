#pragma once

#include <array>
#include <cstddef>

namespace texture
{
    /** Stereo high-pass and low-pass, in series, at 12 dB/oct each.

        Both filters are second-order Butterworth biquads (Q = 1/sqrt 2), evaluated in
        transposed direct form II with double-precision state. Cutoffs are clamped to
        [10 Hz, 0.45 * sampleRate], move in the log domain with a 20 ms time constant
        scale, and the coefficients are refreshed every 16 samples.

        Enable and disable both crossfade linearly over 20 ms. The filter state keeps
        running while bypassed so that re-enabling does not click, and once the fade has
        finished the bypass path returns the scrubbed input sample exactly.

        Setters are plain assignments, like Saturator::setDrive. Call them from the audio
        thread, or under the same synchronisation as process().
    */
    class ToneFilter
    {
    public:
        static constexpr float minimumCutoffHz = 10.0f;
        static constexpr double maximumCutoffRatio = 0.45;
        static constexpr double transitionSeconds = 0.02;

        void prepare (double newSampleRate, int maximumBlockSize);

        /** Clears filter history and snaps cutoffs and the crossfade to their targets. */
        void reset() noexcept;

        /** Requests a cutoff. Non-finite values are ignored; the clamp is applied at use. */
        void setHighPassHz (float hz) noexcept;
        void setLowPassHz (float hz) noexcept;

        /** Enables or bypasses the filters with a 20 ms crossfade. */
        void setEnabled (bool shouldBeEnabled) noexcept;
        bool isEnabled() const noexcept { return enabled; }

        /** The clamped cutoff the filter is moving towards. */
        float getHighPassHz() const noexcept;
        float getLowPassHz() const noexcept;

        /** Processes in place. Real-time safe, no allocation. Non-finite input becomes 0. */
        void process (float* left, float* right, int numSamples) noexcept;

    private:
        struct Coefficients
        {
            double b0 { 1.0 };
            double b1 { 0.0 };
            double b2 { 0.0 };
            double a1 { 0.0 };
            double a2 { 0.0 };
        };

        struct Channel
        {
            double z1 { 0.0 };
            double z2 { 0.0 };
        };

        struct Biquad
        {
            Coefficients coefficients;
            std::array<Channel, 2> channels {};
        };

        double clampedCutoff (float hz) const noexcept;
        void advanceCutoffs (int numSamples) noexcept;
        void refreshCoefficients() noexcept;
        static void designButterworth (Coefficients& c, double cutoffHz, bool highPass, double rate) noexcept;
        static double runBiquad (const Coefficients& c, Channel& state, double x) noexcept;
        double filterSample (double x, std::size_t channel) noexcept;
        static float mix (float dry, double filtered, double amount) noexcept;

        Biquad highPass;
        Biquad lowPass;

        double sampleRate { 44100.0 };
        double crossfadeStep { 0.0 };
        double currentHighPassLog { 0.0 };
        double currentLowPassLog { 0.0 };
        double wetAmount { 0.0 };

        float requestedHighPassHz { 20.0f };
        float requestedLowPassHz { 20000.0f };
        bool enabled { false };
    };
}
