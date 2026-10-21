#include "TestHelpers.h"

#include <juce_cryptography/juce_cryptography.h>

#include "../Source/IO/AtomicFileWriter.h"
#include "../Source/IO/MediaManifest.h"
#include "../Source/IO/MediaRelinker.h"
#include "../Source/IO/MediaStore.h"

#include <atomic>

namespace texture::tests
{
    namespace
    {
        const juce::String hashA = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        const juce::String hashB = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

        /** One manifest media item. Every field is a JSON fragment, so a test can inject
            a wrong type as easily as a wrong value. */
        juce::String item (const juce::String& layer = "0",
                           const juce::String& entry = "\"media/" + hashA + ".wav\"",
                           const juce::String& sha = "\"" + hashA + "\"",
                           const juce::String& bytes = "1024",
                           const juce::String& name = "\"kick.wav\"",
                           const juce::String& channels = "2",
                           const juce::String& rate = "48000",
                           const juce::String& frames = "4800")
        {
            return "{\"layer\":" + layer + ",\"entry\":" + entry + ",\"sha256\":" + sha
                 + ",\"bytes\":" + bytes + ",\"original_name\":" + name + ",\"channels\":" + channels
                 + ",\"sample_rate\":" + rate + ",\"frames\":" + frames + "}";
        }

        juce::String document (const juce::String& schema, const juce::String& media)
        {
            return "{\"schema_version\":" + schema + ",\"media\":[" + media + "]}";
        }

        MediaManifest::Status parseStatus (const juce::String& json)
        {
            MediaManifest manifest;
            juce::String error;
            return MediaManifest::parse (json, manifest, error);
        }
    }

    class MediaManifestTests : public juce::UnitTest
    {
    public:
        MediaManifestTests()
            : juce::UnitTest ("Portable media manifest", "texture") {}

        void runTest() override
        {
            beginTest ("path policy accepts plain names and rejects traversal, absolute and drive forms");
            expect (BundleFormat::isSafeRelativePath ("media/kick.wav"));
            expect (BundleFormat::isSafeRelativePath ("manifest.json"));
            expect (! BundleFormat::isSafeRelativePath (""));
            expect (! BundleFormat::isSafeRelativePath ("../evil.wav"));
            expect (! BundleFormat::isSafeRelativePath ("media/../../evil.wav"));
            expect (! BundleFormat::isSafeRelativePath ("/etc/passwd"));
            expect (! BundleFormat::isSafeRelativePath ("C:/Windows/x.wav"));
            expect (! BundleFormat::isSafeRelativePath ("C:evil.wav"));
            expect (! BundleFormat::isSafeRelativePath ("\\\\server\\share\\x.wav"));
            expect (! BundleFormat::isSafeRelativePath ("media\\..\\x.wav"));
            expect (! BundleFormat::isSafeRelativePath ("media//x.wav"));
            expect (! BundleFormat::isSafeRelativePath ("media/./x.wav"));
            expect (! BundleFormat::isSafeRelativePath ("media/x.wav/"));
            expect (! BundleFormat::isSafeRelativePath ("media/x\x01.wav"));
            expect (! BundleFormat::isSafeRelativePath ("media/\xc3\xa9.wav"));
            expect (! BundleFormat::isSafeRelativePath (juce::String::repeatedString ("a", 201)));

            beginTest ("media entries must be content-addressed and use an allowed extension");
            expect (BundleFormat::isMediaEntry ("media/" + hashA + ".wav"));
            expect (BundleFormat::isMediaEntry ("media/" + hashA + ".flac"));
            expect (! BundleFormat::isMediaEntry ("media/" + hashA + ".exe"));
            expect (! BundleFormat::isMediaEntry ("media/" + hashA + ".WAV"));
            expect (! BundleFormat::isMediaEntry ("media/" + hashA.toUpperCase() + ".wav"));
            expect (! BundleFormat::isMediaEntry ("media/" + hashA.substring (1) + ".wav"));
            expect (! BundleFormat::isMediaEntry ("media/sub/" + hashA + ".wav"));
            expect (! BundleFormat::isMediaEntry ("media/../" + hashA + ".wav"));
            expect (! BundleFormat::isMediaEntry (hashA + ".wav"));
            expect (BundleFormat::mediaEntryName (hashA, "WAV") == "media/" + hashA + ".wav");

            beginTest ("a valid manifest parses and round-trips through JSON");
            MediaManifest original;
            BundleMediaEntry entry;
            entry.layer = 1;
            entry.entryName = "media/" + hashA + ".wav";
            entry.sha256 = hashA;
            entry.byteSize = 4096;
            entry.originalName = "Kick (dry).wav";
            entry.channels = 2;
            entry.sampleRate = 44100;
            entry.lengthFrames = 2205;
            original.media.push_back (entry);

            MediaManifest parsed;
            juce::String error;
            expect (MediaManifest::parse (original.toJson(), parsed, error) == MediaManifest::Status::ok, error);
            expectEquals (static_cast<int> (parsed.media.size()), 1);
            if (! parsed.media.empty())
            {
                expectEquals (parsed.media[0].layer, 1);
                expect (parsed.media[0].entryName == entry.entryName);
                expectEquals (parsed.media[0].byteSize, static_cast<juce::int64> (4096));
                expect (parsed.media[0].originalName == "Kick (dry).wav");
                expectEquals (parsed.media[0].lengthFrames, static_cast<juce::int64> (2205));
            }
            expect (parsed.findLayer (1) != nullptr);
            expect (parsed.findLayer (0) == nullptr);

            beginTest ("an empty media list is a valid preset-only bundle");
            expect (parseStatus (document ("1", "")) == MediaManifest::Status::ok);

            beginTest ("malformed manifests are rejected with the specific reason");
            expect (parseStatus ("not json") == MediaManifest::Status::malformedJson);
            expect (parseStatus ("[1,2,3]") == MediaManifest::Status::malformedJson);
            expect (parseStatus ("{}") == MediaManifest::Status::malformedJson);
            expect (parseStatus ("{\"schema_version\":1}") == MediaManifest::Status::malformedJson);
            expect (parseStatus ("{\"schema_version\":\"1\",\"media\":[]}") == MediaManifest::Status::malformedJson);
            expect (parseStatus (document ("2", "")) == MediaManifest::Status::unsupportedSchema);
            expect (parseStatus (document ("0", "")) == MediaManifest::Status::unsupportedSchema);
            expect (parseStatus ("{\"schema_version\":1,\"media\":{}}") == MediaManifest::Status::malformedJson);
            expect (parseStatus (document ("1", item() + "," + item ("1") + "," + item ("0"))) == MediaManifest::Status::tooManyMediaEntries);
            expect (parseStatus (document ("1", "42")) == MediaManifest::Status::malformedJson);
            expect (parseStatus (document ("1", item ("2"))) == MediaManifest::Status::invalidLayer);
            expect (parseStatus (document ("1", item ("-1"))) == MediaManifest::Status::invalidLayer);
            expect (parseStatus (document ("1", item ("\"0\""))) == MediaManifest::Status::invalidLayer);
            expect (parseStatus (document ("1", item() + "," + item ("0"))) == MediaManifest::Status::duplicateLayer);
            expect (parseStatus (document ("1", item ("0", "\"media/../../evil.wav\""))) == MediaManifest::Status::invalidEntryName);
            expect (parseStatus (document ("1", item ("0", "\"../evil.wav\""))) == MediaManifest::Status::invalidEntryName);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashB + ".wav\""))) == MediaManifest::Status::invalidEntryName);
            expect (parseStatus (document ("1", item ("0", "42"))) == MediaManifest::Status::invalidEntryName);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".exe\""))) == MediaManifest::Status::invalidEntryName);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA.toUpperCase() + "\""))) == MediaManifest::Status::invalidHash);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"abc\""))) == MediaManifest::Status::invalidHash);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "7"))) == MediaManifest::Status::invalidHash);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "0"))) == MediaManifest::Status::invalidSize);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "-5"))) == MediaManifest::Status::invalidSize);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "1000000000000"))) == MediaManifest::Status::invalidSize);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "\"12\""))) == MediaManifest::Status::invalidSize);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "1.5"))) == MediaManifest::Status::invalidSize);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "1024", "\"a/b.wav\""))) == MediaManifest::Status::invalidOriginalName);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "1024", "\"\""))) == MediaManifest::Status::invalidOriginalName);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "1024", "\"x\\\\y.wav\""))) == MediaManifest::Status::invalidOriginalName);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "1024", "\"k\\u0001.wav\""))) == MediaManifest::Status::invalidOriginalName);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "1024", "\"kick.wav\"", "3"))) == MediaManifest::Status::invalidFormat);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "1024", "\"kick.wav\"", "2", "1000"))) == MediaManifest::Status::invalidFormat);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "1024", "\"kick.wav\"", "2", "48000", "1440001"))) == MediaManifest::Status::invalidFormat);
            expect (parseStatus (document ("1", item ("0", "\"media/" + hashA + ".wav\"", "\"" + hashA + "\"", "1024", "\"kick.wav\"", "2", "48000", "0"))) == MediaManifest::Status::invalidFormat);

            beginTest ("an oversized manifest is refused before parsing");
            const auto huge = juce::String::repeatedString (" ", static_cast<int> (BundleFormat::maxManifestBytes) + 1);
            expect (parseStatus (document ("1", "") + huge) == MediaManifest::Status::tooLong);

            beginTest ("atomic writer publishes only complete files and keeps the old one on failure");
            TemporaryDirectory temp;
            const auto destination = temp.child ("sound.txt");
            destination.replaceWithText ("old contents");

            {
                AtomicFileWriter writer (destination);
                juce::String writeError;
                auto stream = writer.openStream (writeError);
                expect (stream != nullptr, writeError);
                if (stream != nullptr)
                    stream->write ("new contents", 12);
                stream.reset();

                expect (writer.commit (3, writeError) == AtomicFileWriter::Status::sizeMismatch);
                expect (destination.loadFileAsString() == "old contents");
                expect (temp.get().getNumberOfChildFiles (juce::File::findFiles, "*.partial") == 0);
            }

            {
                AtomicFileWriter writer (destination);
                juce::String writeError;
                auto stream = writer.openStream (writeError);
                if (stream != nullptr)
                    stream->write ("new contents", 12);
                stream.reset();
                expect (writer.commit (12, writeError) == AtomicFileWriter::Status::ok, writeError);
                expect (destination.loadFileAsString() == "new contents");
                expect (temp.get().getNumberOfChildFiles (juce::File::findFiles, "*.partial") == 0);
            }

            {
                AtomicFileWriter writer (temp.get());
                juce::String writeError;
                expect (writer.openStream (writeError) == nullptr, "a directory is not a valid destination");
                expect (writeError.isNotEmpty());
            }

            {
                AtomicFileWriter writer (temp.child ("abandoned.txt"));
                juce::String writeError;
                auto stream = writer.openStream (writeError);
                expect (stream != nullptr);
                const auto partial = writer.getTemporaryFile();
                stream.reset();
                writer.abandon();
                expect (! partial.existsAsFile(), "abandon removes the partial file");
                expect (! temp.child ("abandoned.txt").existsAsFile());
            }

            beginTest ("content store reuses identical audio and refuses mismatched hashes");
            TemporaryDirectory storeTemp;
            const auto staged = storeTemp.child ("staged.wav");
            staged.replaceWithText ("RIFF test audio bytes");
            const auto contentHash = juce::SHA256 (staged).toHexString();
            MediaStore store (storeTemp.child ("library"));

            const auto first = store.commit (staged, contentHash, "wav");
            expect (first.succeeded, first.message);
            expect (first.file.existsAsFile());
            expect (first.file.getFileName() == contentHash + ".wav");
            const auto second = store.commit (staged, contentHash, "wav");
            expect (second.succeeded && second.file == first.file);

            const auto wrong = store.commit (staged, hashB, "wav");
            expect (! wrong.succeeded);
            expect (! store.fileFor (hashB, "wav").existsAsFile());
            expect (! store.commit (staged, "not-a-hash", "wav").succeeded);
            expect (! store.commit (staged, contentHash, "exe").succeeded);

            beginTest ("relinker prefers content matches, respects depth and skips filesystem roots");
            TemporaryDirectory search;
            const auto shallow = search.child ("library");
            shallow.createDirectory();
            const auto kickBytes = juce::String ("kick audio");
            shallow.getChildFile ("kick.wav").replaceWithText (kickBytes);
            const auto sameName = search.child ("library/nested/deeper");
            sameName.createDirectory();
            sameName.getChildFile ("kick.wav").replaceWithText ("different audio");
            const auto deep = search.child ("library/a/b/c/d/e");
            deep.createDirectory();
            deep.getChildFile ("snare.wav").replaceWithText ("snare audio");

            const auto snareHash = juce::SHA256 (deep.getChildFile ("snare.wav")).toHexString();
            const std::atomic<bool> notCancelled { false };
            MediaRelinker::Expectation snare { snareHash, 11, "snare.wav" };
            const auto deepResult = MediaRelinker::search (search.child ("library"), snare, notCancelled);
            expect (deepResult.candidates.empty(), "content beyond the depth limit is not found");

            MediaRelinker::Expectation kick { juce::SHA256 (shallow.getChildFile ("kick.wav")).toHexString(),
                                              static_cast<juce::int64> (kickBytes.length()), "kick.wav" };
            const auto kickResult = MediaRelinker::search (search.child ("library"), kick, notCancelled);
            expectEquals (static_cast<int> (kickResult.candidates.size()), 2);
            if (kickResult.candidates.size() == 2)
            {
                expect (kickResult.candidates[0].kind == MediaRelinker::MatchKind::content);
                expect (kickResult.candidates[0].file == shallow.getChildFile ("kick.wav"));
                expect (kickResult.candidates[1].kind == MediaRelinker::MatchKind::name);
            }

            auto filesystemRoot = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
            while (! filesystemRoot.isRoot())
                filesystemRoot = filesystemRoot.getParentDirectory();
            expect (! MediaRelinker::isSearchableFolder (filesystemRoot));
            expect (! MediaRelinker::isSearchableFolder (search.child ("missing")));

            const std::atomic<bool> cancelled { true };
            expect (MediaRelinker::search (search.child ("library"), kick, cancelled).cancelled);

            MediaRelinker::Limits tight;
            tight.maxFilesVisited = 1;
            expect (MediaRelinker::search (search.child ("library"), kick, notCancelled, tight).limitReached);
        }
    };

    static MediaManifestTests mediaManifestTests;
}
