#include "Grain.h"

#include "../Utility/DspHelpers.h"

#include <cmath>

namespace texture
{
    float Grain::windowValue (float normalisedPosition, float grainTaper) noexcept
    {
        const auto p = dsp::clamp (normalisedPosition, 0.0f, 1.0f);
        const auto t = dsp::clamp (grainTaper, minimumTaper, 1.0f);
        const auto edge = t * 0.5f;

        if (p < edge)
            return 0.5f - 0.5f * std::cos (dsp::pi * (p / edge));

        if (p > 1.0f - edge)
            return 0.5f - 0.5f * std::cos (dsp::pi * ((1.0f - p) / edge));

        return 1.0f;
    }

    void Grain::start (double startPositionInSamples,
                       int durationSamples,
                       float playbackRatio,
                       float panPosition,
                       float amplitude,
                       float windowTaper) noexcept
    {
        if (durationSamples <= 0)
        {
            active = false;
            return;
        }

        readPosition = startPositionInSamples;
        increment = static_cast<double> (juce::jmax (0.0001f, playbackRatio));
        samplesRemaining = durationSamples;
        totalDuration = durationSamples;
        level = amplitude;
        taper = dsp::clamp (windowTaper, minimumTaper, 1.0f);

        dsp::panGains (panPosition, leftGain, rightGain);

        active = true;
    }

    float Grain::getProgress() const noexcept
    {
        if (! active || totalDuration <= 0)
            return 0.0f;

        return 1.0f - static_cast<float> (samplesRemaining) / static_cast<float> (totalDuration);
    }

    void Grain::renderAdding (const SampleBuffer& source,
                              float* leftOut,
                              float* rightOut,
                              int numSamples) noexcept
    {
        if (! active || leftOut == nullptr || rightOut == nullptr || numSamples <= 0)
            return;

        const auto sourceLength = source.getNumSamples();

        if (sourceLength <= 0)
        {
            active = false;
            return;
        }

        const auto* sourceLeft = source.getChannel (0);
        const auto* sourceRight = source.getChannel (1);

        if (sourceLeft == nullptr || sourceRight == nullptr)
        {
            active = false;
            return;
        }

        const auto lengthAsDouble = static_cast<double> (sourceLength);
        const auto invDuration = 1.0f / static_cast<float> (totalDuration);
        const auto toRender = juce::jmin (numSamples, samplesRemaining);

        for (int i = 0; i < toRender; ++i)
        {
            const auto elapsed = static_cast<float> (totalDuration - samplesRemaining);
            const auto window = windowValue (elapsed * invDuration, taper);

            // Wrap the read head inside the sample bounds: the granular read loops
            // naturally rather than using loop points (04_SYNTH_ENGINE.md).
            if (readPosition >= lengthAsDouble || readPosition < 0.0)
                readPosition -= lengthAsDouble * std::floor (readPosition / lengthAsDouble);

            const auto position = static_cast<float> (readPosition);
            const auto l = dsp::readLinear (sourceLeft, sourceLength, position);
            const auto r = dsp::readLinear (sourceRight, sourceLength, position);

            const auto gain = window * level;

            leftOut[i]  += l * gain * leftGain;
            rightOut[i] += r * gain * rightGain;

            readPosition += increment;
            --samplesRemaining;
        }

        if (samplesRemaining <= 0)
            active = false;
    }
}
