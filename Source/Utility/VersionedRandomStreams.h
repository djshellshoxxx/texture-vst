#pragma once

#include <cstdint>
#include <array>
#include <string>

namespace texture
{
    /** Stable counter-based random lanes for repeatable note variation.

        Algorithm version 1 is intentionally independent of std::hash, platform RNGs,
        draw ordering in other features, and host block boundaries. Persist the seed
        and version with state; changing these constants requires a new version.
    */
    class VersionedRandomStreams final
    {
    public:
        static constexpr std::uint32_t algorithmVersion = 1;

        static constexpr std::uint64_t defaultSeed = UINT64_C (0x5445585455524531);

        /** Encodes exactly sixteen lowercase hexadecimal characters. */
        static std::string seedToHex (std::uint64_t seed)
        {
            constexpr char digits[] = "0123456789abcdef";
            std::string result (16, '0');
            for (int i = 15; i >= 0; --i)
            {
                result[static_cast<std::size_t> (i)] = digits[seed & 0x0fU];
                seed >>= 4;
            }
            return result;
        }

        /** Strict parser: malformed, short, and oversized values are rejected. */
        static bool parseSeedHex (const std::string& text, std::uint64_t& seed) noexcept
        {
            if (text.size() != 16)
                return false;
            std::uint64_t parsed = 0;
            for (const auto ch : text)
            {
                unsigned int digit = 0;
                if (ch >= '0' && ch <= '9') digit = static_cast<unsigned int> (ch - '0');
                else if (ch >= 'a' && ch <= 'f') digit = static_cast<unsigned int> (ch - 'a' + 10);
                else if (ch >= 'A' && ch <= 'F') digit = static_cast<unsigned int> (ch - 'A' + 10);
                else return false;
                parsed = (parsed << 4) | digit;
            }
            seed = parsed;
            return true;
        }

        enum class Lane : std::uint64_t
        {
            grainPosition = 0xA0761D6478BD642FULL,
            shimmer       = 0xE7037ED1A0B428DBULL,
            reverse       = 0x8EBC6AF09C88C6E3ULL,
            detune        = 0x589965CC75374CC3ULL,
            pan           = 0x1D8E4E27C47D124FULL,
            modulation    = 0xEB44ACCAB455D165ULL,
            effects       = 0x9E3779B97F4A7C15ULL
        };

        /** Returns a deterministic 64-bit value for one independent draw. */
        static constexpr std::uint64_t value (std::uint64_t seed, Lane lane,
                                              std::uint64_t noteEvent,
                                              std::uint64_t drawIndex) noexcept
        {
            auto x = seed ^ static_cast<std::uint64_t> (lane)
                          ^ (noteEvent * 0xD6E8FEB86659FD93ULL)
                          ^ (drawIndex * 0xA5A3564E27F8862FULL);
            x += 0x9E3779B97F4A7C15ULL;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
            return x ^ (x >> 31);
        }

        /** Uniform float in [0, 1), using the high 24 bits for stable conversion. */
        static constexpr float unitFloat (std::uint64_t seed, Lane lane,
                                          std::uint64_t noteEvent,
                                          std::uint64_t drawIndex) noexcept
        {
            return static_cast<float> (value (seed, lane, noteEvent, drawIndex) >> 40)
                 * (1.0f / 16777216.0f);
        }
    };
}
