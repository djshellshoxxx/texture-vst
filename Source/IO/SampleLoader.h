#pragma once

#include "../Engine/SampleBuffer.h"

#include <atomic>
#include <functional>

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

        enum class Severity { info, warning, error };

        struct LoadResult
        {
            bool succeeded { false };
            SampleBufferPtr buffer;
            juce::File file;
            juce::String message;                 // toast text, may be empty
            Severity severity { Severity::info };
        };

        /** Delivered on the message thread. */
        using Callback = std::function<void (const LoadResult&)>;

        SampleLoader();
        ~SampleLoader() override;

        /** Sets the sample rate that loaded audio is resampled to. */
        void prepare (double hostSampleRate);

        double getTargetSampleRate() const noexcept { return targetSampleRate; }

        void setCallback (Callback newCallback);

        /** Queues a load. Only the most recent request is delivered; earlier ones are
            decoded but discarded, so rapid drops cannot race each other.
        */
        void loadAsync (const juce::File& file);

        /** Blocks until queued work finishes. Message thread only; used on shutdown. */
        void waitForPendingLoads (int timeoutMs = 5000);

        /** Drops any queued or in-flight results without waiting. */
        void cancelPendingLoads();

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
        void deliver (LoadResult&& result, int generation);

        juce::AudioFormatManager formatManager;
        juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1)
                                                          .withThreadName ("Texture Sample Loader") };

        Callback callback;

        juce::CriticalSection resultLock;
        LoadResult pendingResult;
        bool hasPendingResult { false };

        std::atomic<int> requestGeneration { 0 };
        std::atomic<double> targetSampleRateAtomic { 44100.0 };
        double targetSampleRate { 44100.0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleLoader)
    };
}
