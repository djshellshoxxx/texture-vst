#include "SamplePositionController.h"

#include <algorithm>
#include <cmath>

namespace texture
{
    namespace
    {
        constexpr double clampUnit (double value) noexcept
        {
            return ! std::isfinite (value) ? 0.0 : (value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value));
        }

        constexpr float clampFloat (float value, float low, float high, float fallback) noexcept
        {
            return ! std::isfinite (value) ? fallback : (value < low ? low : (value > high ? high : value));
        }

        /** True when two values differ. Ordered comparisons, to stay clear of -Wfloat-equal. */
        constexpr bool differs (float a, float b) noexcept
        {
            return a < b || a > b;
        }

        /** Jitter radius for a frozen centre: a quarter of the source at full spread. */
        constexpr float frozenJitterScale = 0.25f;
    }

    ScanMode scanModeFromIndex (int index) noexcept
    {
        switch (index)
        {
            case 1: return ScanMode::manual;
            case 2: return ScanMode::scan;
            default: return ScanMode::legacy;
        }
    }

    ScanSettings sanitiseScanSettings (const ScanSettings& settings) noexcept
    {
        ScanSettings result = settings;
        result.position = clampFloat (settings.position, params::scanPositionMin, params::scanPositionMax,
                                      params::scanPositionDefault);
        result.speed = clampFloat (settings.speed, params::scanSpeedMin, params::scanSpeedMax,
                                   params::scanSpeedDefault);
        result.freezeSpread = clampFloat (settings.freezeSpread, params::freezeSpreadMin, params::freezeSpreadMax,
                                          params::freezeSpreadDefault);
        return result;
    }

    void SamplePositionController::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        glideTotal = juce::jmax (1, juce::roundToInt (glideSeconds * sampleRate));
        reset (params::scanPositionDefault);
    }

    void SamplePositionController::reset (float savedPosition) noexcept
    {
        centre = clampUnit (static_cast<double> (savedPosition));
        goal = centre;
        glideStep = 0.0;
        glideRemaining = 0;
        frozen = false;
        synced = false;
        hasLastPosition = false;
    }

    void SamplePositionController::setGoal (double newGoal) noexcept
    {
        if (newGoal < goal || newGoal > goal)
        {
            goal = newGoal;
            glideRemaining = glideTotal;
            glideStep = (goal - centre) / static_cast<double> (glideTotal);
        }
    }

    void SamplePositionController::stepGlide (int numSamples) noexcept
    {
        if (glideRemaining <= 0)
            return;

        const auto used = juce::jmin (juce::jmax (0, numSamples), glideRemaining);
        centre += glideStep * static_cast<double> (used);
        glideRemaining -= used;

        if (glideRemaining == 0)
            centre = goal;
    }

    ScanState SamplePositionController::advance (const ScanSettings& requested, int numSamples,
                                                 double sourceLength, double legacyCentre,
                                                 float morphJitter) noexcept
    {
        const auto settings = sanitiseScanSettings (requested);
        const auto samples = juce::jmax (0, numSamples);
        const bool moving = settings.mode != ScanMode::legacy;

        ScanState state;

        if (! (sourceLength > 0.0) || ! std::isfinite (sourceLength))
            return state;

        // Freeze edges happen at the block boundary: the event boundary for this layer.
        if (settings.freeze && ! frozen)
        {
            frozen = true;
            synced = false;
            latchedCentre = clampUnit (moving ? centre : legacyCentre);
            latchedPosition = settings.position;
            centre = latchedCentre;
            glideRemaining = 0;
            goal = centre;

            state.latched = true;
            state.latchedCentre = latchedCentre;
        }
        else if (! settings.freeze && frozen)
        {
            frozen = false;
            glideRemaining = 0;
            goal = centre;

            // Legacy voices carry their own read heads, so they must move to the frozen centre.
            state.jumped = ! moving;
        }

        if (! frozen && ! moving)
        {
            // Legacy and not frozen: the centre follows the voices, so a later mode change
            // glides from where the sound is. On the block a freeze ends, the centre stays at
            // the frozen value so the voices can jump to it.
            if (! state.jumped)
                centre = clampUnit (legacyCentre);

            goal = centre;
            glideRemaining = 0;
            hasLastPosition = false;

            state.centre = centre;
            return state;
        }

        if (frozen)
        {
            if (! synced)
            {
                if (differs (settings.position, latchedPosition))
                    synced = true;     // the processor's write, or a user edit, arrived
                else
                    centre = latchedCentre;
            }

            if (synced)
                setGoal (clampUnit (static_cast<double> (settings.position)));
        }
        else if (settings.mode == ScanMode::manual)
        {
            setGoal (clampUnit (static_cast<double> (settings.position)));
        }
        else
        {
            // Scan: an edit of the position moves the centre directly; otherwise it advances.
            if (! hasLastPosition || differs (settings.position, lastPosition))
            {
                centre = clampUnit (static_cast<double> (settings.position));
                glideRemaining = 0;
                goal = centre;
            }

            // Speed is in source lengths per second, which is normalised centre per second.
            centre += static_cast<double> (settings.speed) * static_cast<double> (samples) / sampleRate;
            centre -= std::floor (centre);
            centre = clampUnit (centre);
            goal = centre;
        }

        hasLastPosition = true;
        lastPosition = settings.position;

        if (! (frozen && ! synced) && ! (settings.mode == ScanMode::scan && ! frozen))
            stepGlide (samples);

        state.active = true;
        state.centre = clampUnit (centre);
        state.jitter = frozen ? settings.freezeSpread * frozenJitterScale
                              : (std::isfinite (morphJitter) ? morphJitter : 0.0f);
        return state;
    }
}
