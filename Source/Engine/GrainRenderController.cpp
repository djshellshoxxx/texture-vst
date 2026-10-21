#include "GrainRenderController.h"

#include <cmath>

namespace texture
{
    void GrainRenderController::prepare (double newSampleRate) noexcept
    {
        sampleRate = (newSampleRate > 0.0 && std::isfinite (newSampleRate)) ? newSampleRate : 44100.0;
        clock.prepare (sampleRate);
        reset();
    }

    void GrainRenderController::reset() noexcept
    {
        clock.reset();
        events.clear();
        span = {};
    }

    void GrainRenderController::beginBlock (const RhythmSettings& rhythmSettings, bool shouldGateSetDuration,
                                            const TransportSpan& transport, int numSamples) noexcept
    {
        juce::ignoreUnused (numSamples);

        rhythm = rhythmSettings;
        gateSetsDuration = shouldGateSetDuration;
        blockTransport = transport;

        const auto valid = std::isfinite (transport.bpm) && transport.bpm > 0.0;
        blockTempoBpm = valid ? juce::jlimit (rhythm::minBpm, rhythm::maxBpm, transport.bpm)
                              : clock.getTempoBpm();
        blockTransport.bpm = blockTempoBpm;
    }

    const RhythmSpan& GrainRenderController::renderSpan (int offset, int length) noexcept
    {
        TransportSpan transport = blockTransport;

        // The span starts `offset` samples into the block; its beat position follows the tempo.
        if (transport.hasPpq)
            transport.ppq = blockTransport.ppq + static_cast<double> (offset) * blockTempoBpm / (60.0 * sampleRate);

        clock.generate (rhythm, transport, length, events);

        const auto stopped = transport.hasPlayhead && ! transport.playing;

        span.active = rhythm.rhythmic;
        span.clockRunning = ! stopped;
        span.gateSetsDuration = gateSetsDuration;
        span.firstGrainImmediate = rhythm.firstGrainImmediate;
        span.probability = static_cast<float> (juce::jlimit (0.0, 1.0, static_cast<double> (rhythm.probability)));
        span.events = rhythm.rhythmic ? &events : nullptr;

        // The first grain of a note takes one grid step divided by the burst, at the gate.
        const auto subInterval = clock.getGridIntervalSamples() / static_cast<double> (
            juce::jlimit (params::grainBurstMin, params::grainBurstMax, rhythm.burst));
        const auto gate = juce::jlimit (static_cast<double> (params::grainGateMin),
                                        static_cast<double> (params::grainGateMax),
                                        static_cast<double> (rhythm.gate));
        const auto minSamples = juce::jmax (1.0, rhythm::minGrainSeconds * sampleRate);
        const auto maxSamples = rhythm::maxGrainSeconds * sampleRate;
        span.immediateIntervalSamples = static_cast<float> (subInterval);
        span.immediateDurationSamples = static_cast<int> (juce::jlimit (minSamples, maxSamples,
                                                                       std::round (gate * subInterval)));
        return span;
    }
}
