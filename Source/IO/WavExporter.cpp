#include "WavExporter.h"

#include "ExportJob.h"
#include "ExportPlanner.h"

namespace texture
{
    juce::Array<int> WavExporter::getSupportedBitDepths()
    {
        return { 16, 24, 32 };
    }

    juce::Array<double> WavExporter::getSupportedSampleRates()
    {
        return { 44100.0, 48000.0, 88200.0, 96000.0 };
    }

    juce::String WavExporter::describeQuality (const Options& options)
    {
        return juce::String (options.sampleRate / 1000.0, 1) + " kHz / "
             + juce::String (options.bitDepth) + "-bit stereo WAV";
    }

    WavExporter::Result WavExporter::render (const std::array<SampleBufferPtr, 2>& samples,
                                             const RenderSettings& settings,
                                             const Options& options,
                                             const juce::File& destination)
    {
        // Legacy two-hit export: the same job the background exporter runs, with the
        // original schedule and no cancellation.
        PatchSnapshot snapshot;
        snapshot.settings = settings;
        snapshot.samples = samples;

        const auto plan = ExportPlanner::planLegacy (options.sampleRate, options.holdSeconds, options.tailSeconds,
                                                     options.gapSeconds,
                                                     static_cast<double> (settings.releaseMs) * 0.001,
                                                     settings.midiNote, settings.velocity);

        const std::atomic<bool> neverCancelled { false };
        std::atomic<juce::int64> framesDone { 0 };

        return ExportJob::run (snapshot, plan, options.sampleRate, options.bitDepth, destination,
                               neverCancelled, framesDone);
    }
}
