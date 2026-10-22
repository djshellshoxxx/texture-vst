#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace texture
{
    /** Layout constants and archive-name policy for `.texturepack` bundles (F05).

        Pure: no file access, no processor, no GUI. Entry names are matched against
        exact patterns and never used to build a filesystem path, so a traversal,
        absolute, drive, UNC or symlink-style name cannot reach the disk: the reader
        writes only to files it names itself, using verified content hashes.
    */
    struct BundleFormat final
    {
        static constexpr const char* extension = ".texturepack";
        static constexpr const char* manifestEntry = "manifest.json";
        static constexpr const char* presetEntry = "preset.texture";
        static constexpr const char* mediaFolder = "media/";

        static constexpr int schemaVersion = 1;

        /** Limits from the F05 specification. Tests inject smaller values through
            BundleArchive::Limits; these are the production defaults. */
        static constexpr juce::int64 maxArchiveBytes = 256LL * 1024 * 1024;
        static constexpr juce::int64 maxExpandedBytes = 512LL * 1024 * 1024;
        static constexpr int maxEntries = 16;
        static constexpr juce::int64 maxManifestBytes = 64LL * 1024;
        /** The schema-2 preset ceiling. Schema 3 raises it to 256 KiB (00_SHARED_CONTRACTS). */
        static constexpr juce::int64 maxPresetBytes = 8LL * 1024;
        /** Matches SampleLoader::maxFileSizeBytes. */
        static constexpr juce::int64 maxMediaBytes = 100LL * 1024 * 1024;
        static constexpr int maxMediaEntries = 2;
        static constexpr int maxDecodedSecondsPerLayer = 30;   // SampleBuffer::maxLengthSeconds
        static constexpr int maxNameLength = 200;

        /** Audio formats the sample loader decodes, lower case, without the dot. */
        static bool isAllowedExtension (const juce::String& extension) noexcept;

        /** True for a relative path made only of printable ASCII, with no absolute,
            drive, UNC, backslash, empty, `.` or `..` components and no control
            characters. Length-bounded. */
        static bool isSafeRelativePath (const juce::String& path) noexcept;

        /** True for `media/<64 lowercase hex>.<allowed extension>` only. */
        static bool isMediaEntry (const juce::String& entry) noexcept;

        /** The canonical archive name for a media file of the given content hash. */
        static juce::String mediaEntryName (const juce::String& sha256, const juce::String& extension);

        /** True for exactly 64 lowercase hexadecimal characters. */
        static bool isSha256 (const juce::String& text) noexcept;
    };

    /** One audio file carried by a bundle. */
    struct BundleMediaEntry final
    {
        int layer { -1 };
        juce::String entryName;          // media/<sha256>.<ext>
        juce::String sha256;
        juce::int64 byteSize { 0 };
        juce::String originalName;       // display only; never used as a path
        int channels { 0 };
        int sampleRate { 0 };
        juce::int64 lengthFrames { 0 };
    };

    /** The manifest.json of a bundle. Parsing is strict: every field is checked and the
        first violation rejects the whole document. */
    struct MediaManifest final
    {
        enum class Status
        {
            ok,
            malformedJson,
            unsupportedSchema,
            tooManyMediaEntries,
            invalidLayer,
            duplicateLayer,
            invalidEntryName,
            invalidHash,
            invalidSize,
            invalidOriginalName,
            invalidFormat,
            tooLong
        };

        std::vector<BundleMediaEntry> media;

        static Status parse (const juce::String& json, MediaManifest& result, juce::String& errorMessage);
        juce::String toJson() const;
        static juce::String describe (Status status);

        /** The media entry carried for a layer, or nullptr when the layer is empty. */
        const BundleMediaEntry* findLayer (int layer) const noexcept;
    };
}
