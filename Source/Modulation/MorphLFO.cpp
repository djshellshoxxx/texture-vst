#include "MorphLFO.h"

#include "../Utility/DspHelpers.h"
#include "../Utility/ParameterIDs.h"

#include <cmath>

namespace texture
{
    namespace
    {
        constexpr float clampOffset (float fraction) noexcept
        {
            return ! std::isfinite (fraction) ? 0.0f
                 : (fraction < 0.0f ? 0.0f : (fraction >= 1.0f ? 0.0f : fraction));
        }
    }

    void MorphLFO::prepare (double sampleRate)
    {
        preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
        recalculateEffectiveRate();
    }

    void MorphLFO::reset()
    {
        phase = 0.0;
        cycle = 0;
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

    void MorphLFO::setRetrigger (LfoRetrigger newRetrigger) noexcept
    {
        retrigger = newRetrigger;
        recalculateEffectiveRate();
    }

    void MorphLFO::setPhaseOffset (float fraction) noexcept
    {
        phaseOffset = clampOffset (fraction);
    }

    void MorphLFO::setSlewMs (float milliseconds) noexcept
    {
        slewMs = ! std::isfinite (milliseconds) ? 0.0f
               : juce::jlimit (params::lfoSlewMsMin, params::lfoSlewMsMax, milliseconds);
    }

    void MorphLFO::recalculateEffectiveRate() noexcept
    {
        // Host retrigger always follows the tempo division, whether or not Sync is on.
        if (! syncEnabled && retrigger != LfoRetrigger::host)
        {
            effectiveRate = requestedRate;
            return;
        }

        const auto beats = params::divisionBeats[divisionIndex];
        const auto beatsPerSecond = juce::jmax (0.01, hostBpm) / 60.0;
        effectiveRate = static_cast<float> (beatsPerSecond / juce::jmax (0.0001, beats));
    }

    void MorphLFO::setPhaseFromCycles (double cycles) noexcept
    {
        const auto whole = std::floor (cycles);
        cycle = static_cast<std::int64_t> (whole);
        phase = cycles - whole;
    }

    void MorphLFO::updateFromPlayHead (double bpm, bool isPlaying, std::optional<double> ppqPosition)
    {
        if (bpm > 0.0 && std::isfinite (bpm))
            hostBpm = bpm;

        recalculateEffectiveRate();

        const auto beats = params::divisionBeats[divisionIndex];
        const bool hasPosition = ppqPosition.has_value() && std::isfinite (*ppqPosition);

        if (retrigger == LfoRetrigger::host)
        {
            // The host's position is authoritative, including negative pre-roll. With no
            // position, playing free-runs from tempo and stopped holds the current phase.
            if (hasPosition && beats > 0.0)
                setPhaseFromCycles (*ppqPosition / beats);

            wasPlaying = isPlaying;
            return;
        }

        // Free and First note ignore the host position: they advance whenever audio runs.
        if (retrigger != LfoRetrigger::legacy)
        {
            wasPlaying = isPlaying;
            return;
        }

        // Legacy. When the host provides musical position, it is authoritative whether the
        // transport is playing or stopped. This makes stop, seek and loop jumps
        // deterministic. If PPQ is unavailable while playing, advanceBlock() falls
        // back to tempo-derived free-running motion. While stopped without PPQ, the
        // current phase is frozen rather than drifting behind the transport.
        if (syncEnabled && beats > 0.0 && hasPosition && *ppqPosition >= 0.0)
            setPhaseFromCycles (*ppqPosition / beats);

        wasPlaying = isPlaying;
    }

    void MorphLFO::noteStarted() noexcept
    {
        if (retrigger != LfoRetrigger::firstNote)
            return;

        phase = 0.0;
        cycle = 0;
    }

    float MorphLFO::advanceBlock (int numSamples) noexcept
    {
        const auto samples = static_cast<double> (juce::jmax (0, numSamples));

        // The phase the waveform reads: the cycle position plus the user's offset.
        double shaped = phase;

        if (phaseOffset > 0.0f)
        {
            shaped += static_cast<double> (phaseOffset);
            shaped -= std::floor (shaped);
        }

        WaveformInput input;
        input.shape = shape;
        input.phase = shaped;
        input.cycle = cycle;
        input.seed = seed;
        input.elapsedSamples = shaped * preparedSampleRate / juce::jmax (1.0e-6, static_cast<double> (effectiveRate));
        input.slewSamples = static_cast<double> (slewMs) * 0.001 * preparedSampleRate;
        input.curve = &curve;

        currentValue = evaluateWaveform (input);

        bool advances = true;

        switch (retrigger)
        {
            case LfoRetrigger::legacy: advances = ! syncEnabled || wasPlaying; break;
            case LfoRetrigger::host:   advances = wasPlaying; break;
            case LfoRetrigger::free:
            case LfoRetrigger::firstNote: advances = true; break;
        }

        if (advances)
        {
            const auto increment = static_cast<double> (effectiveRate) / preparedSampleRate;
            const auto advanced = phase + increment * samples;
            const auto whole = std::floor (advanced);
            phase = advanced - whole;
            cycle += static_cast<std::int64_t> (whole);
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
