#include "ExportController.h"

namespace texture
{
    ExportController::~ExportController()
    {
        cancel();
        pool.removeAllJobs (true, -1);
    }

    bool ExportController::start (Request request)
    {
        if (request.snapshot == nullptr || ! request.plan.valid || request.destination == juce::File())
            return false;

        auto expected = State::idle;

        if (! state.compare_exchange_strong (expected, State::running))
            return false;

        cancelRequested.store (false, std::memory_order_release);
        framesDone.store (0, std::memory_order_relaxed);
        framesTotal.store (request.plan.totalSamples, std::memory_order_relaxed);

        {
            const juce::ScopedLock sl (lock);
            activeDestination = request.destination;
            pending = {};
        }

        pool.addJob ([this, request = std::move (request)]
        {
            auto result = ExportJob::run (*request.snapshot, request.plan, request.sampleRate,
                                          request.bitDepth, request.destination,
                                          cancelRequested, framesDone);

            {
                const juce::ScopedLock sl (lock);
                pending = std::move (result);
            }

            state.store (State::finished, std::memory_order_release);
        });

        return true;
    }

    void ExportController::cancel() noexcept
    {
        cancelRequested.store (true, std::memory_order_release);
    }

    double ExportController::getProgress() const noexcept
    {
        const auto total = framesTotal.load (std::memory_order_relaxed);

        if (total <= 0)
            return 0.0;

        const auto done = framesDone.load (std::memory_order_relaxed);
        return juce::jlimit (0.0, 1.0, static_cast<double> (done) / static_cast<double> (total));
    }

    bool ExportController::takeResult (WavExporter::Result& result)
    {
        auto expected = State::finished;

        if (! state.compare_exchange_strong (expected, State::idle))
            return false;

        const juce::ScopedLock sl (lock);
        result = std::move (pending);
        pending = {};
        return true;
    }

    juce::File ExportController::getActiveDestination() const
    {
        const juce::ScopedLock sl (lock);
        return activeDestination;
    }
}
