#pragma once

#include "ModulationCurve.h"
#include "../Utility/VersionedRandomStreams.h"

#include <cstdint>

namespace texture
{
    /** The MORPH LFO's waveform (F14). Stored as integers in host state and presets. */
    enum class LfoShape : std::uint8_t
    {
        sine = 0,           // the original LFO
        triangle,
        stepRandom,         // one random value per cycle, with slew
        smoothRandom,       // random values joined by smoothstep
        curve               // the user curve
    };

    /** What restarts the LFO cycle (F14). Stored as integers. */
    enum class LfoRetrigger : std::uint8_t
    {
        legacy = 0,         // the original behaviour: tempo sync follows the transport
        free,               // advances whenever audio runs, regardless of transport
        firstNote,          // restarts when the first key of a chord goes down
        host                // follows the host's musical position, including pre-roll
    };

    /** Unknown or out-of-range stored values fall back to the defaults, never to an extreme. */
    LfoShape lfoShapeFromIndex (int index) noexcept;
    LfoRetrigger lfoRetriggerFromIndex (int index) noexcept;

    /** Everything one waveform evaluation needs. Plain values. */
    struct WaveformInput
    {
        LfoShape shape { LfoShape::sine };
        double phase { 0.0 };                  // cycle position in [0, 1), already offset
        std::int64_t cycle { 0 };              // whole cycles since the last restart
        std::uint64_t seed { 0 };              // patch random seed (random shapes only)
        double elapsedSamples { 0.0 };         // samples since the start of this cycle
        double slewSamples { 0.0 };            // 0 for no slew
        const ModulationCurve* curve { nullptr };
    };

    /** A random cycle value in [-1, 1]. One draw per cycle from the modulation lane, so a
        given seed and cycle always give the same value on every platform. */
    float randomCycleValue (std::uint64_t seed, std::int64_t cycle) noexcept;

    /** Value of the shape at one instant. Pure: no state between calls. Finite, in [-1, 1].

        Sine and triangle are continuous. Step random holds one value per cycle; a change
        moves from the previous cycle's value over the slew time. Smooth random interpolates
        this cycle's value to the next with smoothstep, so it is continuous at the cycle
        boundary. Curve follows the user curve; a wrap from its end to its start is
        crossfaded over the slew time. */
    float evaluateWaveform (const WaveformInput& input) noexcept;
}
