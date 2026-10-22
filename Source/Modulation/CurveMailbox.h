#pragma once

#include "ModulationCurve.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace texture
{
    /** Hands a curve from the message thread to the audio thread without locks (F14).

        A sequence lock: the single writer makes the sequence odd while it stores, and even
        again when done. The reader copies and retries if the sequence moved or was odd.
        Every field is an atomic, so there is no data race. Reads are bounded: after a fixed
        number of attempts the reader keeps its previous curve, which is always valid. */
    class CurveMailbox final
    {
    public:
        static constexpr int maxReadAttempts = 8;

        /** Message thread only, one writer. Publishes a valid curve. */
        void publish (const ModulationCurve& curve) noexcept
        {
            const auto before = sequence.load (std::memory_order_relaxed);
            sequence.store (before + 1, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_release);

            const auto count = curve.getNumPoints();

            for (int i = 0; i < count; ++i)
            {
                const auto& point = curve.getPoint (i);
                coordinates[static_cast<std::size_t> (2 * i)].store (point.x, std::memory_order_relaxed);
                coordinates[static_cast<std::size_t> (2 * i + 1)].store (point.y, std::memory_order_relaxed);
            }

            pointCount.store (count, std::memory_order_relaxed);
            sequence.store (before + 2, std::memory_order_release);
        }

        /** Audio thread. Returns false, leaving `out` alone, if no consistent copy was read. */
        bool tryRead (ModulationCurve& out) const noexcept
        {
            std::array<CurvePoint, ModulationCurve::maxPoints> copy {};

            for (int attempt = 0; attempt < maxReadAttempts; ++attempt)
            {
                const auto first = sequence.load (std::memory_order_acquire);

                if ((first & 1u) != 0u)
                    continue;

                const auto count = pointCount.load (std::memory_order_relaxed);

                if (count < 0 || count > ModulationCurve::maxPoints)
                    continue;

                for (int i = 0; i < count; ++i)
                {
                    copy[static_cast<std::size_t> (i)].x = coordinates[static_cast<std::size_t> (2 * i)].load (std::memory_order_relaxed);
                    copy[static_cast<std::size_t> (i)].y = coordinates[static_cast<std::size_t> (2 * i + 1)].load (std::memory_order_relaxed);
                }

                std::atomic_thread_fence (std::memory_order_acquire);

                if (sequence.load (std::memory_order_relaxed) != first)
                    continue;

                return ModulationCurve::fromPoints (copy.data(), count, out);
            }

            return false;
        }

    private:
        std::atomic<std::uint32_t> sequence { 0 };
        std::atomic<int> pointCount { 0 };
        std::array<std::atomic<float>, 2 * ModulationCurve::maxPoints> coordinates {};
    };
}
