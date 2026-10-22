#pragma once

#include "BundleArchive.h"
#include "MediaStore.h"
#include "../Presets/PresetManager.h"

#include <array>
#include <vector>

namespace texture
{
    /** Turns a validated bundle into a plan the processor can apply (F05).

        Planning checks that the preset and the manifest agree layer by layer and
        computes where each sample will live. It reads the preset but writes nothing,
        so a rejected bundle cannot change the patch or the library. Committing is a
        separate step that runs only after planning succeeds.
    */
    class BundleImportPlanner final
    {
    public:
        enum class Status
        {
            ok,
            readFailed,
            presetInvalid,
            layerMismatch
        };

        struct LayerMedia
        {
            int layer { -1 };
            juce::File staged;
            juce::String sha256;
            juce::String extension;
        };

        struct Plan
        {
            Status status { Status::readFailed };
            juce::String message;
            /** The bundle's preset, with sample paths rewritten to library locations. */
            PresetManager::PresetData preset;
            std::vector<LayerMedia> media;
        };

        static Plan plan (const BundleArchive::ReadResult& read,
                          const PresetManager& presets,
                          const MediaStore& store);

        struct CommitResult
        {
            bool succeeded { false };
            juce::File presetFile;
            juce::String message;
        };

        /** Copies every planned sample into the library, then writes the rewritten preset
            into `presetDirectory` under a name that does not overwrite an existing preset.
            Run off the message thread; touches the filesystem. */
        static CommitResult commit (const Plan& plan,
                                    const PresetManager& presets,
                                    const MediaStore& store,
                                    const juce::File& presetDirectory);

        static juce::String describe (Status status);
    };
}
