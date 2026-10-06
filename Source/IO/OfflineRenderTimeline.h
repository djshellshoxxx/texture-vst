#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace texture
{
    template <size_t NumEvents>
    struct OfflineRenderTimeline final
    {
        using Events = std::array<juce::int64, NumEvents>;

        static juce::int64 nextBoundary (juce::int64 position,
                                         juce::int64 blockEnd,
                                         const Events& events) noexcept
        {
            auto boundary = blockEnd;

            for (const auto event : events)
                if (event > position && event < boundary)
                    boundary = event;

            return boundary;
        }

        static bool hasEventAt (juce::int64 position, const Events& events) noexcept
        {
            for (const auto event : events)
                if (event == position)
                    return true;

            return false;
        }
    };
}
