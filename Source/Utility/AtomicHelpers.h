#pragma once

#include <atomic>
#include <cmath>

namespace texture
{
    /** Relaxed load — used on the audio thread where ordering does not matter
        because the value is a self-contained scalar snapshot.
    */
    template <typename T>
    inline T loadRelaxed (const std::atomic<T>& a) noexcept
    {
        return a.load (std::memory_order_relaxed);
    }

    /** Relaxed store counterpart to loadRelaxed. */
    template <typename T>
    inline void storeRelaxed (std::atomic<T>& a, T v) noexcept
    {
        a.store (v, std::memory_order_relaxed);
    }

    /** Atomically raises `a` to `v` if `v` is larger. Used for peak meters,
        which are written from the audio thread and read from the GUI timer.
    */
    inline void atomicMax (std::atomic<float>& a, float v) noexcept
    {
        float current = a.load (std::memory_order_relaxed);
        while (v > current && ! a.compare_exchange_weak (current, v, std::memory_order_relaxed))
        {
        }
    }

    /** A float that decays towards zero every time it is read by the GUI.
        Lets the audio thread publish a peak without any handshake.
    */
    class AtomicPeak
    {
    public:
        void push (float absValue) noexcept { atomicMax (value, absValue); }

        /** Reads and resets. Message thread only. */
        float readAndClear() noexcept { return value.exchange (0.0f, std::memory_order_relaxed); }

        void reset() noexcept { value.store (0.0f, std::memory_order_relaxed); }

    private:
        std::atomic<float> value { 0.0f };
    };

    /** Single-producer/single-consumer flag that latches until consumed. */
    class AtomicFlagLatch
    {
    public:
        void set() noexcept { flag.store (true, std::memory_order_release); }

        bool consume() noexcept { return flag.exchange (false, std::memory_order_acquire); }

        bool peek() const noexcept { return flag.load (std::memory_order_acquire); }

    private:
        std::atomic<bool> flag { false };
    };
}
