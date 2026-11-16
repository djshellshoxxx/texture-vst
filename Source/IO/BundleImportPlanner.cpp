#include "BundleImportPlanner.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace texture
{
    namespace
    {
        juce::String safeFileStem (const juce::String& name)
        {
            auto stem = juce::File::createLegalFileName (name).trim();
            return stem.isEmpty() ? juce::String ("Imported sound") : stem.substring (0, 80);
        }
    }

    juce::String BundleImportPlanner::describe (Status status)
    {
        switch (status)
        {
            case Status::ok:             return "OK";
            case Status::readFailed:     return "The bundle could not be read.";
            case Status::presetInvalid:  return "The bundle preset is invalid.";
            case Status::layerMismatch:  return "The bundle preset and its audio files do not match.";
        }

        return "Unknown import status.";
    }

    BundleImportPlanner::Plan BundleImportPlanner::plan (const BundleArchive::ReadResult& read,
                                                         const PresetManager& presets,
                                                         const MediaStore& store)
    {
        Plan result;

        if (! read.isOk() || read.mediaFiles.size() != read.manifest.media.size())
        {
            result.status = Status::readFailed;
            result.message = read.message.isNotEmpty() ? read.message : describe (Status::readFailed);
            return result;
        }

        juce::String error;

        if (presets.load (read.presetFile, result.preset, error) != PresetManager::Status::ok)
        {
            result.status = Status::presetInvalid;
            result.message = error.isNotEmpty() ? error : describe (Status::presetInvalid);
            return result;
        }

        for (int layer = 0; layer < 2; ++layer)
        {
            const auto storedPath = result.preset.samplePaths[static_cast<size_t> (layer)];
            const auto* entry = read.manifest.findLayer (layer);

            if (entry == nullptr)
            {
                if (storedPath.isNotEmpty())
                {
                    result.status = Status::layerMismatch;
                    result.message = describe (Status::layerMismatch);
                    return result;
                }

                result.preset.samplePaths[static_cast<size_t> (layer)].clear();
                result.preset.mediaSha256[static_cast<size_t> (layer)].clear();
                continue;
            }

            if (storedPath != entry->entryName)
            {
                result.status = Status::layerMismatch;
                result.message = describe (Status::layerMismatch);
                return result;
            }

            // mediaFiles is parallel to the manifest, so the layer selects the staged file.
            juce::File staged;

            for (size_t i = 0; i < read.manifest.media.size(); ++i)
                if (read.manifest.media[i].layer == layer)
                    staged = read.mediaFiles[i];

            LayerMedia media;
            media.layer = layer;
            media.staged = staged;
            media.sha256 = entry->sha256;
            media.extension = entry->entryName.fromLastOccurrenceOf (".", false, false);
            result.media.push_back (media);

            result.preset.samplePaths[static_cast<size_t> (layer)]
                = store.fileFor (entry->sha256, media.extension).getFullPathName();
            result.preset.mediaSha256[static_cast<size_t> (layer)] = entry->sha256;
        }

        result.preset.samplePath = result.preset.samplePaths[0];
        result.status = Status::ok;
        return result;
    }

    BundleImportPlanner::CommitResult BundleImportPlanner::commit (const Plan& plan,
                                                                   const PresetManager& presets,
                                                                   const MediaStore& store,
                                                                   const juce::File& presetDirectory)
    {
        CommitResult result;

        if (plan.status != Status::ok)
        {
            result.message = plan.message.isNotEmpty() ? plan.message : describe (plan.status);
            return result;
        }

        for (const auto& media : plan.media)
        {
            const auto committed = store.commit (media.staged, media.sha256, media.extension);

            if (! committed.succeeded)
            {
                result.message = committed.message;
                return result;
            }
        }

        const auto category = safeFileStem (plan.preset.category);
        const auto directory = presetDirectory.getChildFile (category);
        const auto stem = safeFileStem (plan.preset.name);
        auto target = directory.getChildFile (stem + PresetManager::fileExtension);

        for (int suffix = 2; target.existsAsFile() && suffix < 1000; ++suffix)
            target = directory.getChildFile (stem + " " + juce::String (suffix) + PresetManager::fileExtension);

        if (target.existsAsFile())
        {
            result.message = "A preset with that name already exists.";
            return result;
        }

        juce::String error;

        if (presets.save (target, plan.preset, error) != PresetManager::Status::ok)
        {
            result.message = error.isNotEmpty() ? error : "The preset could not be saved.";
            return result;
        }

        result.succeeded = true;
        result.presetFile = target;
        return result;
    }
}
