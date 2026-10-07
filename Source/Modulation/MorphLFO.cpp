#include "MorphLFO.h"

#include "../Utility/DspHelpers.h"
#include "../Utility/ParameterIDs.h"

#include <cmath>

namespace texture
{
    void MorphLFO::prepare (double sampleRate)
    {
        preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
        recalculateEffectiveRate();
    }

    void MorphLFO::reset()
    {
        phase = 0.0;
        currentValue = 0.0f;
        wasPlaying = false;
    }

    void MorphLFO::setRateHz (float hz) noexcept
    {
        requestedRate = juce::jlimit (params::lfoRateMinHz, params::lfoRateMaxHz, hz);
        recalculateEffectiveRate();
    }

    void MorphLFO::setDepth (float newDepth) noexcept
    {
        depth = juce::jlimit (0.0f, 1.0f, newDepth);
    }

    void MorphLFO::setSyncEnabled (bool shouldSync) noexcept
    {
        syncEnabled = shouldSync;
        recalculateEffectiveRate();
    }

    void MorphLFO::setDivisionIndex (int index) noexcept
    {
        divisionIndex = juce::jlimit (0, params::numDivisions - 1, index);
        recalculateEffectiveRate();
    }

    void MorphLFO::recalculateEffectiveRate() noexcept
    {
        if (! syncEnabled)
        {
            effectiveRate = requestedRate;
            return;
        }

        const auto beats = params::divisionBeats[divisionIndex];
        const auto beatsPerSecond = juce::jmax (0.01, hostBpm) / 60.0;
        effectiveRate = static_cast<float> (beatsPerSecond / juce::jmax (0.0001, beats));
    }

    void MorphLFO::updateFromPlayHead (double bpm, bool isPlaying, double ppqPosition)
    {
        if (bpm > 0.0 && std::isfinite (bpm))
            hostBpm = bpm;

        recalculateEffectiveRate();

        if (! syncEnabled)
        {
            wasPlaying = isPlaying;
            return;
        }

        // When the host provides musical position, it is authoritative whether the
        // transport is playing or stopped. This makes stop, seek and loop jumps
        // deterministic. If PPQ is unavailable while playing, advanceBlock() falls
        // back to tempo-derived free-running motion. While stopped without PPQ, the
        // current phase is frozen rather than drifting behind the transport.
        const auto beats = params::divisionBeats[divisionIndex];

        if (beats > 0.0 && ppqPosition >= 0.0 && std::isfinite (ppqPosition))
        {
            const auto cycles = ppqPosition / beats;
            phase = cycles - std::floor (cycles);
        }

        wasPlaying = isPlaying;
    }

    float MorphLFO::advanceBlock (int numSamples) noexcept
    {
        currentValue = std::sin (static_cast<float> (phase) * dsp::twoPi);

        if (! syncEnabled || wasPlaying)
        {
            const auto increment = static_cast<double> (effectiveRate) / preparedSampleRate;
            phase += increment * static_cast<double> (juce::jmax (0, numSamples));
            phase -= std::floor (phase);
        }

        return currentValue;
    }

    float MorphLFO::applyTo (float automatedMorph) const noexcept
    {
        if (depth <= 0.0f)
            return juce::jlimit (0.0f, 1.0f, automatedMorph);

        // finalMorph = automated + lfo * depth * 0.5, soft-folded at the edges so a
        // large depth never produces an audible step (08_MODULATION.md).
        const auto summed = automatedMorph + currentValue * depth * 0.5f;

        return dsp::softFold (summed, 0.0f, 1.0f);
    }
}
