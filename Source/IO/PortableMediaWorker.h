#pragma once

#include "BundleArchive.h"
#include "BundleImportPlanner.h"
#include "MediaRelinker.h"
#include "MediaStore.h"
#include "../Presets/PresetManager.h"

#include <array>
#include <atomic>
#include <functional>

namespace texture
{
    /** Runs bundle export, bundle import and missing-sample search off the message
        thread (F05). One job at a time: a second request is refused while one is
        queued or running, so the user is told instead of two jobs racing for the
        same library.

        Results return on the message thread through MessageManager::callAsync. The
        owner must outlive any job it starts; the destructor waits for queued work.
        Callers capture a SafePointer in their callbacks when the GUI can go away.
    */
    class PortableMediaWorker final
    {
    public:
        using ExportCallback = std::function<void (const BundleArchive::WriteResult&)>;
        using ImportCallback = std::function<void (const BundleImportPlanner::CommitResult&)>;
        using SearchCallback = std::function<void (const MediaRelinker::Result&)>;

        PortableMediaWorker();
        ~PortableMediaWorker();

        PortableMediaWorker (const PortableMediaWorker&) = delete;
        PortableMediaWorker& operator= (const PortableMediaWorker&) = delete;

        bool isBusy() const noexcept { return pending.load() > 0; }

        /** The arguments are copied, so the caller's live state can change freely.
            Scratch files go to `stagingRoot` (plugin data), never beside the destination. */
        bool startExport (const juce::File& destination,
                          const juce::File& stagingRoot,
                          PresetManager::PresetData preset,
                          std::array<juce::File, 2> layerFiles,
                          ExportCallback done);

        /** Reads, validates, commits media to `store` and writes the preset into
            `presetDirectory`. `presets` must outlive the job. */
        bool startImport (const juce::File& archive,
                          const juce::File& stagingRoot,
                          const PresetManager& presets,
                          const MediaStore& store,
                          const juce::File& presetDirectory,
                          ImportCallback done);

        bool startSearch (const juce::File& folder,
                          MediaRelinker::Expectation expectation,
                          SearchCallback done);

    private:
        bool enqueue (std::function<void()> job);

        juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1)
                                                          .withThreadName ("Texture portable media") };
        std::atomic<int> pending { 0 };
    };
}
