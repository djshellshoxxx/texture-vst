#include "TestHelpers.h"
#include "../Source/Effects/StereoWidth.h"
#include "../Source/Effects/ToneFilter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace texture::tests
{
    namespace
    {
        constexpr double testRate = 48000.0;

        std::vector<float> sine (double frequencyHz, double rate, int numSamples)
        {
            std::vector<float> out (static_cast<std::size_t> (numSamples));

            for (int i = 0; i < numSamples; ++i)
                out[static_cast<std::size_t> (i)] = static_cast<float> (
                    std::sin (juce::MathConstants<double>::twoPi * frequencyHz * static_cast<double> (i) / rate));

            return out;
        }

        /** Two-tone deterministic test signal, so bit-identical checks are meaningful. */
        std::vector<float> mixedTones (double rate, int numSamples)
        {
            auto out = sine (440.0, rate, numSamples);
            const auto upper = sine (3217.0, rate, numSamples);

            for (std::size_t i = 0; i < out.size(); ++i)
                out[i] = 0.5f * out[i] + 0.3f * upper[i];

            return out;
        }

        bool bitIdentical (const std::vector<float>& a, const std::vector<float>& b) noexcept
        {
            return a.size() == b.size()
                && (a.empty() || std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
        }

        bool allFinite (const std::vector<float>& values) noexcept
        {
            for (const auto value : values)
                if (! std::isfinite (value))
                    return false;

            return true;
        }

        /** Steady-state gain in dB of one filter at one frequency. The filter is
            freshly prepared and snapped so that no smoothing or fade is in play. */
        double measureGainDb (bool highPass, float cutoffHz, double frequencyHz)
        {
            ToneFilter filter;
            filter.prepare (testRate, 512);

            if (highPass)
                filter.setHighPassHz (cutoffHz);
            else
                filter.setLowPassHz (cutoffHz);

            filter.setEnabled (true);
            filter.reset();

            const auto total = static_cast<int> (testRate);      // one second
            const auto tail = static_cast<int> (testRate / 4.0); // measure the last 0.25 s

            const auto input = sine (frequencyHz, testRate, total);
            auto left = input;
            auto right = input;
            filter.process (left.data(), right.data(), total);

            double inputEnergy = 0.0;
            double outputEnergy = 0.0;

            for (int i = total - tail; i < total; ++i)
            {
                const auto index = static_cast<std::size_t> (i);
                inputEnergy += static_cast<double> (input[index]) * static_cast<double> (input[index]);
                outputEnergy += static_cast<double> (left[index]) * static_cast<double> (left[index]);
            }

            return 10.0 * std::log10 (outputEnergy / inputEnergy);
        }

        /** Butterworth second-order magnitude in dB, the reference for the measurements. */
        double butterworthDb (bool highPass, double cutoffHz, double frequencyHz)
        {
            const auto ratio = frequencyHz / cutoffHz;
            const auto fourth = ratio * ratio * ratio * ratio;
            const auto magnitudeSquared = highPass ? fourth / (1.0 + fourth) : 1.0 / (1.0 + fourth);
            return 10.0 * std::log10 (magnitudeSquared);
        }
    }

    class ToneSpaceTests final : public juce::UnitTest
    {
    public:
        ToneSpaceTests() : juce::UnitTest ("Tone and space", "texture") {}

        void runTest() override
        {
            beginTest ("low-pass is -3 dB at cutoff and follows 12 dB/oct");
            {
                const auto atCutoff = measureGainDb (false, 1000.0f, 1000.0);
                expectWithinAbsoluteError (atCutoff, -3.01, 0.2);

                for (const auto frequency : { 250.0, 500.0, 2000.0, 4000.0 })
                    expectWithinAbsoluteError (measureGainDb (false, 1000.0f, frequency),
                                               butterworthDb (false, 1000.0, frequency), 0.6);

                // One octave above cutoff in the stop band should drop about 12 dB.
                const auto octaveDrop = measureGainDb (false, 1000.0f, 4000.0)
                                      - measureGainDb (false, 1000.0f, 2000.0);
                expectWithinAbsoluteError (octaveDrop, -12.0, 0.3);
            }

            beginTest ("high-pass is -3 dB at cutoff and follows 12 dB/oct");
            {
                const auto atCutoff = measureGainDb (true, 1000.0f, 1000.0);
                expectWithinAbsoluteError (atCutoff, -3.01, 0.2);

                for (const auto frequency : { 250.0, 500.0, 2000.0, 4000.0 })
                    expectWithinAbsoluteError (measureGainDb (true, 1000.0f, frequency),
                                               butterworthDb (true, 1000.0, frequency), 0.6);

                // One octave below cutoff in the stop band should drop about 12 dB.
                const auto octaveDrop = measureGainDb (true, 1000.0f, 250.0)
                                      - measureGainDb (true, 1000.0f, 500.0);
                expectWithinAbsoluteError (octaveDrop, -12.0, 0.3);
            }

            beginTest ("impulse response is finite and decays");
            {
                ToneFilter filter;
                filter.prepare (testRate, 512);
                filter.setLowPassHz (2000.0f);
                filter.setEnabled (true);
                filter.reset();

                std::vector<float> left (4096, 0.0f);
                std::vector<float> right (4096, 0.0f);
                left[0] = 1.0f;
                right[0] = 1.0f;

                filter.process (left.data(), right.data(), static_cast<int> (left.size()));

                expect (allFinite (left));
                expect (allFinite (right));
                expect (std::abs (left.back()) < 1.0e-6f);
            }

            beginTest ("cutoffs clamp to 0.45 * sampleRate at 22.05 kHz");
            {
                ToneFilter filter;
                filter.prepare (22050.0, 512);

                filter.setLowPassHz (20000.0f);
                expectWithinAbsoluteError (filter.getLowPassHz(), 0.45f * 22050.0f, 0.01f);

                filter.setHighPassHz (1.0f);
                expectWithinAbsoluteError (filter.getHighPassHz(), ToneFilter::minimumCutoffHz, 1.0e-4f);

                filter.setLowPassHz (std::numeric_limits<float>::quiet_NaN());
                expectWithinAbsoluteError (filter.getLowPassHz(), 0.45f * 22050.0f, 0.01f);
            }

            beginTest ("bypass is bit-identical once the fade has finished");
            {
                ToneFilter filter;
                filter.prepare (testRate, 512);
                filter.setLowPassHz (1000.0f);
                filter.setEnabled (true);
                filter.reset();

                const auto warmup = mixedTones (testRate, 4800);
                auto warmupLeft = warmup;
                auto warmupRight = warmup;
                filter.process (warmupLeft.data(), warmupRight.data(), static_cast<int> (warmup.size()));

                filter.setEnabled (false);

                // 20 ms at 48 kHz is 960 samples; 1200 covers the fade plus a margin.
                const auto transitionInput = mixedTones (testRate, 1200);
                auto transitionLeft = transitionInput;
                auto transitionRight = transitionInput;
                filter.process (transitionLeft.data(), transitionRight.data(), 1200);
                expect (! bitIdentical (transitionLeft, transitionInput));

                const auto bypassInput = sine (3000.0, testRate, 1024);
                auto bypassLeft = bypassInput;
                auto bypassRight = bypassInput;
                filter.process (bypassLeft.data(), bypassRight.data(), 1024);
                expect (bitIdentical (bypassLeft, bypassInput));
                expect (bitIdentical (bypassRight, bypassInput));
            }

            beginTest ("extreme and non-finite input stays finite");
            {
                const std::vector<float> extremes {
                    std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max(),
                    1.0e30f,
                    0.5f
                };

                std::vector<float> input (512);
                for (std::size_t i = 0; i < input.size(); ++i)
                    input[i] = extremes[i % extremes.size()];

                ToneFilter filter;
                filter.prepare (testRate, 512);
                filter.setHighPassHz (100.0f);
                filter.setLowPassHz (5000.0f);
                filter.setEnabled (true);
                filter.reset();

                auto left = input;
                auto right = input;
                filter.process (left.data(), right.data(), static_cast<int> (input.size()));
                expect (allFinite (left));
                expect (allFinite (right));

                // Bypass scrubs too.
                filter.setEnabled (false);
                for (int pass = 0; pass < 2; ++pass)
                {
                    auto bypassLeft = input;
                    auto bypassRight = input;
                    filter.process (bypassLeft.data(), bypassRight.data(), static_cast<int> (input.size()));
                    expect (allFinite (bypassLeft));
                    expect (allFinite (bypassRight));
                }
            }

            beginTest ("width zero sums to mono");
            {
                StereoWidth width;
                width.prepare (testRate);
                width.setWidth (0.0f);
                width.reset();

                const auto left = sine (440.0, testRate, 2048);
                const auto right = sine (660.0, testRate, 2048);

                auto outLeft = left;
                auto outRight = right;
                width.process (outLeft.data(), outRight.data(), 2048);

                expect (bitIdentical (outLeft, outRight));

                double maxError = 0.0;
                for (std::size_t i = 0; i < left.size(); ++i)
                {
                    const auto mono = 0.5 * (static_cast<double> (left[i]) + static_cast<double> (right[i]));
                    maxError = std::max (maxError, std::abs (static_cast<double> (outLeft[i]) - mono));
                }
                expectWithinAbsoluteError (maxError, 0.0, 1.0e-6);
            }

            beginTest ("width one is a bit-identical passthrough once settled");
            {
                StereoWidth width;
                width.prepare (testRate);
                width.setWidth (0.0f);
                width.reset();
                width.setWidth (1.0f);

                // Ramp from 0 to 1 over 100 ms, then the samples must not change.
                auto warmupLeft = sine (440.0, testRate, 4800);
                auto warmupRight = sine (660.0, testRate, 4800);
                width.process (warmupLeft.data(), warmupRight.data(), 4800);

                const auto input = mixedTones (testRate, 1024);
                auto left = input;
                auto right = input;
                width.process (left.data(), right.data(), 1024);

                expect (bitIdentical (left, input));
                expect (bitIdentical (right, input));
            }

            beginTest ("width two doubles the side signal");
            {
                StereoWidth width;
                width.prepare (testRate);
                width.setWidth (2.0f);
                width.reset();

                const auto inLeft = sine (440.0, testRate, 512);
                const auto inRight = sine (660.0, testRate, 512);
                auto left = inLeft;
                auto right = inRight;
                width.process (left.data(), right.data(), 512);

                double maxError = 0.0;
                for (std::size_t i = 0; i < inLeft.size(); ++i)
                {
                    const auto expectedSide = 2.0 * (static_cast<double> (inLeft[i]) - static_cast<double> (inRight[i]));
                    const auto actualSide = static_cast<double> (left[i]) - static_cast<double> (right[i]);
                    maxError = std::max (maxError, std::abs (actualSide - expectedSide));
                }
                expectWithinAbsoluteError (maxError, 0.0, 1.0e-5);
            }

            beginTest ("stereo width stays finite at extremes");
            {
                const std::vector<float> extremes {
                    std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max(),
                    0.25f
                };

                for (const auto widthValue : { 0.0f, 1.0f, 2.0f })
                {
                    StereoWidth width;
                    width.prepare (testRate);
                    width.setWidth (widthValue);
                    width.reset();

                    std::vector<float> left;
                    std::vector<float> right;
                    for (std::size_t i = 0; i < 256; ++i)
                    {
                        left.push_back (extremes[i % extremes.size()]);
                        right.push_back (extremes[(i + 2) % extremes.size()]);
                    }

                    width.process (left.data(), right.data(), static_cast<int> (left.size()));
                    expect (allFinite (left));
                    expect (allFinite (right));
                }
            }
        }
    };

    static ToneSpaceTests toneSpaceTests;
}
