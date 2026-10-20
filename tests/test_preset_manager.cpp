#include "TestHelpers.h"

#include "../Source/Presets/PresetManager.h"
#include "../Source/Utility/ParameterIDs.h"

namespace texture::tests
{
    namespace
    {
        /** A preset with every parameter set to a distinctive value. */
        PresetManager::PresetData makeFullPreset (const juce::String& name,
                                                  const juce::String& category)
        {
            PresetManager::PresetData data;
            data.name = name;
            data.category = category;
            data.author = "Test Suite";
            data.tags = { "warm", "slow", "stereo" };
            data.samplePath = "factory/Pad_04.wav";

            data.setParameter (params::morph, 0.4242f);
            data.setParameter (params::attack, 321.5f);
            data.setParameter (params::decay, 654.25f);
            data.setParameter (params::sustain, 0.6125f);
            data.setParameter (params::release, 1987.75f);
            data.setParameter (params::reverbOn, 0.0f);
            data.setParameter (params::gainDb, -4.5f);
            data.setParameter (params::lfoRate, 0.75f);
            data.setParameter (params::lfoDepth, 0.25f);
            data.setParameter (params::lfoSync, 1.0f);
            data.setParameter (params::lfoDivision, 3.0f);
            data.setParameter (params::layerBlend, 0.5f);
            data.setParameter (params::tune, -7.0f);
            data.setParameter (params::layer1Root, 69.0f);
            data.setParameter (params::layer2Root, 48.0f);
            data.setParameter (params::layer1FineCents, 13.2f);
            data.setParameter (params::layer2FineCents, -22.7f);
            data.setParameter (params::reverse, 0.375f);
            data.setParameter (params::shimmer, 0.625f);
            data.setParameter (params::randomMode, 1.0f);

            return data;
        }
    }

    class PresetManagerTests : public juce::UnitTest
    {
    public:
        PresetManagerTests()
            : juce::UnitTest ("Preset manager", "texture") {}

        void runTest() override
        {
            testRoundTrip();
            testCorruptFileLeavesStateAlone();
            testSchemaTooNew();
            testInvalidSchemaRejected();
            testInvalidParameterDataRejected();
            testInvalidPresetCannotBeSaved();
            testDualLayerPresetRoundTrip();
            testOversizedPresetRejected();
            testMissingSampleStillLoadsParameters();
            testSamplePathResolution();
            testCategoryFilteringAndSearch();
            testCategories();
        }

    private:
        void testRoundTrip()
        {
            beginTest ("save then load reproduces every parameter exactly");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);

            const auto original = makeFullPreset ("Round Trip", "Cinematic");
            const auto file = temp.child ("round_trip.texture");

            juce::String error;
            expectEquals (presets.save (file, original, error), PresetManager::Status::ok);
            expect (error.isEmpty());
            expect (file.existsAsFile());

            PresetManager::PresetData loaded;
            expectEquals (presets.load (file, loaded, error), PresetManager::Status::ok);

            expectEquals (loaded.name, original.name);
            expectEquals (loaded.category, original.category);
            expectEquals (loaded.author, original.author);
            expectEquals (loaded.samplePath, original.samplePath);
            expectEquals (loaded.schemaVersion, PresetManager::currentSchemaVersion);
            expectEquals (loaded.tags.size(), original.tags.size());

            for (const auto& tag : original.tags)
                expect (loaded.tags.contains (tag), "missing tag: " + tag);

            for (const auto* id : params::allIDs)
                expectWithinAbsoluteError (loaded.getParameter (id, -999.0f),
                                           original.getParameter (id, -998.0f),
                                           0.0001f,
                                           juce::String ("parameter did not survive: ") + id);

            beginTest ("the file on disk is readable JSON with the documented schema");

            const auto text = file.loadFileAsString();
            const auto parsed = juce::JSON::parse (text);

            expect (parsed.isObject());
            expectEquals (static_cast<int> (parsed.getProperty ("schema_version", 0)), PresetManager::currentSchemaVersion);
            expectEquals (parsed.getProperty ("preset_name", "").toString(), juce::String ("Round Trip"));
            expect (parsed.getProperty ("parameters", juce::var()).isObject());
            expect (file.getSize() <= PresetManager::maxPresetFileSizeBytes);
        }

        void testCorruptFileLeavesStateAlone()
        {
            beginTest ("a corrupt preset does not modify the data it was given");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);

            // Start from a known-good value so we can prove nothing changed.
            auto data = makeFullPreset ("Untouched", "Lofi");

            for (const auto* text : { "{ this is not json",
                                      "",
                                      "[1, 2, 3]",
                                      "\"just a string\"",
                                      "null" })
            {
                const auto file = temp.child ("corrupt.texture");
                file.replaceWithText (text);

                juce::String error;
                const auto status = presets.load (file, data, error);

                expect (status != PresetManager::Status::ok,
                        juce::String ("should have rejected: ") + text);
                expect (error.isNotEmpty(), "a user-facing message is required");

                expectEquals (data.name, juce::String ("Untouched"), "the caller's data must survive");
                expectWithinAbsoluteError (data.getParameter (params::morph, 0.0f), 0.4242f, 0.0001f);
            }

            beginTest ("a missing preset file is reported rather than crashing");

            juce::String error;
            const auto status = presets.load (temp.child ("absent.texture"), data, error);
            expectEquals (status, PresetManager::Status::fileMissing);
            expectEquals (data.name, juce::String ("Untouched"));
        }

        void testSchemaTooNew()
        {
            beginTest ("a preset from a newer version is refused with the right message");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);

            const auto file = temp.child ("future.texture");
            file.replaceWithText (R"({"schema_version": 99, "preset_name": "From The Future"})");

            auto data = makeFullPreset ("Original", "Ambient");
            juce::String error;

            expectEquals (presets.load (file, data, error), PresetManager::Status::schemaTooNew);
            expect (error.containsIgnoreCase ("newer version"));
            expectEquals (data.name, juce::String ("Original"), "state must be left alone");
        }


        void testInvalidSchemaRejected()
        {
            beginTest ("schema zero and negative schema values are rejected");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);
            auto data = makeFullPreset ("Original", "Ambient");

            for (const auto schema : { 0, -1 })
            {
                const auto file = temp.child ("invalid_schema.texture");
                file.replaceWithText ("{\"schema_version\": " + juce::String (schema)
                                      + ", \"preset_name\": \"Broken\", \"parameters\": {}}");

                juce::String error;
                expectEquals (presets.load (file, data, error), PresetManager::Status::invalidData);
                expect (error.isNotEmpty());
                expectEquals (data.name, juce::String ("Original"));
            }
        }

        void testInvalidParameterDataRejected()
        {
            beginTest ("known parameter values must be numeric and inside documented ranges");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);
            auto data = makeFullPreset ("Original", "Ambient");

            const juce::StringArray invalidBodies {
                R"({"schema_version":1,"preset_name":"Bad","parameters":{"morph":1.5}})",
                R"({"schema_version":1,"preset_name":"Bad","parameters":{"attack":0.0}})",
                R"({"schema_version":1,"preset_name":"Bad","parameters":{"gain_db":99.0}})",
                R"({"schema_version":1,"preset_name":"Bad","parameters":{"lfo_division":99}})",
                R"({"schema_version":1,"preset_name":"Bad","parameters":{"morph":"loud"}})",
                R"({"schema_version":1,"preset_name":"Bad","parameters":{"reverb_on":2}})"
            };

            for (int i = 0; i < invalidBodies.size(); ++i)
            {
                const auto file = temp.child ("invalid_parameter_" + juce::String (i) + ".texture");
                file.replaceWithText (invalidBodies[i]);

                juce::String error;
                expectEquals (presets.load (file, data, error), PresetManager::Status::invalidData,
                              "invalid parameter payload should be rejected");
                expect (error.containsIgnoreCase ("parameter"));
                expectEquals (data.name, juce::String ("Original"),
                              "invalid preset must leave caller state untouched");
                expectWithinAbsoluteError (data.getParameter (params::morph, 0.0f), 0.4242f, 0.0001f);
            }

            beginTest ("unknown parameter IDs are ignored for forward-compatible metadata");

            const auto futureMetadata = temp.child ("unknown_parameter.texture");
            futureMetadata.replaceWithText (
                R"({"schema_version":1,"preset_name":"Forward","parameters":{"morph":0.25,"future_hint":42}})");

            juce::String error;
            PresetManager::PresetData loaded;
            expectEquals (presets.load (futureMetadata, loaded, error), PresetManager::Status::ok);
            expectWithinAbsoluteError (loaded.getParameter (params::morph, 0.0f), 0.25f, 0.0001f);
            expect (loaded.parameters.getVarPointer ("future_hint") == nullptr,
                    "unknown fields must not enter the active parameter set");
        }


        void testInvalidPresetCannotBeSaved()
        {
            beginTest ("save rejects invalid known parameter values");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);
            auto data = makeFullPreset ("Bad Save", "Ambient");
            data.setParameter (params::morph, 2.0f);

            juce::String error;
            const auto file = temp.child ("bad_save.texture");

            expectEquals (presets.save (file, data, error), PresetManager::Status::invalidData);
            expect (error.containsIgnoreCase ("morph"));
            expect (! file.existsAsFile(), "invalid preset must not be written");

            beginTest ("unknown in-memory parameter IDs are not serialized");

            data = makeFullPreset ("Forward Safe", "Ambient");
            data.parameters.set ("future_hint", 42.0);

            expectEquals (presets.save (file, data, error), PresetManager::Status::ok);
            const auto text = file.loadFileAsString();
            expect (! text.contains ("future_hint"));
        }


        void testDualLayerPresetRoundTrip()
        {
            beginTest ("schema 2 preserves both sample layers and start positions");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);
            auto data = makeFullPreset ("Dual", "Ambient");
            data.samplePaths[0] = "factory/Pad_01.wav";
            data.samplePaths[1] = "factory/Pad_02.wav";
            data.sampleStarts[0] = 0.125f;
            data.sampleStarts[1] = 0.875f;

            juce::String error;
            const auto file = temp.child ("dual.texture");
            expectEquals (presets.save (file, data, error), PresetManager::Status::ok);

            PresetManager::PresetData loaded;
            expectEquals (presets.load (file, loaded, error), PresetManager::Status::ok);
            expectEquals (loaded.samplePaths[0], juce::String ("factory/Pad_01.wav"));
            expectEquals (loaded.samplePaths[1], juce::String ("factory/Pad_02.wav"));
            expectWithinAbsoluteError (loaded.sampleStarts[0], 0.125f, 1.0e-6f);
            expectWithinAbsoluteError (loaded.sampleStarts[1], 0.875f, 1.0e-6f);

            beginTest ("legacy schema 1 sample_path migrates to layer 1");
            const auto legacy = temp.child ("legacy.texture");
            legacy.replaceWithText (
                R"({"schema_version":1,"preset_name":"Legacy","sample_path":"factory/Pad_03.wav","parameters":{"morph":0.5}})");

            PresetManager::PresetData migrated;
            expectEquals (presets.load (legacy, migrated, error), PresetManager::Status::ok);
            expectEquals (migrated.samplePaths[0], juce::String ("factory/Pad_03.wav"));
            expect (migrated.samplePaths[1].isEmpty());
            expectWithinAbsoluteError (migrated.sampleStarts[0], 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (migrated.sampleStarts[1], 0.0f, 1.0e-6f);

            beginTest ("invalid sample start values are rejected atomically");
            const auto invalid = temp.child ("invalid_start.texture");
            invalid.replaceWithText (
                R"({"schema_version":2,"preset_name":"Bad","samples":[{"path":"x.wav","start":1.5},{}],"parameters":{"morph":0.5}})");

            auto unchanged = makeFullPreset ("Keep", "Ambient");
            expectEquals (presets.load (invalid, unchanged, error), PresetManager::Status::invalidData);
            expectEquals (unchanged.name, juce::String ("Keep"));
        }

        void testOversizedPresetRejected()
        {
            beginTest ("a preset larger than 8 KB is refused");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);

            const auto file = temp.child ("huge.texture");
            file.replaceWithText ("{\"schema_version\": 1, \"preset_name\": \""
                                  + juce::String::repeatedString ("x", 9000) + "\"}");

            PresetManager::PresetData data;
            juce::String error;

            expectEquals (presets.load (file, data, error), PresetManager::Status::tooLarge);

            // And we refuse to write one that big in the first place.
            auto oversized = makeFullPreset ("Oversized", "Trap");

            for (int i = 0; i < 400; ++i)
                oversized.tags.add ("tag_number_" + juce::String (i) + "_with_padding");

            expectEquals (presets.save (temp.child ("out.texture"), oversized, error),
                          PresetManager::Status::writeFailed);
        }

        void testMissingSampleStillLoadsParameters()
        {
            beginTest ("a preset whose sample is gone still restores its parameters");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);

            auto data = makeFullPreset ("Orphan", "Synthwave");
            data.samplePath = "/nowhere/at/all/definitely_missing.wav";

            const auto file = temp.child ("orphan.texture");
            juce::String error;
            expectEquals (presets.save (file, data, error), PresetManager::Status::ok);

            PresetManager::PresetData loaded;
            expectEquals (presets.load (file, loaded, error), PresetManager::Status::ok);

            expectWithinAbsoluteError (loaded.getParameter (params::morph, 0.0f), 0.4242f, 0.0001f);
            expectEquals (loaded.samplePath, data.samplePath);

            // The sample slot resolves to nothing, which is what tells the processor
            // to show the "sample missing" toast (12_PRESET_SYSTEM.md).
            expect (presets.resolveSamplePath (loaded.samplePath) == juce::File());
        }

        void testSamplePathResolution()
        {
            beginTest ("absolute paths win, then the factory folder");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);

            const auto real = temp.child ("actual_sample.wav");
            expect (writeTestAudioFile (real, 44100.0, 0.1, 1));

            // 1. An absolute path that exists resolves to itself.
            expect (presets.resolveSamplePath (real.getFullPathName()) == real);

            // 2. A "factory/" prefixed path looks inside the factory samples folder.
            const auto factoryDirectory = PresetManager::getFactorySampleDirectory();
            const auto factoryFile = factoryDirectory.getChildFile ("TextureTest_Pad.wav");
            expect (writeTestAudioFile (factoryFile, 44100.0, 0.1, 1));

            expect (presets.resolveSamplePath ("factory/TextureTest_Pad.wav") == factoryFile);
            expect (presets.resolveSamplePath ("TextureTest_Pad.wav") == factoryFile);

            factoryFile.deleteFile();

            // 3. Anything unresolvable comes back empty rather than as a bad File.
            expect (presets.resolveSamplePath ("factory/nope_not_here.wav") == juce::File());
            expect (presets.resolveSamplePath ("") == juce::File());

            beginTest ("factory samples are stored as relative references");

            const auto storedFactory = presets.makeStorablePath (
                factoryDirectory.getChildFile ("Pad_04.wav"));
            expect (storedFactory.startsWithIgnoreCase ("factory/"),
                    "got: " + storedFactory);

            const auto storedUser = presets.makeStorablePath (real);
            expectEquals (storedUser, real.getFullPathName(),
                          "user samples keep their absolute path");

            expect (presets.makeStorablePath (juce::File()).isEmpty());
        }

        void testCategoryFilteringAndSearch()
        {
            beginTest ("category filtering and search return the right subset");

            TemporaryDirectory temp;
            PresetManager presets (nullptr);
            presets.setPresetSearchRoot (temp.get());

            // A unique marker keeps this independent of anything already installed.
            const auto marker = "zzmarker" + juce::String (juce::Random::getSystemRandom().nextInt (100000));

            struct Fixture { const char* name; const char* category; const char* tag; };

            const Fixture fixtures[] = {
                { "Alpha", "Cinematic", "wide" },
                { "Bravo", "Cinematic", "narrow" },
                { "Charlie", "Lofi", "dusty" },
                { "Delta", "Ambient", "wide" },
                { "Echo", "Trap", "hard" }
            };

            for (const auto& fixture : fixtures)
            {
                auto data = makeFullPreset (marker + "_" + fixture.name, fixture.category);
                data.tags = { fixture.tag, marker };

                juce::String error;
                const auto file = temp.child (juce::String (fixture.category) + "_"
                                              + fixture.name + ".texture");
                expectEquals (presets.save (file, data, error), PresetManager::Status::ok);
            }

            presets.refresh();

            auto countMatching = [&] (const juce::String& categoryFilter, const juce::String& search)
            {
                int count = 0;

                for (const auto& info : presets.filter (categoryFilter, search))
                    if (info.name.startsWith (marker))
                        ++count;

                return count;
            };

            expectEquals (countMatching ("All", marker), 5, "all five fixtures should be indexed");
            expectEquals (countMatching ("Cinematic", marker), 2);
            expectEquals (countMatching ("Lofi", marker), 1);
            expectEquals (countMatching ("Ambient", marker), 1);
            expectEquals (countMatching ("Trap", marker), 1);
            expectEquals (countMatching ("Synthwave", marker), 0);

            beginTest ("search covers names, tags and the author");

            expectEquals (countMatching ("All", marker + "_Charlie"), 1);
            expectEquals (countMatching ("All", "wide"), 2, "tags must be searchable");
            expectEquals (countMatching ("All", "WIDE"), 2, "search must ignore case");
            expectEquals (countMatching ("All", "no-such-thing-anywhere"), 0);

            beginTest ("lookup by name finds an indexed preset");

            const auto* found = presets.findByName (marker + "_Delta");
            expect (found != nullptr);

            if (found != nullptr)
            {
                expectEquals (found->category, juce::String ("Ambient"));
                expect (found->tags.contains ("wide"));
            }

            expect (presets.findByName ("definitely not a preset") == nullptr);
        }

        void testCategories()
        {
            beginTest ("the five documented categories are offered");

            const auto categories = PresetManager::getCategories();

            expectEquals (categories.size(), 5);

            for (const auto* expected : { "Cinematic", "Lofi", "Ambient", "Trap", "Synthwave" })
                expect (categories.contains (expected), juce::String ("missing category: ") + expected);
        }
    };

    static PresetManagerTests presetManagerTests;
}
