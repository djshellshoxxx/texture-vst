#pragma once

#include "../Engine/SampleBuffer.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <vector>
#include <array>

namespace texture
{
    /** Asynchronous sample loading.

        Decoding, resampling and waveform analysis all happen on a background thread;
        the finished SampleBuffer is handed to the message thread, which publishes it
        to the audio thread with a single atomic pointer store (05_SAMPLE_LOADER.md).
    */
    class SampleLoader : private juce::AsyncUpdater
    {
    public:
        /** Hard ceiling from 10_CONSTRAINTS.md. */
        static constexpr juce::int64 maxFileSizeBytes = 100 * 1024 * 1024;

        /** Maximum simultaneous decoded float-buffer memory used while converting
            one sample. Compressed files can expand far beyond their on-disk size.
        */
        static constexpr juce::int64 maxConversionWorkingBytes = 128 * 1024 * 1024;

        enum class Severity { info, warning, error };

        struct LoadResult
        {
            bool succeeded { false };
            SampleBufferPtr buffer;
            juce::File file;
            juce::String message;                 // toast text, may be empty
            Severity severity { Severity::info };
            int requestTag { 0 };                 // caller-defined lane, e.g. sample layer
        };

        /** Delivered on the message thread. */
        using Callback = std::function<void (const LoadResult&)>;

        SampleLoader();
        ~SampleLoader() override;

        /** Sets the sample rate that loaded audio is resampled to. */
        void prepare (double hostSampleRate);

        double getTargetSampleRate() const noexcept { return targetSampleRate; }

        void setCallback (Callback newCallback);

        /** Queues a load. Only the most recent request for the same tag is delivered;
            independent tags can complete without cancelling each other.
        */
        void loadAsync (const juce::File& file, int requestTag = 0);

        /** Blocks until queued work finishes. Message thread only; used on shutdown. */
        void waitForPendingLoads (int timeoutMs = 5000);

        /** Drops any queued or in-flight results without waiting. */
        void cancelPendingLoads();

        /** Invalidates only one caller-defined request lane. */
        void cancelPendingLoad (int requestTag);

        bool isBusy() const;

        /** Decodes on the calling thread. Used by tests and by preset loading. */
        LoadResult loadSynchronously (const juce::File& file);

        /** Wildcard for the file chooser, e.g. "*.wav;*.aiff;...". */
        juce::String getSupportedWildcard() const;

        bool isSupportedAudioFile (const juce::File& file) const;

        /** True for the plugin's own preset extension, which is routed elsewhere. */
        static bool isPresetFile (const juce::File& file);

        static constexpr const char* presetExtension = ".texture";

    private:
        class LoadJob;

        void handleAsyncUpdate() override;
        static constexpr int maxRequestTags = 8;
        static int sanitiseRequestTag (int requestTag) noexcept;
        void deliver (LoadResult&& result, int generation, int requestTag);

        juce::AudioFormatManager formatManager;
        juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1)
                                                          .withThreadName ("Texture Sample Loader") };

        Callback callback;

        juce::CriticalSection resultLock;
        std::vector<LoadResult> pendingResults;
        std::array<std::atomic<int>, maxRequestTags> requestGenerations {};
        std::atomic<double> targetSampleRateAtomic { 44100.0 };
        double targetSampleRate { 44100.0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleLoader)
    };
}
