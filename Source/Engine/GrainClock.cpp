#include "GrainClock.h"

#include <cmath>

namespace texture
{
    namespace
    {
        constexpr std::array<double, params::grainDivisionCount> divisionBeatTable {
            1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0
        };

        /** A start up to one sample before the position is emitted now, at offset zero. The
            loop stops at the first start whose rounded-up offset reaches the block end, so the
            next block can begin up to one sample after that start. Anything further back is a
            jump and is realigned instead. */
        constexpr double toleranceSamples = 1.0;

        /** Bounded search when realigning. Only a few grid points are ever skipped. */
        constexpr int realignGuard = 64;

        double clampTo (double value, double low, double high) noexcept
        {
            return value < low ? low : (value > high ? high : value);
        }

        int clampIndex (int value, int count) noexcept
        {
            return value < 0 ? 0 : (value >= count ? count - 1 : value);
        }
    }

    namespace rhythm
    {
        double divisionBeats (int index) noexcept
        {
            const auto safe = (index >= 0 && index < params::grainDivisionCount) ? index
                                                                              : params::grainDivisionDefault;
            return divisionBeatTable[static_cast<std::size_t> (safe)];
        }

        bool isRhythmParameterMutable (std::string_view id, bool rhythmic) noexcept
        {
            if (id == params::grainClock || id == params::grainDivision || id == params::grainFirstImmediate)
                return false;

            if (id == params::grainSwing || id == params::grainProbability
                || id == params::grainGate || id == params::grainBurst)
                return rhythmic;

            return true;   // not a rhythm control: this gate does not apply
        }
    }

    double GrainClock::gridPointBeats (std::int64_t n, double stepBeats, double swing) noexcept
    {
        const auto odd = (n % 2) != 0;
        return static_cast<double> (n) * stepBeats + (odd ? swing * stepBeats : 0.0);
    }

    double GrainClock::gridStepBeats (std::int64_t n, double stepBeats, double swing) noexcept
    {
        // An even point is fixed and the odd point after it is late, so the step is longer;
        // the odd point's step back to the next even point is shorter by the same amount.
        const auto odd = (n % 2) != 0;
        return odd ? stepBeats - swing * stepBeats : stepBeats + swing * stepBeats;
    }

    void GrainClock::prepare (double newSampleRate) noexcept
    {
        sampleRate = (newSampleRate > 0.0 && std::isfinite (newSampleRate)) ? newSampleRate : 44100.0;
        reset();
    }

    void GrainClock::reset() noexcept
    {
        freeBeats = 0.0;
        sampleCounter = 0;
        cursorValid = false;
        cursor = {};
        divisionInitialised = false;
        pendingDivision = -1;
        pendingSinceSample = 0;
        lastSwing = -1.0;
        lastBurst = 0;
        // lastBpm and lastGridSamples are kept: the tempo keeps its last valid value (F19).
    }

    double GrainClock::spawnBeats (const Cursor& point, double stepBeats, double swing, int burst) const noexcept
    {
        return gridPointBeats (point.n, stepBeats, swing)
             + static_cast<double> (point.k) * gridStepBeats (point.n, stepBeats, swing) / static_cast<double> (burst);
    }

    void GrainClock::advance (Cursor& point, int burst) const noexcept
    {
        if (++point.k >= burst)
        {
            point.k = 0;
            ++point.n;
        }
    }

    void GrainClock::realign (double fromBeats, double stepBeats, double swing, int burst) noexcept
    {
        Cursor point;
        point.n = static_cast<std::int64_t> (std::floor (fromBeats / stepBeats)) - 2;
        point.k = 0;

        for (int guard = 0; guard < realignGuard; ++guard)
        {
            if (spawnBeats (point, stepBeats, swing, burst) >= fromBeats)
                break;

            advance (point, burst);
        }

        cursor = point;
        cursorValid = true;
    }

    void GrainClock::generate (const RhythmSettings& settings, const TransportSpan& transport,
                               int numSamples, GrainEventList& out) noexcept
    {
        out.clear();

        if (numSamples <= 0)
            return;

        const auto bpm = (std::isfinite (transport.bpm) && transport.bpm > 0.0)
                             ? clampTo (transport.bpm, rhythm::minBpm, rhythm::maxBpm)
                             : lastBpm;
        lastBpm = bpm;

        const auto samplesPerBeat = 60.0 * sampleRate / bpm;
        const auto blockBeats = static_cast<double> (numSamples) / samplesPerBeat;
        const auto beatNow = (transport.hasPpq && std::isfinite (transport.ppq)) ? transport.ppq : freeBeats;
        const auto stopped = transport.hasPlayhead && ! transport.playing;

        // Division changes are held for 20 ms and then applied from a realigned grid (F16 scheduler
        // selection). The first setting after a reset applies at once.
        const auto requested = clampIndex (settings.division, params::grainDivisionCount);

        if (! divisionInitialised)
        {
            activeDivision = requested;
            divisionInitialised = true;
            pendingDivision = -1;
        }
        else if (requested == activeDivision)
        {
            pendingDivision = -1;
        }
        else
        {
            if (pendingDivision != requested)
            {
                pendingDivision = requested;
                pendingSinceSample = sampleCounter;
            }

            const auto holdSamples = static_cast<std::uint64_t> (std::llround (rhythm::transitionSeconds * sampleRate));

            if (sampleCounter - pendingSinceSample >= holdSamples)
            {
                activeDivision = requested;
                pendingDivision = -1;
                cursorValid = false;
            }
        }

        const auto step = rhythm::divisionBeats (activeDivision);
        const auto swing = (std::isfinite (settings.swing) ? clampTo (settings.swing, params::grainSwingMin, params::grainSwingMax)
                                                           : static_cast<double> (params::grainSwingDefault));
        const auto burst = juce::jlimit (params::grainBurstMin, params::grainBurstMax, settings.burst);
        const auto gate = std::isfinite (settings.gate)
                              ? clampTo (settings.gate, params::grainGateMin, params::grainGateMax)
                              : static_cast<double> (params::grainGateDefault);

        if (! juce::exactlyEqual (swing, lastSwing) || burst != lastBurst)
        {
            cursorValid = false;
            lastSwing = swing;
            lastBurst = burst;
        }

        lastGridSamples = step * samplesPerBeat;

        const auto running = settings.rhythmic && ! stopped;

        if (! running)
        {
            // Stopped or Legacy: create nothing, and realign when the grid is next used.
            cursorValid = false;
        }
        else
        {
            const auto tolerance = toleranceSamples / samplesPerBeat;

            if (cursorValid)
            {
                const auto first = spawnBeats (cursor, step, swing, burst);

                // Behind the position is a seek or loop; far ahead is a loop back. Both realign.
                if (first < beatNow - tolerance || first > beatNow + blockBeats + 2.0 * step)
                    cursorValid = false;
            }

            if (! cursorValid)
                realign (beatNow - tolerance, step, swing, burst);

            const auto minSamples = rhythm::minGrainSeconds * sampleRate;
            const auto maxSamples = rhythm::maxGrainSeconds * sampleRate;

            for (;;)
            {
                const auto beat = spawnBeats (cursor, step, swing, burst);
                const auto offsetAsReal = std::ceil ((beat - beatNow) * samplesPerBeat);

                if (offsetAsReal >= static_cast<double> (numSamples))
                    break;

                GrainEvent event;
                event.offset = offsetAsReal <= 0.0 ? 0 : static_cast<int> (offsetAsReal);
                event.gridIndex = cursor.n;
                event.subIndex = cursor.k;

                const auto interval = gridStepBeats (cursor.n, step, swing) * samplesPerBeat
                                    / static_cast<double> (burst);
                event.intervalSamples = static_cast<float> (interval);
                event.durationSamples = static_cast<int> (clampTo (std::round (gate * interval),
                                                                   juce::jmax (1.0, minSamples), maxSamples));

                // A full list drops the newest start and keeps the cursor moving, so timing
                // of the starts that do fit is unaffected.
                out.push (event);
                advance (cursor, burst);
            }
        }

        freeBeats = beatNow + blockBeats;
        sampleCounter += static_cast<std::uint64_t> (numSamples);
    }
}
