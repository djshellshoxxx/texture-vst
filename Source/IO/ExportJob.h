#pragma once

#include "ExportPlanner.h"
#include "WavExporter.h"

#include <array>
#include <atomic>

namespace texture
{
    /** Everything a render needs, copied from the live patch on the message thread (F06).

        Immutable once built. Editing a control after the render starts cannot change
        it: the job reads only this copy and its own private engines and effects. */
    struct PatchSnapshot final
    {
        WavExporter::RenderSettings settings;
        std::array<SampleBufferPtr, 2> samples {};
    };

    /** Renders one plan to a WAV file. Runs on the calling thread, which must not be
        the audio thread. Cancellation is checked before every 512-sample block. */
    struct ExportJob final
    {
        static constexpr int renderBlockSize = 512;
        static constexpr int peakBins = 128;

        /** Samples at or beyond full scale in the first `numSamples` frames. Pure. */
        static int countFullScaleSamples (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

        static WavExporter::Result run (const PatchSnapshot& snapshot,
                                        const ExportPlanner::Plan& plan,
                                        double sampleRate,
                                        int bitDepth,
                                        const juce::File& destination,
                                        const std::atomic<bool>& cancel,
                                        std::atomic<juce::int64>& framesDone);
    };
}
