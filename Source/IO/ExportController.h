#pragma once

#include "ExportJob.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

namespace texture
{
    /** Owns the single background WAV render for an instance (F06).

        One job at a time. A second start is refused while a job runs or a finished
        result has not been collected, so the caller can offer cancel-and-restart.
        Progress is an atomic frame counter, read by the GUI at a modest rate; the
        worker never posts to the message thread per block. The result is kept here
        and survives the editor closing. Destruction cancels and joins the worker, so
        removing the plugin during a render is safe.
    */
    class ExportController final
    {
    public:
        enum class State
        {
            idle,
            running,
            finished     // a result is waiting to be collected
        };

        struct Request
        {
            std::shared_ptr<const PatchSnapshot> snapshot;
            ExportPlanner::Plan plan;
            double sampleRate { 48000.0 };
            int bitDepth { 24 };
            juce::File destination;
        };

        ExportController() = default;
        ~ExportController();

        ExportController (const ExportController&) = delete;
        ExportController& operator= (const ExportController&) = delete;

        /** False when a render is running, a result is uncollected, or the request is
            not usable. */
        bool start (Request request);

        /** Asks the running render to stop. It acknowledges within one block. */
        void cancel() noexcept;

        State getState() const noexcept { return state.load (std::memory_order_acquire); }
        bool isBusy() const noexcept { return getState() != State::idle; }

        /** Fraction of the active plan rendered, 0 to 1. */
        double getProgress() const noexcept;

        /** Returns true once, with the finished result, and returns the controller to idle. */
        bool takeResult (WavExporter::Result& result);

        juce::File getActiveDestination() const;

    private:
        std::atomic<State> state { State::idle };
        std::atomic<bool> cancelRequested { false };
        std::atomic<juce::int64> framesDone { 0 };
        std::atomic<juce::int64> framesTotal { 0 };

        mutable juce::CriticalSection lock;
        WavExporter::Result pending;
        juce::File activeDestination;

        // Last member: destroyed first, so it joins before the state it reads goes away.
        juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1)
                                                          .withThreadName ("Texture export") };
    };
}
