#include "SampleBuffer.h"

#include <cmath>

namespace texture
{
    SampleBuffer::SampleBuffer (juce::AudioBuffer<float>&& audioToOwn,
                                double sampleRateOfAudio,
                                const juce::File& sourceFile,
                                int originalChannelCount,
                                double originalSampleRate,
                                bool wasTruncated)
        : audio (std::move (audioToOwn)),
          sampleRate (sampleRateOfAudio > 0.0 ? sampleRateOfAudio : 44100.0),
          file (sourceFile),
          name (sourceFile.getFileName()),
          originalChannels (originalChannelCount),
          originalRate (originalSampleRate),
          truncated (wasTruncated)
    {
        if (name.isEmpty())
            name = "Untitled";

        computeWaveform();
    }

    const float* SampleBuffer::getChannel (int channel) const noexcept
    {
        const auto numChannels = audio.getNumChannels();

        if (numChannels <= 0)
            return nullptr;

        return audio.getReadPointer (juce::jlimit (0, numChannels - 1, channel));
    }

    double SampleBuffer::getLengthSeconds() const noexcept
    {
        return sampleRate > 0.0 ? static_cast<double> (audio.getNumSamples()) / sampleRate : 0.0;
    }

    void SampleBuffer::computeWaveform()
    {
        peaks.assign (static_cast<size_t> (waveformResolution), 0.0f);
        rms.assign (static_cast<size_t> (waveformResolution), 0.0f);
        globalPeak = 0.0f;

        const auto numSamples = audio.getNumSamples();
        const auto numChannels = audio.getNumChannels();

        if (numSamples <= 0 || numChannels <= 0)
            return;

        const auto samplesPerBin = juce::jmax (1, numSamples / waveformResolution);

        for (int bin = 0; bin < waveformResolution; ++bin)
        {
            const auto start = juce::jmin (numSamples - 1, bin * samplesPerBin);
            const auto end = juce::jmin (numSamples, start + samplesPerBin);

            float binPeak = 0.0f;
            double sumOfSquares = 0.0;
            int count = 0;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto* data = audio.getReadPointer (ch);

                for (int i = start; i < end; ++i)
                {
                    const auto v = data[i];

                    if (! std::isfinite (v))
                        continue;

                    binPeak = juce::jmax (binPeak, std::abs (v));
                    sumOfSquares += static_cast<double> (v) * static_cast<double> (v);
                    ++count;
                }
            }

            peaks[static_cast<size_t> (bin)] = binPeak;
            rms[static_cast<size_t> (bin)] = count > 0
                ? static_cast<float> (std::sqrt (sumOfSquares / static_cast<double> (count)))
                : 0.0f;

            globalPeak = juce::jmax (globalPeak, binPeak);
        }

        // Normalise for display only — the audio itself is never rescaled.
        if (globalPeak > 0.0f)
        {
            const auto inverse = 1.0f / globalPeak;

            for (auto& v : peaks)
                v *= inverse;

            for (auto& v : rms)
                v *= inverse;
        }
    }

    std::shared_ptr<const SampleBuffer> SampleBuffer::makeEmpty()
    {
        return std::make_shared<const SampleBuffer>();
    }
}
