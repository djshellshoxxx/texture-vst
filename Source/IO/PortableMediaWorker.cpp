#include "PortableMediaWorker.h"

namespace texture
{
    PortableMediaWorker::PortableMediaWorker() = default;

    PortableMediaWorker::~PortableMediaWorker()
    {
        pool.removeAllJobs (true, -1);
    }

    bool PortableMediaWorker::enqueue (std::function<void()> job)
    {
        int expected = 0;

        // Refuse rather than queue a second request: the first one owns the library.
        if (! pending.compare_exchange_strong (expected, 1))
            return false;

        pool.addJob ([this, task = std::move (job)]
        {
            task();
            pending.store (0);
        });

        return true;
    }

    bool PortableMediaWorker::startExport (const juce::File& destination,
                                           const juce::File& stagingRoot,
                                           PresetManager::PresetData preset,
                                           std::array<juce::File, 2> layerFiles,
                                           ExportCallback done)
    {
        return enqueue ([destination, stagingRoot, presetData = std::move (preset), layerFiles, onDone = std::move (done)]
        {
            // Writing the archive needs no preset state, so a detached manager is enough.
            const PresetManager presets (nullptr);
            auto result = BundleArchive::write (destination, stagingRoot, presets, presetData, layerFiles);

            juce::MessageManager::callAsync ([onDone, outcome = std::move (result)]
            {
                if (onDone)
                    onDone (outcome);
            });
        });
    }

    bool PortableMediaWorker::startImport (const juce::File& archive,
                                           const juce::File& stagingRoot,
                                           const PresetManager& presets,
                                           const MediaStore& store,
                                           const juce::File& presetDirectory,
                                           ImportCallback done)
    {
        return enqueue ([archive, stagingRoot, &presets, store, presetDirectory, onDone = std::move (done)]
        {
            auto read = BundleArchive::read (archive, stagingRoot);
            const auto plan = BundleImportPlanner::plan (read, presets, store);

            BundleImportPlanner::CommitResult result;

            if (plan.status == BundleImportPlanner::Status::ok)
                result = BundleImportPlanner::commit (plan, presets, store, presetDirectory);
            else
                result.message = plan.message;

            // Failed or succeeded, the staged copies are no longer needed.
            BundleArchive::discardStaging (read.stagingDirectory);

            juce::MessageManager::callAsync ([onDone, outcome = std::move (result)]
            {
                if (onDone)
                    onDone (outcome);
            });
        });
    }

    bool PortableMediaWorker::startSearch (const juce::File& folder,
                                           MediaRelinker::Expectation expectation,
                                           SearchCallback done)
    {
        return enqueue ([folder, expectationCopy = std::move (expectation), onDone = std::move (done)]
        {
            static const std::atomic<bool> neverCancelled { false };
            auto result = MediaRelinker::search (folder, expectationCopy, neverCancelled);

            juce::MessageManager::callAsync ([onDone, outcome = std::move (result)]
            {
                if (onDone)
                    onDone (outcome);
            });
        });
    }
}
