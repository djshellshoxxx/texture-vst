#include "ModulationOscillator.h"

#include "../Utility/DspHelpers.h"

#include <algorithm>
#include <cmath>

namespace texture
{
    namespace
    {
        float clampUnit (float value) noexcept
        {
            return ! std::isfinite (value) ? 0.0f : juce::jlimit (-1.0f, 1.0f, value);
        }

        /** Slew moves a value by `fraction` of the difference over `slewSamples`, starting
            at the cycle boundary. Returns 1 at the boundary and 0 once the slew is done. */
        float remainingSlew (double elapsedSamples, double slewSamples) noexcept
        {
            if (! (slewSamples > 0.0) || ! std::isfinite (elapsedSamples) || elapsedSamples >= slewSamples)
                return 0.0f;

            return static_cast<float> (1.0 - std::max (0.0, elapsedSamples) / slewSamples);
        }
    }

    LfoShape lfoShapeFromIndex (int index) noexcept
    {
        switch (index)
        {
            case 1: return LfoShape::triangle;
            case 2: return LfoShape::stepRandom;
            case 3: return LfoShape::smoothRandom;
            case 4: return LfoShape::curve;
            default: return LfoShape::sine;
        }
    }

    LfoRetrigger lfoRetriggerFromIndex (int index) noexcept
    {
        switch (index)
        {
            case 1: return LfoRetrigger::free;
            case 2: return LfoRetrigger::firstNote;
            case 3: return LfoRetrigger::host;
            default: return LfoRetrigger::legacy;
        }
    }

    float randomCycleValue (std::uint64_t seed, std::int64_t cycle) noexcept
    {
        const auto draw = VersionedRandomStreams::unitFloat (seed, VersionedRandomStreams::Lane::modulation,
                                                             0, static_cast<std::uint64_t> (cycle));
        return draw * 2.0f - 1.0f;
    }

    float evaluateWaveform (const WaveformInput& input) noexcept
    {
        const auto p = std::isfinite (input.phase) ? juce::jlimit (0.0, 1.0, input.phase) : 0.0;
        const auto pf = static_cast<float> (p);

        switch (input.shape)
        {
            case LfoShape::sine:
                return clampUnit (std::sin (pf * dsp::twoPi));

            case LfoShape::triangle:
            {
                // Starts at zero, rises to +1 at a quarter, falls to -1 at three quarters.
                const auto value = pf < 0.25f ? 4.0f * pf
                                 : (pf < 0.75f ? 2.0f - 4.0f * pf : 4.0f * pf - 4.0f);
                return clampUnit (value);
            }

            case LfoShape::stepRandom:
            {
                const auto current = randomCycleValue (input.seed, input.cycle);
                const auto previous = randomCycleValue (input.seed, input.cycle - 1);
                const auto slew = remainingSlew (input.elapsedSamples, input.slewSamples);
                return clampUnit (current + (previous - current) * slew);
            }

            case LfoShape::smoothRandom:
            {
                const auto from = randomCycleValue (input.seed, input.cycle);
                const auto to = randomCycleValue (input.seed, input.cycle + 1);
                const auto eased = pf * pf * (3.0f - 2.0f * pf);
                return clampUnit (from + (to - from) * eased);
            }

            case LfoShape::curve:
            {
                if (input.curve == nullptr)
                    return 0.0f;

                const auto start = input.curve->evaluate (0.0f);
                const auto end = input.curve->evaluate (1.0f);
                const auto slew = remainingSlew (input.elapsedSamples, input.slewSamples);
                return clampUnit (input.curve->evaluate (pf) + (end - start) * slew);
            }
        }

        return 0.0f;
    }
}
