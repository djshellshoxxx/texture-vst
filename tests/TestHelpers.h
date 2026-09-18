#pragma once

#include "../Source/Engine/SampleBuffer.h"

#include <cmath>
#include <memory>

namespace texture::tests
{
    /** A scratch directory that deletes itself, so tests never leave litter. */
    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
            : directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("TextureTests_" + juce::Uuid().toDashedString()))
        {
            directory.createDirectory();
        }

        ~TemporaryDirectory()
        {
            directory.deleteRecursively();
        }

        const juce::File& get() const noexcept { return directory; }

        juce::File child (const juce::String& name) const { return directory.getChildFile (name); }

    private:
        juce::File directory;
    };

    /** Builds an in-memory sine sample buffer at the given rate. */
    inline SampleBufferPtr makeTestSample (double sampleRate = 48000.0,
                                           double lengthSeconds = 1.0,
                                           double frequency = 220.0,
                                           int numChannels = 2)
    {
        const auto numFrames = juce::jmax (1, static_cast<int> (sampleRate * lengthSeconds));

        juce::AudioBuffer<float> buffer (juce::jmax (2, numChannels), numFrames);
        buffer.clear();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);

            for (int i = 0; i < numFrames; ++i)
                data[i] = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                           * static_cast<float> (frequency * i / sampleRate));
        }

        return std::make_shared<const SampleBuffer> (std::move (buffer), sampleRate,
                                                     juce::File(), numChannels, sampleRate, false);
    }

    /** Writes a sine wave to disk in whichever format the extension implies. */
    inline bool writeTestAudioFile (const juce::File& destination,
                                    double sampleRate,
                                    double lengthSeconds,
                                    int numChannels,
                                    int bitDepth = 16)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        auto* format = formats.findFormatForFileExtension (destination.getFileExtension());

        if (format == nullptr)
            return false;

        const auto numFrames = juce::jmax (1, static_cast<int> (sampleRate * lengthSeconds));

        juce::AudioBuffer<float> buffer (numChannels, numFrames);
        buffer.clear();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);

            for (int i = 0; i < numFrames; ++i)
                data[i] = 0.4f * std::sin (juce::MathConstants<float>::twoPi
                                           * static_cast<float> ((110.0 + ch * 55.0) * i / sampleRate));
        }

        destination.deleteFile();
        auto stream = destination.createOutputStream();

        if (stream == nullptr)
            return false;

        const auto depths = format->getPossibleBitDepths();
        const auto depth = depths.contains (bitDepth) ? bitDepth
                                                      : (depths.isEmpty() ? bitDepth : depths[0]);

        std::unique_ptr<juce::AudioFormatWriter> writer (
            format->createWriterFor (stream.get(), sampleRate, static_cast<unsigned int> (numChannels),
                                     depth, {}, 0));

        if (writer == nullptr)
            return false;

        stream.release();
        const auto ok = writer->writeFromAudioSampleBuffer (buffer, 0, numFrames);
        writer->flush();

        return ok;
    }

    /** Spins the message loop until `predicate` is true or the timeout elapses. */
    template <typename Predicate>
    inline bool waitForCondition (Predicate&& predicate, int timeoutMs = 10000)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);

        while (juce::Time::getMillisecondCounter() < deadline)
        {
            if (predicate())
                return true;

            // Pump the message queue so async callbacks actually land.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }

        return predicate();
    }

    /** True if every sample in the buffer is finite. */
    inline bool isFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite (data[i]))
                    return false;
        }

        return true;
    }

    /** Peak magnitude across every channel. */
    inline float peakOf (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

        return peak;
    }
}
