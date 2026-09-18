#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <vector>

namespace texture
{
    /** An immutable, ready-to-play stereo sample together with its metadata and a
        pre-computed waveform envelope.

        Instances are produced complete by SampleLoader on a background thread and
        published to the audio thread by an atomic shared_ptr swap. Once published,
        nothing mutates a SampleBuffer — that is what makes the swap safe.
    */
    class SampleBuffer
    {
    public:
        /** Number of peak/RMS pairs cached for the waveform display. */
        static constexpr int waveformResolution = 512;

        /** Hard ceiling from 10_CONSTRAINTS.md. */
        static constexpr double maxLengthSeconds = 30.0;

        SampleBuffer() = default;

        /** Takes ownership of already-resampled stereo audio at `sampleRate`. */
        SampleBuffer (juce::AudioBuffer<float>&& audioToOwn,
                      double sampleRateOfAudio,
                      const juce::File& sourceFile,
                      int originalChannelCount,
                      double originalSampleRate,
                      bool wasTruncated);

        /** True when there is at least one frame of audio to play. */
        bool isValid() const noexcept { return audio.getNumSamples() > 0; }

        const juce::AudioBuffer<float>& getAudio() const noexcept { return audio; }

        /** Read pointer for a channel, folding requests beyond the channel count. */
        const float* getChannel (int channel) const noexcept;

        int getNumSamples() const noexcept { return audio.getNumSamples(); }
        int getNumChannels() const noexcept { return audio.getNumChannels(); }

        double getSampleRate() const noexcept { return sampleRate; }
        double getLengthSeconds() const noexcept;

        const juce::File& getFile() const noexcept { return file; }
        const juce::String& getName() const noexcept { return name; }

        int getOriginalChannelCount() const noexcept { return originalChannels; }
        double getOriginalSampleRate() const noexcept { return originalRate; }
        bool wasTruncatedOnLoad() const noexcept { return truncated; }

        /** Normalised absolute peaks, `waveformResolution` entries, for the display. */
        const std::vector<float>& getWaveformPeaks() const noexcept { return peaks; }

        /** Normalised RMS values, same resolution, drawn as the inner body. */
        const std::vector<float>& getWaveformRms() const noexcept { return rms; }

        /** Peak magnitude across the whole file, used to normalise the display. */
        float getGlobalPeak() const noexcept { return globalPeak; }

        /** Builds an empty placeholder so the engine never holds a null pointer. */
        static std::shared_ptr<const SampleBuffer> makeEmpty();

    private:
        void computeWaveform();

        juce::AudioBuffer<float> audio;
        double sampleRate { 44100.0 };
        juce::File file;
        juce::String name;
        int originalChannels { 0 };
        double originalRate { 0.0 };
        bool truncated { false };
        float globalPeak { 0.0f };
        std::vector<float> peaks;
        std::vector<float> rms;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleBuffer)
    };

    /** Shared, const handle passed between threads. */
    using SampleBufferPtr = std::shared_ptr<const SampleBuffer>;
}
