#include "ToneSpace.h"

#include "../Utility/DspHelpers.h"

#include <cmath>

namespace texture
{
    namespace
    {
        float finiteOr (float value, float fallback) noexcept
        {
            return std::isfinite (value) ? value : fallback;
        }

        float clampFinite (float value, float fallback, float minimum, float maximum) noexcept
        {
            return juce::jlimit (minimum, maximum, finiteOr (value, fallback));
        }

        float lerp (float a, float b, float t) noexcept
        {
            return a + (b - a) * t;
        }
    }

    ToneSpaceSettings sanitiseToneSpace (const ToneSpaceSettings& settings) noexcept
    {
        ToneSpaceSettings out;
        out.independent = settings.independent;
        out.hpHz = clampFinite (settings.hpHz, params::toneHpDefaultHz, params::toneHpMinHz, params::toneHpMaxHz);
        out.lpHz = clampFinite (settings.lpHz, params::toneLpDefaultHz, params::toneLpMinHz, params::toneLpMaxHz);
        out.filterOn = settings.filterOn;
        out.tiltDb = clampFinite (settings.tiltDb, params::toneTiltDefaultDb, params::toneTiltMinDb, params::toneTiltMaxDb);
        out.width = clampFinite (settings.width, params::spaceWidthDefault, params::spaceWidthMin, params::spaceWidthMax);
        out.reverbSend = clampFinite (settings.reverbSend, params::spaceReverbDefault, params::spaceReverbMin, params::spaceReverbMax);
        out.chorusAmount = clampFinite (settings.chorusAmount, params::spaceChorusDefault, params::spaceChorusMin, params::spaceChorusMax);
        out.drive = clampFinite (settings.drive, params::toneDriveDefault, params::toneDriveMin, params::toneDriveMax);
        return out;
    }

    ResolvedToneSpace ToneSpaceRouter::resolve (const MorphTargets& morph, const ToneSpaceSettings& rawSettings) noexcept
    {
        const auto settings = sanitiseToneSpace (rawSettings);

        ResolvedToneSpace out;

        if (! settings.independent)
        {
            // Follow MORPH: exactly the legacy derivation, with the filter and width bypassed.
            out.saturatorDrive = morph.saturatorDrive;
            out.chorusDepth = morph.chorusDepth;
            out.reverbSend = morph.reverbSend;
            out.highShelfTiltDb = morph.highShelfTiltDb;
            out.filterMix = 0.0f;
            out.hpHz = params::toneHpDefaultHz;
            out.lpHz = params::toneLpDefaultHz;
            out.width = 1.0f;
            return out;
        }

        // Independent: drive spans the same 0.1..0.35 saturator range MORPH uses, and
        // chorus spans 0..0.6, so the controls keep the legacy headroom.
        out.saturatorDrive = 0.1f + settings.drive * 0.25f;
        out.chorusDepth = settings.chorusAmount * 0.6f;
        out.reverbSend = settings.reverbSend;
        out.highShelfTiltDb = settings.tiltDb;
        out.filterMix = settings.filterOn ? 1.0f : 0.0f;
        out.hpHz = settings.hpHz;
        out.lpHz = settings.lpHz;
        out.width = settings.width;
        return out;
    }

    void ToneSpaceRouter::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        transitionTotal = transitionSeconds * sampleRate;
        smoothingCoefficientPerSample = 1.0 / (transitionTotal > 0.0 ? transitionTotal : 1.0);
    }

    void ToneSpaceRouter::reset (const MorphTargets& morph) noexcept
    {
        current = resolve (morph, ToneSpaceSettings {});
        transitionStart = current;
        transitionRemaining = 0.0;
        lastIndependent = false;
        logHp = std::log2 (static_cast<double> (current.hpHz));
        logLp = std::log2 (static_cast<double> (current.lpHz));
    }

    ResolvedToneSpace ToneSpaceRouter::advance (const MorphTargets& morph,
                                                const ToneSpaceSettings& rawSettings,
                                                int numSamples) noexcept
    {
        const auto settings = sanitiseToneSpace (rawSettings);
        const auto goal = resolve (morph, settings);

        if (settings.independent != lastIndependent)
        {
            lastIndependent = settings.independent;
            transitionStart = current;
            transitionRemaining = transitionTotal;
        }

        const auto samples = static_cast<double> (juce::jmax (0, numSamples));

        if (transitionRemaining > 0.0)
        {
            transitionRemaining -= samples;

            if (transitionRemaining <= 0.0)
            {
                // Completed: land exactly on the goal.
                transitionRemaining = 0.0;
                current = goal;
            }
            else
            {
                // Linear crossfade from the pre-change values across the transition.
                const auto progress = static_cast<float> (1.0 - transitionRemaining / transitionTotal);
                current.saturatorDrive = lerp (transitionStart.saturatorDrive, goal.saturatorDrive, progress);
                current.chorusDepth = lerp (transitionStart.chorusDepth, goal.chorusDepth, progress);
                current.reverbSend = lerp (transitionStart.reverbSend, goal.reverbSend, progress);
                current.highShelfTiltDb = lerp (transitionStart.highShelfTiltDb, goal.highShelfTiltDb, progress);
                current.filterMix = lerp (transitionStart.filterMix, goal.filterMix, progress);
                current.width = lerp (transitionStart.width, goal.width, progress);
                logHp = lerp (static_cast<float> (std::log2 (transitionStart.hpHz)),
                              static_cast<float> (std::log2 (goal.hpHz)), progress);
                logLp = lerp (static_cast<float> (std::log2 (transitionStart.lpHz)),
                              static_cast<float> (std::log2 (goal.lpHz)), progress);
                current.hpHz = static_cast<float> (std::exp2 (logHp));
                current.lpHz = static_cast<float> (std::exp2 (logLp));
            }

            return current;
        }

        if (! settings.independent)
        {
            // Settled Follow MORPH: no smoothing, bit-for-bit the legacy path.
            current = goal;
            logHp = std::log2 (static_cast<double> (goal.hpHz));
            logLp = std::log2 (static_cast<double> (goal.lpHz));
            return current;
        }

        // Settled Independent: continuous controls follow with a 20 ms one-pole ramp.
        const auto alpha = static_cast<float> (1.0 - std::exp (-samples * smoothingCoefficientPerSample));
        current.saturatorDrive += (goal.saturatorDrive - current.saturatorDrive) * alpha;
        current.chorusDepth += (goal.chorusDepth - current.chorusDepth) * alpha;
        current.reverbSend += (goal.reverbSend - current.reverbSend) * alpha;
        current.highShelfTiltDb += (goal.highShelfTiltDb - current.highShelfTiltDb) * alpha;
        current.filterMix += (goal.filterMix - current.filterMix) * alpha;
        current.width += (goal.width - current.width) * alpha;
        logHp += (std::log2 (static_cast<double> (goal.hpHz)) - logHp) * alpha;
        logLp += (std::log2 (static_cast<double> (goal.lpHz)) - logLp) * alpha;
        current.hpHz = static_cast<float> (std::exp2 (logHp));
        current.lpHz = static_cast<float> (std::exp2 (logLp));

        // A one-pole never quite arrives. Snap once it is inaudibly close, so an idle
        // filter mix reaches exactly zero and the bypass path becomes exact again.
        constexpr float settled = 1.0e-5f;
        const auto snap = [] (float& value, float goalValue)
        {
            if (std::abs (value - goalValue) < settled)
                value = goalValue;
        };
        snap (current.saturatorDrive, goal.saturatorDrive);
        snap (current.chorusDepth, goal.chorusDepth);
        snap (current.reverbSend, goal.reverbSend);
        snap (current.highShelfTiltDb, goal.highShelfTiltDb);
        snap (current.filterMix, goal.filterMix);
        snap (current.width, goal.width);
        if (std::abs (logHp - std::log2 (static_cast<double> (goal.hpHz))) < 1.0e-6)
            logHp = std::log2 (static_cast<double> (goal.hpHz));
        if (std::abs (logLp - std::log2 (static_cast<double> (goal.lpHz))) < 1.0e-6)
            logLp = std::log2 (static_cast<double> (goal.lpHz));
        current.hpHz = static_cast<float> (std::exp2 (logHp));
        current.lpHz = static_cast<float> (std::exp2 (logLp));
        return current;
    }
}
