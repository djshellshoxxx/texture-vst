#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>
#include <array>

namespace texture
{
    /** Reading, writing and indexing of `.texture` preset files.

        The manager deals only in data: it never touches the APVTS, which keeps it
        unit-testable and keeps the processor the only thing that applies state.
    */
    class PresetManager
    {
    public:
        /** Schema this build writes and is able to read (12_PRESET_SYSTEM.md). */
        static constexpr int currentSchemaVersion = 2;

        /** Hard ceiling from 10_CONSTRAINTS.md. */
        static constexpr juce::int64 maxPresetFileSizeBytes = 8 * 1024;

        static constexpr const char* fileExtension = ".texture";

        /** The five categories offered by the browser. */
        static juce::StringArray getCategories();

        /** One preset's contents. */
        struct PresetData
        {
            int schemaVersion { currentSchemaVersion };
            juce::String name;
            juce::String category { "Cinematic" };
            juce::String author { "Factory" };
            juce::StringArray tags;
            /** Legacy schema-1 Layer 1 path. Kept as a compatibility alias. */
            juce::String samplePath;

            /** Schema 2 sample references and normalized start positions. */
            std::array<juce::String, 2> samplePaths {};
            std::array<float, 2> sampleStarts { 0.0f, 0.0f };

            /** Optional content hash (lower-case SHA-256) per layer, written by sound
                bundles (F05) so a missing sample can be relinked by content. Empty when
                unknown. Older readers ignore the key. */
            std::array<juce::String, 2> mediaSha256 {};

            /** Parameter ID to value, in real-world units. */
            juce::NamedValueSet parameters;

            float getParameter (const juce::String& id, float fallback) const;
            void setParameter (const juce::String& id, float value);
        };

        /** An indexed preset on disk. */
        struct PresetInfo
        {
            juce::File file;
            juce::String name;
            juce::String category;
            juce::String author;
            juce::StringArray tags;
            bool isFactory { false };
            bool isFavourite { false };
        };

        enum class Status
        {
            ok,
            fileMissing,
            tooLarge,
            parseError,
            schemaTooNew,
            invalidData,
            writeFailed
        };

        explicit PresetManager (juce::ApplicationProperties* properties);

        // --- Locations --------------------------------------------------------
        /** `<user data>/Texture/Presets/User`, created on demand. */
        static juce::File getUserPresetDirectory();

        /** `<user data>/Texture/Presets/Factory`, where bundled presets are extracted. */
        static juce::File getFactoryPresetDirectory();

        /** `<user data>/Texture/Samples/Factory`, where bundled samples are extracted. */
        static juce::File getFactorySampleDirectory();

        /** Root of all plugin data, e.g. `%APPDATA%/Texture`. */
        static juce::File getPluginDataDirectory();

        /** User-overridable preset search root; defaults to the user preset folder. */
        juce::File getPresetSearchRoot() const;
        void setPresetSearchRoot (const juce::File& directory);

        // --- Indexing ---------------------------------------------------------
        /** Rescans disk. Cached afterwards; the browser must not scan on every open
            (21_WHAT_NOT_TO_DO.md, item 37).
        */
        void refresh();

        const std::vector<PresetInfo>& getAllPresets() const noexcept { return index; }

        /** Filters the cached index by category and a free-text query over name+tags. */
        std::vector<PresetInfo> filter (const juce::String& category,
                                        const juce::String& searchText,
                                        bool favouritesOnly = false) const;

        /** Finds an indexed preset by display name, or nullptr. */
        const PresetInfo* findByName (const juce::String& name) const;

        // --- Favourites -------------------------------------------------------
        bool isFavourite (const juce::File& file) const;
        void setFavourite (const juce::File& file, bool shouldBeFavourite);

        // --- Read / write -----------------------------------------------------
        /** Parses a preset. On failure `data` is left untouched (16_ERROR_HANDLING.md). */
        Status load (const juce::File& file, PresetData& data, juce::String& errorMessage) const;

        Status save (const juce::File& file, const PresetData& data, juce::String& errorMessage) const;

        /** Resolves a preset's sample reference using the order in 12_PRESET_SYSTEM.md. */
        juce::File resolveSamplePath (const juce::String& storedPath) const;

        /** Stores a sample path relative to the factory folder when possible. */
        juce::String makeStorablePath (const juce::File& sampleFile) const;

        /** Writes the five bundled factory presets to disk if they are not there. */
        void installFactoryPresetsIfNeeded();

        /** The preset shown on a fresh instance, per 12_PRESET_SYSTEM.md. */
        juce::File getDefaultPresetFile() const;

        static juce::String describe (Status status);

        /** The range rules preset loading applies to one parameter value. Sound snapshots
            (F09) use the same check, so an in-memory variation can never hold a value a
            preset file would refuse. */
        static bool isValidParameterValue (const juce::String& id, double value);

    private:
        juce::PropertiesFile* getStore() const;
        void indexDirectory (const juce::File& directory, bool isFactory);

        juce::ApplicationProperties* applicationProperties { nullptr };
        std::vector<PresetInfo> index;
        juce::File searchRootOverride;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
    };
}
