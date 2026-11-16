#include "BundleArchive.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

#include "AtomicFileWriter.h"

#include <map>
#include <memory>
#include <set>

namespace texture
{
    namespace
    {
        constexpr int copyChunkBytes = 64 * 1024;
        constexpr const char* importFolderPrefix = "import-";
        constexpr const char* exportFolderPrefix = "export-";

        /** Copies one archive entry to `out`, enforcing the per-entry cap and the running
            expanded total while streaming. Header sizes are checked by the caller, but
            they are not trusted: a compressed bomb is stopped by the byte count here. */
        BundleArchive::Status copyEntry (juce::ZipFile& zip, int index, juce::OutputStream& out,
                                         juce::int64 maxEntryBytes, juce::int64 maxExpandedBytes,
                                         juce::int64& expandedUsed, juce::int64& copiedBytes)
        {
            std::unique_ptr<juce::InputStream> input (zip.createStreamForEntry (index));

            if (input == nullptr)
                return BundleArchive::Status::corruptArchive;

            std::vector<char> buffer (static_cast<size_t> (copyChunkBytes));
            juce::int64 copied = 0;

            for (;;)
            {
                const auto got = input->read (buffer.data(), static_cast<int> (buffer.size()));

                if (got <= 0)
                    break;

                copied += got;
                expandedUsed += got;

                if (copied > maxEntryBytes)
                    return BundleArchive::Status::entryTooLarge;

                if (expandedUsed > maxExpandedBytes)
                    return BundleArchive::Status::expandedTooLarge;

                if (! out.write (buffer.data(), static_cast<size_t> (got)))
                    return BundleArchive::Status::stagingFailed;
            }

            copiedBytes = copied;
            return BundleArchive::Status::ok;
        }

        /** Confirms the decoded header agrees with what the manifest promised. */
        BundleArchive::Status verifyDecodedFormat (const juce::File& file,
                                                   const BundleMediaEntry& entry,
                                                   int maxSeconds)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

            if (reader == nullptr)
                return BundleArchive::Status::mediaFormatMismatch;

            const auto rate = juce::roundToInt (reader->sampleRate);

            if (static_cast<int> (reader->numChannels) != entry.channels
                || rate != entry.sampleRate
                || reader->lengthInSamples != entry.lengthFrames
                || reader->lengthInSamples > static_cast<juce::int64> (rate) * maxSeconds)
                return BundleArchive::Status::mediaFormatMismatch;

            return BundleArchive::Status::ok;
        }

        /** A display-only name: control characters and separators are removed. */
        juce::String sanitiseDisplayName (const juce::String& name)
        {
            juce::String cleaned;

            for (auto i = 0; i < name.length(); ++i)
            {
                const auto c = name[i];

                if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':')
                    continue;

                cleaned << juce::String::charToString (c);
            }

            cleaned = cleaned.trim();

            if (cleaned.isEmpty())
                cleaned = "audio";

            return cleaned.substring (0, 255);
        }

        struct DirectoryCleanup final
        {
            juce::File directory;

            ~DirectoryCleanup()
            {
                if (directory != juce::File())
                    BundleArchive::discardStaging (directory);
            }
        };
    }

    juce::String BundleArchive::describe (Status status)
    {
        switch (status)
        {
            case Status::ok:                    return "OK";
            case Status::missingArchive:        return "The bundle file could not be found.";
            case Status::archiveTooLarge:       return "The bundle is larger than 256 MiB.";
            case Status::notABundle:            return "This file is not a TEXTURE sound bundle.";
            case Status::tooManyEntries:        return "The bundle contains too many files.";
            case Status::unexpectedEntry:       return "The bundle contains a file that is not allowed.";
            case Status::symbolicLinkEntry:     return "The bundle contains a link, which is not allowed.";
            case Status::duplicateEntry:        return "The bundle lists the same file twice.";
            case Status::entryTooLarge:         return "A file in the bundle is larger than its limit.";
            case Status::expandedTooLarge:      return "The bundle expands to more than 512 MiB.";
            case Status::corruptArchive:        return "The bundle is damaged or incomplete.";
            case Status::manifestInvalid:       return "The bundle manifest is invalid.";
            case Status::presetMissing:         return "The bundle has no preset.";
            case Status::mediaMissing:          return "A sample listed in the bundle is missing from it.";
            case Status::unreferencedMedia:     return "The bundle contains a sample that nothing uses.";
            case Status::mediaSizeMismatch:     return "A sample in the bundle has the wrong size.";
            case Status::mediaHashMismatch:     return "A sample in the bundle does not match its content hash.";
            case Status::mediaFormatMismatch:   return "A sample in the bundle does not match its recorded format.";
            case Status::stagingFailed:         return "A temporary folder could not be used.";
            case Status::sourceMissing:         return "A sample file could not be found.";
            case Status::sourceTooLarge:        return "A sample file is larger than the bundle limit.";
            case Status::sourceUnsupported:     return "A sample file cannot be bundled (unsupported or longer than 30 s).";
            case Status::sourceChanged:         return "A sample file changed while it was being bundled.";
            case Status::writeFailed:           return "The bundle could not be written.";
            case Status::outputTooLarge:        return "The bundle would be larger than its limit.";
        }

        return "Unknown bundle status.";
    }

    BundleArchive::ReadResult BundleArchive::read (const juce::File& archive,
                                                   const juce::File& stagingRoot,
                                                   const Limits& limits)
    {
        ReadResult result;

        const auto fail = [&result] (Status status, const juce::String& message)
        {
            result.status = status;
            result.message = message;
            return result;
        };

        if (! archive.existsAsFile())
            return fail (Status::missingArchive, describe (Status::missingArchive));

        if (archive.getSize() > limits.maxArchiveBytes)
            return fail (Status::archiveTooLarge, describe (Status::archiveTooLarge));

        juce::ZipFile zip (archive);
        const auto numEntries = zip.getNumEntries();

        if (numEntries <= 0)
            return fail (Status::notABundle, describe (Status::notABundle));

        if (numEntries > limits.maxEntries)
            return fail (Status::tooManyEntries, describe (Status::tooManyEntries));

        // Pass 1: names, links, duplicates and declared sizes. Nothing is extracted yet.
        int manifestIndex = -1;
        int presetIndex = -1;
        std::map<juce::String, int> mediaIndices;
        std::set<juce::String> seenLowerCase;
        juce::int64 declaredTotal = 0;

        for (int index = 0; index < numEntries; ++index)
        {
            const auto* entry = zip.getEntry (index);

            if (entry == nullptr)
                return fail (Status::corruptArchive, describe (Status::corruptArchive));

            const auto name = entry->filename;

            if (entry->isSymbolicLink)
                return fail (Status::symbolicLinkEntry, describe (Status::symbolicLinkEntry));

            if (! BundleFormat::isSafeRelativePath (name))
                return fail (Status::unexpectedEntry, describe (Status::unexpectedEntry));

            if (! seenLowerCase.insert (name.toLowerCase()).second)
                return fail (Status::duplicateEntry, describe (Status::duplicateEntry));

            juce::int64 entryLimit = 0;

            if (name == BundleFormat::manifestEntry)
            {
                manifestIndex = index;
                entryLimit = limits.maxManifestBytes;
            }
            else if (name == BundleFormat::presetEntry)
            {
                presetIndex = index;
                entryLimit = limits.maxPresetBytes;
            }
            else if (BundleFormat::isMediaEntry (name))
            {
                mediaIndices[name] = index;
                entryLimit = limits.maxMediaBytes;
            }
            else
            {
                return fail (Status::unexpectedEntry, describe (Status::unexpectedEntry));
            }

            if (entry->uncompressedSize < 0 || entry->uncompressedSize > entryLimit)
                return fail (Status::entryTooLarge, describe (Status::entryTooLarge));

            declaredTotal += entry->uncompressedSize;

            if (declaredTotal > limits.maxExpandedBytes)
                return fail (Status::expandedTooLarge, describe (Status::expandedTooLarge));
        }

        if (manifestIndex < 0)
            return fail (Status::manifestInvalid, describe (Status::manifestInvalid));

        if (presetIndex < 0)
            return fail (Status::presetMissing, describe (Status::presetMissing));

        // Pass 2: the manifest, bounded while streaming.
        juce::int64 expandedUsed = 0;
        juce::int64 copied = 0;
        juce::MemoryOutputStream manifestBytes;

        if (const auto status = copyEntry (zip, manifestIndex, manifestBytes, limits.maxManifestBytes,
                                           limits.maxExpandedBytes, expandedUsed, copied);
            status != Status::ok)
            return fail (status, describe (status));

        juce::String manifestError;
        const auto parseStatus = MediaManifest::parse (manifestBytes.toUTF8(), result.manifest, manifestError);

        if (parseStatus != MediaManifest::Status::ok)
            return fail (Status::manifestInvalid, MediaManifest::describe (parseStatus));

        std::set<juce::String> referenced;

        for (const auto& entry : result.manifest.media)
        {
            if (mediaIndices.find (entry.entryName) == mediaIndices.end())
                return fail (Status::mediaMissing, describe (Status::mediaMissing));

            referenced.insert (entry.entryName);
        }

        for (const auto& archived : mediaIndices)
            if (referenced.find (archived.first) == referenced.end())
                return fail (Status::unreferencedMedia, describe (Status::unreferencedMedia));

        // Pass 3: stage into a fresh folder. Names come from the verified manifest.
        if (! stagingRoot.createDirectory())
            return fail (Status::stagingFailed, describe (Status::stagingFailed));

        result.stagingDirectory = stagingRoot.getChildFile (importFolderPrefix + juce::Uuid().toDashedString());

        if (! result.stagingDirectory.createDirectory())
            return fail (Status::stagingFailed, describe (Status::stagingFailed));

        DirectoryCleanup cleanup { result.stagingDirectory };

        const auto stageFail = [&result] (Status status)
        {
            result.status = status;
            result.message = describe (status);
            return result;
        };

        result.presetFile = result.stagingDirectory.getChildFile (BundleFormat::presetEntry);

        {
            juce::FileOutputStream out (result.presetFile);

            if (! out.openedOk())
                return stageFail (Status::stagingFailed);

            if (const auto status = copyEntry (zip, presetIndex, out, limits.maxPresetBytes,
                                               limits.maxExpandedBytes, expandedUsed, copied);
                status != Status::ok)
                return stageFail (status);

            out.flush();

            if (! out.getStatus().wasOk())
                return stageFail (Status::stagingFailed);
        }

        std::map<juce::String, juce::File> stagedByName;

        for (const auto& entry : result.manifest.media)
        {
            auto cached = stagedByName.find (entry.entryName);

            if (cached == stagedByName.end())
            {
                const auto leaf = entry.entryName.substring (juce::String (BundleFormat::mediaFolder).length());
                const auto file = result.stagingDirectory.getChildFile (leaf);

                {
                    juce::FileOutputStream out (file);

                    if (! out.openedOk())
                        return stageFail (Status::stagingFailed);

                    juce::int64 entryCopied = 0;

                    if (const auto status = copyEntry (zip, mediaIndices[entry.entryName], out,
                                                       limits.maxMediaBytes, limits.maxExpandedBytes,
                                                       expandedUsed, entryCopied);
                        status != Status::ok)
                        return stageFail (status);

                    out.flush();

                    if (! out.getStatus().wasOk())
                        return stageFail (Status::stagingFailed);
                }

                if (file.getSize() != entry.byteSize)
                    return stageFail (Status::mediaSizeMismatch);

                if (juce::SHA256 (file).toHexString() != entry.sha256)
                    return stageFail (Status::mediaHashMismatch);

                if (const auto status = verifyDecodedFormat (file, entry, limits.maxDecodedSeconds);
                    status != Status::ok)
                    return stageFail (status);

                cached = stagedByName.emplace (entry.entryName, file).first;
            }

            result.mediaFiles.push_back (cached->second);
        }

        cleanup.directory = juce::File();
        result.status = Status::ok;
        result.message.clear();
        return result;
    }

    BundleArchive::WriteResult BundleArchive::write (const juce::File& destination,
                                                     const juce::File& stagingRoot,
                                                     const PresetManager& presets,
                                                     PresetManager::PresetData preset,
                                                     const std::array<juce::File, 2>& layerFiles,
                                                     const Limits& limits)
    {
        WriteResult result;

        const auto fail = [&result] (Status status, const juce::String& message)
        {
            result.status = status;
            result.message = message.isNotEmpty() ? message : describe (status);
            return result;
        };

        if (destination == juce::File() || ! destination.hasFileExtension (BundleFormat::extension))
            return fail (Status::writeFailed, "Sound bundles are saved with the .texturepack extension.");

        if (! stagingRoot.createDirectory())
            return fail (Status::stagingFailed, {});

        const auto work = stagingRoot.getChildFile (exportFolderPrefix + juce::Uuid().toDashedString());

        if (! work.createDirectory())
            return fail (Status::stagingFailed, {});

        DirectoryCleanup cleanup { work };

        MediaManifest manifest;
        juce::int64 expandedBytes = 0;

        preset.samplePaths = {};
        preset.mediaSha256 = {};
        preset.samplePath.clear();

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        for (int layer = 0; layer < 2; ++layer)
        {
            const auto& source = layerFiles[static_cast<size_t> (layer)];

            if (source == juce::File())
                continue;

            if (! source.existsAsFile())
                return fail (Status::sourceMissing, "The sample for layer " + juce::String (layer + 1)
                                                    + " could not be found.");

            const auto size = source.getSize();

            if (size < 1 || size > limits.maxMediaBytes)
                return fail (Status::sourceTooLarge, {});

            const auto extension = source.getFileExtension().fromFirstOccurrenceOf (".", false, false).toLowerCase();

            if (! BundleFormat::isAllowedExtension (extension))
                return fail (Status::sourceUnsupported, {});

            // Copy first: the bytes hashed below are the bytes that will be archived.
            const auto snapshot = work.getChildFile ("layer-" + juce::String (layer) + ".part");

            if (! source.copyFileTo (snapshot) || snapshot.getSize() != size)
                return fail (snapshot.existsAsFile() ? Status::sourceChanged : Status::stagingFailed, {});

            const auto hash = juce::SHA256 (snapshot).toHexString();
            const auto entryName = BundleFormat::mediaEntryName (hash, extension);
            const auto leaf = work.getChildFile (entryName.substring (juce::String (BundleFormat::mediaFolder).length()));

            if (leaf.existsAsFile())
                snapshot.deleteFile();
            else if (! snapshot.moveFileTo (leaf))
                return fail (Status::stagingFailed, {});

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (leaf));

            if (reader == nullptr)
                return fail (Status::sourceUnsupported, "The sample for layer " + juce::String (layer + 1)
                                                        + " could not be decoded.");

            BundleMediaEntry entry;
            entry.layer = layer;
            entry.entryName = entryName;
            entry.sha256 = hash;
            entry.byteSize = size;
            entry.originalName = sanitiseDisplayName (source.getFileName());
            entry.channels = static_cast<int> (reader->numChannels);
            entry.sampleRate = juce::roundToInt (reader->sampleRate);
            entry.lengthFrames = reader->lengthInSamples;
            reader.reset();

            if (entry.channels < 1 || entry.channels > 2 || entry.sampleRate < 8000 || entry.sampleRate > 384000
                || entry.lengthFrames < 1
                || entry.lengthFrames > static_cast<juce::int64> (entry.sampleRate) * limits.maxDecodedSeconds)
                return fail (Status::sourceUnsupported, {});

            manifest.media.push_back (entry);
            preset.samplePaths[static_cast<size_t> (layer)] = entryName;
            preset.mediaSha256[static_cast<size_t> (layer)] = hash;
            expandedBytes += size;
        }

        preset.samplePath = preset.samplePaths[0];

        const auto manifestText = manifest.toJson();
        const auto manifestFile = work.getChildFile (BundleFormat::manifestEntry);

        if (! manifestFile.replaceWithText (manifestText))
            return fail (Status::stagingFailed, {});

        const auto presetFile = work.getChildFile (BundleFormat::presetEntry);
        juce::String presetError;

        if (presets.save (presetFile, preset, presetError) != PresetManager::Status::ok)
            return fail (Status::writeFailed, presetError);

        expandedBytes += manifestFile.getSize() + presetFile.getSize();

        if (expandedBytes > limits.maxExpandedBytes)
            return fail (Status::outputTooLarge, {});

        juce::ZipFile::Builder builder;
        builder.addFile (manifestFile, 6, BundleFormat::manifestEntry);
        builder.addFile (presetFile, 6, BundleFormat::presetEntry);

        std::set<juce::String> added;

        for (const auto& entry : manifest.media)
        {
            if (! added.insert (entry.entryName).second)
                continue;

            builder.addFile (work.getChildFile (entry.entryName.substring (juce::String (BundleFormat::mediaFolder).length())),
                             6, entry.entryName);
        }

        AtomicFileWriter writer (destination);
        juce::String error;
        auto stream = writer.openStream (error);

        if (stream == nullptr)
            return fail (Status::writeFailed, error);

        const auto written = builder.writeToStream (*stream, nullptr);
        stream->flush();
        const auto flushed = stream->getStatus().wasOk();
        stream.reset();

        if (! written || ! flushed)
        {
            writer.abandon();
            return fail (Status::writeFailed, {});
        }

        const auto archiveBytes = writer.getTemporaryFile().getSize();

        if (archiveBytes > limits.maxArchiveBytes)
        {
            writer.abandon();
            return fail (Status::outputTooLarge, {});
        }

        if (writer.commit (archiveBytes, error) != AtomicFileWriter::Status::ok)
            return fail (Status::writeFailed, error);

        result.status = Status::ok;
        result.archiveBytes = archiveBytes;
        result.manifest = manifest;
        return result;
    }

    void BundleArchive::discardStaging (const juce::File& stagingDirectory)
    {
        // Only folders this class created may be removed.
        const auto name = stagingDirectory.getFileName();

        if (stagingDirectory.isDirectory()
            && (name.startsWith (importFolderPrefix) || name.startsWith (exportFolderPrefix)))
            stagingDirectory.deleteRecursively();
    }
}
