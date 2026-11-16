#include "TestHelpers.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

#include "../Source/IO/BundleArchive.h"
#include "../Source/IO/BundleImportPlanner.h"
#include "../Source/IO/MediaStore.h"
#include "../Source/Utility/ParameterIDs.h"

#include <utility>
#include <vector>

namespace texture::tests
{
    namespace
    {
        using Entry = std::pair<juce::String, juce::MemoryBlock>;

        juce::File writeWav (const juce::File& file, double rate, int channels, int frames, float frequency)
        {
            file.deleteFile();
            juce::AudioBuffer<float> buffer (channels, frames);

            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < frames; ++i)
                    buffer.setSample (ch, i, 0.4f * std::sin (juce::MathConstants<float>::twoPi
                                                               * static_cast<float> (static_cast<double> (frequency) * static_cast<double> (i) / rate)));

            juce::WavAudioFormat format;
            auto* stream = new juce::FileOutputStream (file);
            std::unique_ptr<juce::AudioFormatWriter> writer (
                format.createWriterFor (stream, rate, static_cast<unsigned int> (channels), 16, {}, 0));
            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (buffer, 0, frames);

            return file;
        }

        juce::MemoryBlock readBytes (const juce::File& file)
        {
            juce::MemoryBlock block;
            file.loadFileAsData (block);
            return block;
        }

        void buildZip (const juce::File& file, const std::vector<Entry>& entries)
        {
            file.deleteFile();
            juce::ZipFile::Builder builder;

            for (const auto& entry : entries)
                builder.addEntry (new juce::MemoryInputStream (entry.second, true), 0, entry.first,
                                  juce::Time::getCurrentTime());

            juce::FileOutputStream out (file);
            builder.writeToStream (out, nullptr);
        }

        /** Preset text with one sample reference, as a bundle would carry it. */
        juce::String presetText (const juce::File& scratch, const juce::String& samplePath, const juce::String& sha)
        {
            PresetManager presets (nullptr);
            PresetManager::PresetData data;
            data.name = "Kick Pad";
            data.category = "Cinematic";
            data.samplePaths[0] = samplePath;
            data.mediaSha256[0] = sha;
            data.setParameter (params::morph, 0.5f);
            juce::String error;
            const auto file = scratch.getChildFile (juce::String ("preset-source") + PresetManager::fileExtension);
            (void) presets.save (file, data, error);
            return file.loadFileAsString();
        }

        /** The parts of one valid single-layer bundle, which tests then corrupt. */
        struct Parts
        {
            juce::File wav;
            juce::String sha;
            juce::String entryName;
            BundleMediaEntry media;
            juce::String manifest;
            juce::String preset;
        };

        Parts makeParts (const juce::File& scratch)
        {
            Parts parts;
            parts.wav = writeWav (scratch.getChildFile ("kick.wav"), 48000.0, 2, 4800, 110.0f);
            parts.sha = juce::SHA256 (parts.wav).toHexString();
            parts.entryName = BundleFormat::mediaEntryName (parts.sha, "wav");
            parts.media.layer = 0;
            parts.media.entryName = parts.entryName;
            parts.media.sha256 = parts.sha;
            parts.media.byteSize = parts.wav.getSize();
            parts.media.originalName = "kick.wav";
            parts.media.channels = 2;
            parts.media.sampleRate = 48000;
            parts.media.lengthFrames = 4800;

            MediaManifest manifest;
            manifest.media.push_back (parts.media);
            parts.manifest = manifest.toJson();
            parts.preset = presetText (scratch, parts.entryName, parts.sha);
            return parts;
        }

        std::vector<Entry> validEntries (const Parts& parts)
        {
            return { { BundleFormat::manifestEntry, juce::MemoryBlock (parts.manifest.toRawUTF8(), parts.manifest.getNumBytesAsUTF8()) },
                     { BundleFormat::presetEntry, juce::MemoryBlock (parts.preset.toRawUTF8(), parts.preset.getNumBytesAsUTF8()) },
                     { parts.entryName, readBytes (parts.wav) } };
        }

        BundleArchive::ReadResult readWith (const juce::File& archive, const juce::File& staging,
                                            BundleArchive::Limits limits = {})
        {
            return BundleArchive::read (archive, staging, limits);
        }

        int countImportFolders (const juce::File& root)
        {
            return root.findChildFiles (juce::File::findDirectories, false, "import-*").size();
        }
    }

    class BundleArchiveTests : public juce::UnitTest
    {
    public:
        BundleArchiveTests()
            : juce::UnitTest ("Portable sound bundles", "texture") {}

        void runTest() override
        {
            TemporaryDirectory temp;
            const auto staging = temp.child ("staging");
            const auto archive = temp.child ("bundle.texturepack");
            const auto parts = makeParts (temp.get());

            beginTest ("a valid bundle is staged and every audio file matches its manifest");
            buildZip (archive, validEntries (parts));
            auto read = readWith (archive, staging);
            expect (read.isOk(), read.message);
            expectEquals (static_cast<int> (read.mediaFiles.size()), 1);
            if (read.isOk() && read.mediaFiles.size() == 1)
            {
                expect (juce::SHA256 (read.mediaFiles[0]).toHexString() == parts.sha);
                expect (read.presetFile.existsAsFile());
                expect (read.stagingDirectory.isAChildOf (staging));
            }
            BundleArchive::discardStaging (read.stagingDirectory);
            expectEquals (countImportFolders (staging), 0);

            beginTest ("path traversal and absolute entry names are refused and write nothing outside staging");
            for (const auto* hostile : { "../escape.txt", "/tmp/escape.txt", "media/../../escape.wav",
                                         "C:/escape.txt", "media\\..\\escape.wav" })
            {
                auto entries = validEntries (parts);
                entries.push_back ({ hostile, juce::MemoryBlock ("x", 1) });
                buildZip (archive, entries);
                const auto result = readWith (archive, staging);
                expect (result.status == BundleArchive::Status::unexpectedEntry, juce::String (juce::CharPointer_UTF8 (hostile)));
                expect (! temp.child ("escape.txt").existsAsFile());
                expect (! temp.child ("escape.wav").existsAsFile());
            }
            expectEquals (countImportFolders (staging), 0);

            beginTest ("unexpected, duplicate and excessive entries are rejected");
            auto extra = validEntries (parts);
            extra.push_back ({ "readme.txt", juce::MemoryBlock ("x", 1) });
            buildZip (archive, extra);
            expect (readWith (archive, staging).status == BundleArchive::Status::unexpectedEntry);

            auto duplicates = validEntries (parts);
            duplicates.push_back ({ "Manifest.json", juce::MemoryBlock ("{}", 2) });
            buildZip (archive, duplicates);
            expect (readWith (archive, staging).status == BundleArchive::Status::duplicateEntry);

            auto crowded = validEntries (parts);
            for (int i = 0; i < 14; ++i)
                crowded.push_back ({ "extra-" + juce::String (i) + ".txt", juce::MemoryBlock ("x", 1) });
            buildZip (archive, crowded);
            expect (readWith (archive, staging).status == BundleArchive::Status::tooManyEntries);

            auto noPreset = validEntries (parts);
            noPreset.erase (noPreset.begin() + 1);
            buildZip (archive, noPreset);
            expect (readWith (archive, staging).status == BundleArchive::Status::presetMissing);

            auto noManifest = validEntries (parts);
            noManifest.erase (noManifest.begin());
            buildZip (archive, noManifest);
            expect (readWith (archive, staging).status == BundleArchive::Status::manifestInvalid);

            beginTest ("malformed manifests and missing or unreferenced audio are rejected");
            auto badManifest = parts;
            badManifest.manifest = "{\"schema_version\":1,\"media\":[{\"layer\":0}]}";
            buildZip (archive, validEntries (badManifest));
            expect (readWith (archive, staging).status == BundleArchive::Status::manifestInvalid);

            auto noMedia = validEntries (parts);
            noMedia.pop_back();
            buildZip (archive, noMedia);
            expect (readWith (archive, staging).status == BundleArchive::Status::mediaMissing);

            auto unreferenced = validEntries (parts);
            unreferenced.push_back ({ BundleFormat::mediaEntryName (juce::String::repeatedString ("a", 64), "wav"),
                                      juce::MemoryBlock ("x", 1) });
            buildZip (archive, unreferenced);
            expect (readWith (archive, staging).status == BundleArchive::Status::unreferencedMedia);

            beginTest ("content substitution, size and format disagreements are detected");
            auto substituted = validEntries (parts);
            auto bytes = readBytes (parts.wav);
            static_cast<juce::uint8*> (bytes.getData())[bytes.getSize() / 2] ^= 0x5a;
            substituted.back().second = bytes;
            buildZip (archive, substituted);
            const auto hashResult = readWith (archive, staging);
            expect (hashResult.status == BundleArchive::Status::mediaHashMismatch);
            expectEquals (countImportFolders (staging), 0);

            auto wrongSize = parts;
            wrongSize.media.byteSize = parts.media.byteSize + 1;
            MediaManifest sizeManifest;
            sizeManifest.media.push_back (wrongSize.media);
            wrongSize.manifest = sizeManifest.toJson();
            buildZip (archive, validEntries (wrongSize));
            expect (readWith (archive, staging).status == BundleArchive::Status::mediaSizeMismatch);

            auto wrongFrames = parts;
            wrongFrames.media.lengthFrames = 4801;
            MediaManifest framesManifest;
            framesManifest.media.push_back (wrongFrames.media);
            wrongFrames.manifest = framesManifest.toJson();
            buildZip (archive, validEntries (wrongFrames));
            expect (readWith (archive, staging).status == BundleArchive::Status::mediaFormatMismatch);

            beginTest ("size limits refuse oversized archives, entries and expansions");
            buildZip (archive, validEntries (parts));
            BundleArchive::Limits tinyArchive;
            tinyArchive.maxArchiveBytes = 64;
            expect (readWith (archive, staging, tinyArchive).status == BundleArchive::Status::archiveTooLarge);

            BundleArchive::Limits tinyExpansion;
            tinyExpansion.maxExpandedBytes = 1000;
            expect (readWith (archive, staging, tinyExpansion).status == BundleArchive::Status::expandedTooLarge);

            BundleArchive::Limits tinyManifest;
            tinyManifest.maxManifestBytes = 10;
            expect (readWith (archive, staging, tinyManifest).status == BundleArchive::Status::entryTooLarge);

            BundleArchive::Limits tinyPreset;
            tinyPreset.maxPresetBytes = 10;
            expect (readWith (archive, staging, tinyPreset).status == BundleArchive::Status::entryTooLarge);

            BundleArchive::Limits tinyMedia;
            tinyMedia.maxMediaBytes = 100;
            expect (readWith (archive, staging, tinyMedia).status == BundleArchive::Status::entryTooLarge);

            BundleArchive::Limits shortClips;
            shortClips.maxDecodedSeconds = 0;
            expect (readWith (archive, staging, shortClips).status == BundleArchive::Status::mediaFormatMismatch);

            beginTest ("damaged, truncated, missing and non-bundle files fail cleanly");
            expect (readWith (temp.child ("absent.texturepack"), staging).status == BundleArchive::Status::missingArchive);

            temp.child ("text.texturepack").replaceWithText ("this is not a zip file at all");
            expect (readWith (temp.child ("text.texturepack"), staging).status == BundleArchive::Status::notABundle);

            const auto whole = readBytes (archive);
            const auto truncated = temp.child ("truncated.texturepack");
            truncated.replaceWithData (whole.getData(), whole.getSize() / 2);
            expect (! readWith (truncated, staging).isOk());
            expectEquals (countImportFolders (staging), 0);

            beginTest ("writing produces a bundle that reads back, and failures leave the destination alone");
            const auto destination = temp.child ("written.texturepack");
            PresetManager presets (nullptr);
            PresetManager::PresetData preset;
            preset.name = "Written";
            preset.category = "Cinematic";
            preset.setParameter (params::morph, 0.25f);

            const auto written = BundleArchive::write (destination, staging, presets, preset,
                                                       { parts.wav, juce::File() });
            expect (written.status == BundleArchive::Status::ok, written.message);
            expectEquals (static_cast<int> (written.manifest.media.size()), 1);

            const auto roundTrip = readWith (destination, staging);
            expect (roundTrip.isOk(), roundTrip.message);
            BundleArchive::discardStaging (roundTrip.stagingDirectory);

            const auto before = readBytes (destination);
            const auto missingWrite = BundleArchive::write (destination, staging, presets, preset,
                                                            { temp.child ("gone.wav"), juce::File() });
            expect (missingWrite.status == BundleArchive::Status::sourceMissing);
            expect (readBytes (destination) == before, "a failed write must keep the existing bundle");

            const auto notAudio = temp.child ("notes.txt");
            notAudio.replaceWithText ("plain text");
            expect (BundleArchive::write (temp.child ("text-source.texturepack"), staging, presets, preset,
                                          { notAudio, juce::File() }).status == BundleArchive::Status::sourceUnsupported);

            expect (BundleArchive::write (temp.child ("wrong.zip"), staging, presets, preset,
                                          { parts.wav, juce::File() }).status == BundleArchive::Status::writeFailed);

            BundleArchive::Limits tinyWrite;
            tinyWrite.maxArchiveBytes = 64;
            const auto overflow = temp.child ("overflow.texturepack");
            expect (BundleArchive::write (overflow, staging, presets, preset,
                                          { parts.wav, juce::File() }, tinyWrite).status == BundleArchive::Status::outputTooLarge);
            expect (! overflow.existsAsFile());

            beginTest ("a two-layer bundle sharing one file stores the audio once");
            const auto twin = temp.child ("twin.texturepack");
            const auto twinWrite = BundleArchive::write (twin, staging, presets, preset, { parts.wav, parts.wav });
            expect (twinWrite.status == BundleArchive::Status::ok, twinWrite.message);
            const auto twinRead = readWith (twin, staging);
            expect (twinRead.isOk(), twinRead.message);
            expectEquals (static_cast<int> (twinRead.manifest.media.size()), 2);
            BundleArchive::discardStaging (twinRead.stagingDirectory);

            beginTest ("the import planner accepts a valid bundle and commits it to the library");
            const auto validRead = readWith (destination, staging);
            expect (validRead.isOk());
            const MediaStore library (temp.child ("library"));
            const auto library2 = temp.child ("presets");
            const auto planned = BundleImportPlanner::plan (validRead, presets, library);
            expect (planned.status == BundleImportPlanner::Status::ok, planned.message);

            const auto committed = BundleImportPlanner::commit (planned, presets, library, library2);
            expect (committed.succeeded, committed.message);
            if (committed.succeeded)
            {
                expect (committed.presetFile.existsAsFile());
                PresetManager::PresetData loaded;
                juce::String error;
                expect (presets.load (committed.presetFile, loaded, error) == PresetManager::Status::ok, error);
                expect (loaded.samplePaths[0] == library.fileFor (parts.sha, "wav").getFullPathName());
                expect (loaded.mediaSha256[0] == parts.sha);
                expect (library.fileFor (parts.sha, "wav").existsAsFile());
                expect (juce::SHA256 (library.fileFor (parts.sha, "wav")).toHexString() == parts.sha);
            }

            const auto again = BundleImportPlanner::commit (planned, presets, library, library2);
            expect (again.succeeded && again.presetFile != committed.presetFile,
                    "a second import must not overwrite the first preset");
            BundleArchive::discardStaging (validRead.stagingDirectory);

            beginTest ("the planner rejects a preset that disagrees with the manifest");
            const auto disagreeStaging = temp.child ("disagree");
            disagreeStaging.createDirectory();
            const auto disagreeing = presetText (disagreeStaging, "media/" + juce::String::repeatedString ("b", 64) + ".wav", parts.sha);

            BundleArchive::ReadResult fake;
            fake.status = BundleArchive::Status::ok;
            fake.manifest.media.push_back (parts.media);
            fake.mediaFiles.push_back (parts.wav);
            fake.presetFile = disagreeStaging.getChildFile (juce::String ("disagree") + PresetManager::fileExtension);
            fake.presetFile.replaceWithText (disagreeing);
            expect (BundleImportPlanner::plan (fake, presets, library).status == BundleImportPlanner::Status::layerMismatch);

            fake.presetFile.replaceWithText ("[1, 2, 3]");
            expect (BundleImportPlanner::plan (fake, presets, library).status == BundleImportPlanner::Status::presetInvalid);

            BundleArchive::ReadResult failed;
            failed.status = BundleArchive::Status::mediaHashMismatch;
            expect (BundleImportPlanner::plan (failed, presets, library).status == BundleImportPlanner::Status::readFailed);

            beginTest ("preset content hashes are optional, validated and round-trip");
            PresetManager::PresetData hashed;
            hashed.name = "Hashed";
            hashed.mediaSha256[1] = parts.sha;
            juce::String error;
            const auto hashedFile = temp.child (juce::String ("hashed") + PresetManager::fileExtension);
            expect (presets.save (hashedFile, hashed, error) == PresetManager::Status::ok, error);
            PresetManager::PresetData hashedLoaded;
            expect (presets.load (hashedFile, hashedLoaded, error) == PresetManager::Status::ok);
            expect (hashedLoaded.mediaSha256[1] == parts.sha && hashedLoaded.mediaSha256[0].isEmpty());

            hashed.mediaSha256[1] = "NOT-A-HASH";
            expect (presets.save (temp.child ("bad-hash.texture"), hashed, error) == PresetManager::Status::invalidData);

            temp.child ("legacy.texture").replaceWithText (
                "{\"schema_version\":2,\"preset_name\":\"Old\",\"samples\":[{\"path\":\"factory/a.wav\",\"start\":0.0}],"
                "\"parameters\":{}}");
            PresetManager::PresetData legacy;
            expect (presets.load (temp.child ("legacy.texture"), legacy, error) == PresetManager::Status::ok, error);
            expect (legacy.mediaSha256[0].isEmpty() && legacy.samplePaths[0] == "factory/a.wav");

            temp.child ("bad-sha.texture").replaceWithText (
                "{\"schema_version\":2,\"preset_name\":\"Bad\",\"samples\":[{\"path\":\"x.wav\",\"start\":0.0,\"sha256\":\"zz\"}],"
                "\"parameters\":{}}");
            expect (presets.load (temp.child ("bad-sha.texture"), legacy, error) == PresetManager::Status::invalidData);

            beginTest ("discarding staging removes only folders this class created");
            const auto keep = temp.child ("keep-me");
            keep.createDirectory();
            BundleArchive::discardStaging (keep);
            expect (keep.isDirectory(), "unrelated folders are never removed");
        }
    };

    static BundleArchiveTests bundleArchiveTests;
}
