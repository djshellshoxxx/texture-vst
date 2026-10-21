#pragma once

#include "../Engine/SampleBuffer.h"
#include "../Modulation/ModulationCurve.h"
#include "../Utility/ParameterIDs.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace texture
{
    /** A complete sound: every registered parameter, both sample references with their
        start positions and file identities, and the saved random seed (F09).

        Media is shared by reference. Two snapshots that use the same loaded sample hold
        the same immutable buffer, so a variation never duplicates audio. */
    struct SoundSnapshot
    {
        std::array<float, static_cast<std::size_t> (params::numParameters)> parameters {};
        std::array<SampleBufferPtr, 2> media {};
        std::array<juce::File, 2> files {};
        std::array<juce::String, 2> originalNames {};
        std::array<float, 2> sampleStarts { 0.0f, 0.0f };
        std::uint64_t seed { 0 };
        ModulationCurve lfoCurve;   // F14: part of the sound, so variations keep it
    };

    /** Bytes of sample audio held by a set of references, each buffer counted once. */
    inline std::size_t uniqueMediaBytes (std::vector<const SampleBuffer*> references)
    {
        std::sort (references.begin(), references.end());
        references.erase (std::unique (references.begin(), references.end()), references.end());

        std::size_t bytes = 0;

        for (const auto* buffer : references)
            if (buffer != nullptr)
                bytes += static_cast<std::size_t> (buffer->getNumSamples())
                       * static_cast<std::size_t> (buffer->getNumChannels()) * sizeof (float);

        return bytes;
    }
}
