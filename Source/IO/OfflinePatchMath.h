#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace texture
{
    struct OfflinePatchMath final
    {
        static std::array<float, 2> layerGains (float requestedBlend) noexcept
        {
            const auto blend = juce::jlimit (0.0f, 1.0f, requestedBlend);
            const auto normaliser = 1.0f / std::sqrt (1.0f + blend);
            return { normaliser, blend * normaliser };
        }
    };
}
