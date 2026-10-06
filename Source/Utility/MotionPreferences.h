#pragma once

#include <atomic>

namespace texture
{
    /** Process-wide UI motion preference.

        This is intentionally independent of processor state: reduced motion is a
        user-interface preference shared by every open TEXTURE editor, not part of
        the sound or host project state.
    */
    class MotionPreferences final
    {
    public:
        static bool isReducedMotion() noexcept
        {
            return reducedMotion.load (std::memory_order_relaxed);
        }

        static void setReducedMotion (bool shouldReduce) noexcept
        {
            reducedMotion.store (shouldReduce, std::memory_order_relaxed);
        }

        /** Returns zero for optional animation when reduced motion is enabled. */
        static int animationDurationMs (int normalDurationMs) noexcept
        {
            return isReducedMotion() ? 0 : normalDurationMs;
        }

    private:
        inline static std::atomic<bool> reducedMotion { false };
    };
}
