#pragma once

#include "MediaManifest.h"
#include "../Presets/PresetManager.h"

#include <array>
#include <vector>

namespace texture
{
    /** Byte and count limits. Production values come from BundleFormat; tests pass
        smaller values to exercise the rejection paths. */
    struct BundleArchiveLimits
    {
        juce::int64 maxArchiveBytes { BundleFormat::maxArchiveBytes };
        juce::int64 maxExpandedBytes { BundleFormat::maxExpandedBytes };
        int maxEntries { BundleFormat::maxEntries };
        juce::int64 maxManifestBytes { BundleFormat::maxManifestBytes };
        juce::int64 maxPresetBytes { BundleFormat::maxPresetBytes };
        juce::int64 maxMediaBytes { BundleFormat::maxMediaBytes };
        int maxDecodedSeconds { BundleFormat::maxDecodedSecondsPerLayer };
    };

    /** Reads and writes `.texturepack` sound bundles (F05).

        Reading never trusts the archive: entry names must match the fixed layout,
        sizes are enforced while streaming, every audio file is hashed and checked
        against the manifest, and the decoded header must match the manifest. Output
        is written only into a fresh staging folder under names the reader generates.
        Nothing is applied to the patch here; the caller decides after validation.

        Writing copies each source into staging first, so the bytes hashed are the
        bytes archived, and publishes the archive through AtomicFileWriter.
    */
    class BundleArchive final
    {
    public:
        using Limits = BundleArchiveLimits;

        enum class Status
        {
            ok,
            missingArchive,
            archiveTooLarge,
            notABundle,
            tooManyEntries,
            unexpectedEntry,
            symbolicLinkEntry,
            duplicateEntry,
            entryTooLarge,
            expandedTooLarge,
            corruptArchive,
            manifestInvalid,
            presetMissing,
            mediaMissing,
            unreferencedMedia,
            mediaSizeMismatch,
            mediaHashMismatch,
            mediaFormatMismatch,
            stagingFailed,
            sourceMissing,
            sourceTooLarge,
            sourceUnsupported,
            sourceChanged,
            writeFailed,
            outputTooLarge
        };

        struct ReadResult
        {
            Status status { Status::missingArchive };
            juce::String message;
            MediaManifest manifest;
            /** The per-import folder that holds everything below. Caller discards it
                with discardStaging() after the import is committed or abandoned. */
            juce::File stagingDirectory;
            juce::File presetFile;                 // staged preset.texture
            std::vector<juce::File> mediaFiles;    // staged, parallel to manifest.media

            bool isOk() const noexcept { return status == Status::ok; }
        };

        /** Validates and stages the archive. On failure the staging folder is removed. */
        static ReadResult read (const juce::File& archive,
                                const juce::File& stagingRoot,
                                const Limits& limits = {});

        struct WriteResult
        {
            Status status { Status::writeFailed };
            juce::String message;
            juce::int64 archiveBytes { 0 };
            MediaManifest manifest;
        };

        /** Writes `preset` and the audio in `layerFiles` (empty File for an empty layer)
            into a bundle at `destination`. The preset's sample paths and hashes are set
            by this call. Publishes atomically: a failure leaves `destination` as it was. */
        static WriteResult write (const juce::File& destination,
                                  const juce::File& stagingRoot,
                                  const PresetManager& presets,
                                  PresetManager::PresetData preset,
                                  const std::array<juce::File, 2>& layerFiles,
                                  const Limits& limits = {});

        static void discardStaging (const juce::File& stagingDirectory);

        static juce::String describe (Status status);
    };
}
