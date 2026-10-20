#include "../Source/Engine/WaveformMath.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

int main()
{
    int failures = 0;
    const auto check = [&failures] (bool passed)
    {
        if (! passed)
            ++failures;
    };

    constexpr int bins = 512;
    // Every source frame must appear, with no duplication for clips >= 512 frames.
    for (const int length : { 1, 2, 511, 512, 513, 1023, 1024, 48001 })
    {
        std::vector<int> visits (static_cast<std::size_t> (length), 0);
        for (int bin = 0; bin < bins; ++bin)
        {
            const auto range = texture::waveform::binRange (bin, length, bins);
            check (range.begin >= 0 && range.end <= length && range.begin < range.end);
            for (int frame = range.begin; frame < range.end; ++frame)
                ++visits[static_cast<std::size_t> (frame)];
        }
        for (const auto count : visits)
            check (length >= bins ? count == 1 : count >= 1);
        if (length == 2)
            check (visits[0] == 256 && visits[1] == 256);
    }

    // Multiplication must not overflow for int-sized audio buffers.
    const auto last = texture::waveform::binRange (511, std::numeric_limits<int>::max(), bins);
    check (last.begin > 0 && last.end == std::numeric_limits<int>::max());
    check (texture::waveform::binRange (0, 0, bins).end == 0);
    check (texture::waveform::binRange (-1, 100, bins).end == 0);
    check (texture::waveform::binRange (bins, 100, bins).end == 0);
    check (texture::waveform::binRange (0, 100, 0).end == 0);

    const auto tiny = std::numeric_limits<float>::denorm_min();
    check (std::abs (texture::waveform::normalise (tiny, tiny) - 1.0f) < 1.0e-6f);
    check (std::isfinite (texture::waveform::normalise (0.0f, tiny)));
    check (std::abs (texture::waveform::normalise (0.25f, 0.5f) - 0.5f) < 1.0e-6f);
    check (std::isfinite (texture::waveform::normalise (0.0f, 0.0f)));

    std::cout << "Waveform math: " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
