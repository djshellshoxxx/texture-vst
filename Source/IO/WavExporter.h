#pragma once

#include "../Engine/SampleBuffer.h"

#include <array>

namespace texture
{
    /** Offline renderer that bounces the current patch to a stereo WAV file (Pro).

        It owns a private engine and effects chain rather than borrowing the live
        ones, so exporting never touches anything the audio thread is using and can
        run at a sample rate the host is not using.
    */
    class WavExporter
    {
    public:
        /** Everything the renderer needs to reproduce the current patch. */
        struct RenderSettings
        {
            float morph { 0.35f };
            float attackMs { 250.0f };
            float decayMs { 500.0f };
            float sustain { 0.8f };
            float releaseMs { 1200.0f };
            bool reverbOn { true };
            float gainDb { 0.0f };
            float lfoRateHz { 0.2f };
            float lfoDepth { 0.0f };
            float layerBlend { 0.0f };
            std::array<float, 2> sampleStart { 0.0f, 0.0f };
            float tuneSemitones { 0.0f };
            float reverseChance { 0.0f };
            float shimmerAmount { 0.0f };
            float gritBits { 12.0f };
            float gritDownsample { 4.0f };
            float gritMix { 0.0f };
            int midiNote { 60 };          // C3
            float velocity { 0.9f };
        };

        /** User-facing quality choices from the export dialog. */
        struct Options
        {
            double sampleRate { 48000.0 };
            int bitDepth { 24 };
            double holdSeconds { 1.5 };     // note held for the short hit
            double tailSeconds { 4.0 };     // extra tail on the second hit
            double gapSeconds { 0.5 };      // silence between the two hits
        };

        struct Result
        {
            bool succeeded { false };
            juce::File file;
            double lengthSeconds { 0.0 };
            juce::int64 fileSizeBytes { 0 };
            juce::String message;
        };

        /** Renders and writes the file. Call from the message thread or a background
            thread — never from the audio thread.
        */
        static Result render (const std::array<SampleBufferPtr, 2>& samples,
                              const RenderSettings& settings,
                              const Options& options,
                              const juce::File& destination);

        /** Compatibility overload for a one-layer render. */
        static Result render (const SampleBufferPtr& sample,
                              const RenderSettings& settings,
                              const Options& options,
                              const juce::File& destination)
        {
            return render (std::array<SampleBufferPtr, 2> { sample, nullptr }, settings, options, destination);
        }

        /** Bit depths offered in the export dialog. */
        static juce::Array<int> getSupportedBitDepths();

        /** Sample rates offered in the export dialog. */
        static juce::Array<double> getSupportedSampleRates();

        /** "48 kHz / 24-bit stereo WAV" — the string shown in the success popup. */
        static juce::String describeQuality (const Options& options);
    };
}
