#pragma once

#include <algorithm>
#include <cstdint>

namespace texture::waveform
{
    struct SampleRange
    {
        int begin;
        int end;
    };

    // Partition the complete sample, including any remainder. For clips shorter
    // than the display, repeat each source frame across its proportional width.
    inline SampleRange binRange (int bin, int numSamples, int numBins) noexcept
    {
        if (numSamples <= 0 || numBins <= 0 || bin < 0 || bin >= numBins)
            return { 0, 0 };

        const auto start = static_cast<int> (static_cast<std::int64_t> (bin) * numSamples / numBins);
        const auto end = static_cast<int> ((static_cast<std::int64_t> (bin) + 1) * numSamples / numBins);
        return { start, std::min (numSamples, std::max (start + 1, end)) };
    }

    // Divide directly: the reciprocal of a tiny, finite peak can overflow.
    inline float normalise (float magnitude, float peak) noexcept
    {
        return peak > 0.0f ? magnitude / peak : 0.0f;
    }
}
