#include "PresetManager.h"

#include "../Utility/ParameterIDs.h"

#include <algorithm>
#include <cmath>

namespace texture
{
    namespace
    {
        constexpr const char* favouritesKeyName = "preset_favourites";
        constexpr const char* presetRootKeyName = "preset_folder";

        /** A factory preset defined in code so a fresh install is never empty. */
        struct FactoryPresetSpec
        {
            const char* category;
            const char* name;
            const char* tags;
            const char* sample;
            float morph;
            float attackMs;
            float decayMs;
            float sustain;
            float releaseMs;
            bool reverbOn;
            float gainDb;
        };

        constexpr FactoryPresetSpec factoryPresets[] = {
            { "Cinematic", "Warm Pad",   "warm,slow,stereo",      "factory/Pad_04.wav", 0.42f,  350.0f,  500.0f, 0.80f, 1200.0f, true,  -3.0f },
            { "Cinematic", "Cold Vast",  "cold,wide,slow",        "factory/Pad_11.wav", 0.78f, 1400.0f, 1800.0f, 0.75f, 4200.0f, true,  -4.5f },
            { "Ambient",   "Slow Drift", "drift,soft,evolving",   "factory/Pad_07.wav", 0.62f,  900.0f, 1500.0f, 0.85f, 3000.0f, true,  -3.5f },
            { "Lofi",      "Tape Bed",   "lofi,tape,dusty",       "factory/Pad_02.wav", 0.28f,  120.0f,  400.0f, 0.70f,  900.0f, true,  -2.0f },
            { "Synthwave", "Neon Hold",  "neon,bright,sustained", "factory/Pad_16.wav", 0.35f,   60.0f,  300.0f, 0.90f,  800.0f, true,  -1.5f }
        };

        juce::String joinTags (const juce::StringArray& tags)
        {
            return tags.joinIntoString (",");
        }

        juce::StringArray splitTags (const juce::String& text)
        {
            juce::StringArray tags;
            tags.addTokens (text, ",", "");
            tags.trim();
            tags.removeEmptyStrings();
            return tags;
        }

        bool isKnownParameterID (const juce::String& id)
        {
            for (const auto* known : params::allIDs)
                if (id == known)
                    return true;

            return false;
        }

        bool readNumericParameter (const juce::var& value, double& result)
        {
            if (! (value.isInt() || value.isInt64() || value.isDouble() || value.isBool()))
                return false;

            result = static_cast<double> (value);
            return std::isfinite (result);
        }

        bool parameterValueIsValid (const juce::String& id, const juce::var& value)
        {
            double numeric = 0.0;

            if (! readNumericParameter (value, numeric))
                return false;

            const auto inRange = [numeric] (double minimum, double maximum)
            {
                return numeric >= minimum && numeric <= maximum;
            };

            if (id == params::morph)       return inRange (params::morphMin, params::morphMax);
            if (id == params::attack)      return inRange (params::attackMinMs, params::attackMaxMs);
            if (id == params::decay)       return inRange (params::decayMinMs, params::decayMaxMs);
            if (id == params::sustain)     return inRange (params::sustainMin, params::sustainMax);
            if (id == params::release)     return inRange (params::releaseMinMs, params::releaseMaxMs);
            if (id == params::gainDb)      return inRange (params::gainMinDb, params::gainMaxDb);
            if (id == params::lfoRate)     return inRange (params::lfoRateMinHz, params::lfoRateMaxHz);
            if (id == params::lfoDepth)    return inRange (params::lfoDepthMin, params::lfoDepthMax);
            if (id == params::layerBlend)  return inRange (params::layerBlendMin, params::layerBlendMax);
            if (id == params::reverse)     return inRange (params::reverseMin, params::reverseMax);
            if (id == params::shimmer)     return inRange (params::shimmerMin, params::shimmerMax);
            if (id == params::layer1FineCents || id == params::layer2FineCents)
                return inRange (params::sourceFineMinCents, params::sourceFineMaxCents);

            if (id == params::layer1Root || id == params::layer2Root)
                return juce::exactlyEqual (numeric, std::floor (numeric))
                    && inRange (params::sourceRootMinMidi, params::sourceRootMaxMidi);

            if (id == params::tune)
                return juce::exactlyEqual (numeric, std::floor (numeric))
                    && inRange (params::tuneMinSemitones, params::tuneMaxSemitones);

            if (id == params::reverbOn || id == params::lfoSync)
                return juce::exactlyEqual (numeric, 0.0) || juce::exactlyEqual (numeric, 1.0);

            if (id == params::lfoDivision)
                return juce::exactlyEqual (numeric, std::floor (numeric))
                    && numeric >= 0.0
                    && numeric < static_cast<double> (params::numDivisions);

            if (id == params::randomMode)
                return juce::exactlyEqual (numeric, std::floor (numeric))
                    && numeric >= 0.0 && numeric <= 2.0;

            // F07 tone and space. Any value outside its range rejects the preset, as for every
            // other parameter; readers that predate these IDs ignore them.
            if (id == params::toneSpaceMode)
                return juce::exactlyEqual (numeric, std::floor (numeric))
                    && numeric >= 0.0 && numeric <= static_cast<double> (params::toneSpaceModeIndependent);

            if (id == params::toneFilterOn)
                return juce::exactlyEqual (numeric, 0.0) || juce::exactlyEqual (numeric, 1.0);

            if (id == params::toneHpHz)    return inRange (params::toneHpMinHz, params::toneHpMaxHz);
            if (id == params::toneLpHz)    return inRange (params::toneLpMinHz, params::toneLpMaxHz);
            if (id == params::toneTiltDb)  return inRange (params::toneTiltMinDb, params::toneTiltMaxDb);
            if (id == params::spaceWidth)  return inRange (params::spaceWidthMin, params::spaceWidthMax);
            if (id == params::spaceReverb) return inRange (params::spaceReverbMin, params::spaceReverbMax);
            if (id == params::spaceChorus) return inRange (params::spaceChorusMin, params::spaceChorusMax);
            if (id == params::toneDrive)   return inRange (params::toneDriveMin, params::toneDriveMax);

            return false;
        }
    }

    // --- PresetData ---------------------------------------------------------

    float PresetManager::PresetData::getParameter (const juce::String& id, float fallback) const
    {
        const auto* value = parameters.getVarPointer (juce::Identifier (id));
        return value != nullptr ? static_cast<float> (static_cast<double> (*value)) : fallback;
    }

    void PresetManager::PresetData::setParameter (const juce::String& id, float value)
    {
        parameters.set (juce::Identifier (id), value);
    }

    // --- Construction -------------------------------------------------------

    PresetManager::PresetManager (juce::ApplicationProperties* properties)
        : applicationProperties (properties)
    {
        if (auto* store = getStore())
        {
            const auto storedRoot = store->getValue (presetRootKeyName);

            if (storedRoot.isNotEmpty())
                searchRootOverride = juce::File (storedRoot);
        }
    }

    juce::PropertiesFile* PresetManager::getStore() const
    {
        return applicationProperties != nullptr ? applicationProperties->getUserSettings() : nullptr;
    }

    juce::StringArray PresetManager::getCategories()
    {
        return { "Cinematic", "Lofi", "Ambient", "Trap", "Synthwave" };
    }

    // --- Locations ----------------------------------------------------------

    juce::File PresetManager::getPluginDataDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Texture");
    }

    juce::File PresetManager::getUserPresetDirectory()
    {
        auto directory = getPluginDataDirectory().getChildFile ("Presets").getChildFile ("User");
        directory.createDirectory();
        return directory;
    }

    juce::File PresetManager::getFactoryPresetDirectory()
    {
        auto directory = getPluginDataDirectory().getChildFile ("Presets").getChildFile ("Factory");
        directory.createDirectory();
        return directory;
    }

    juce::File PresetManager::getFactorySampleDirectory()
    {
        auto directory = getPluginDataDirectory().getChildFile ("Samples").getChildFile ("Factory");
        directory.createDirectory();
        return directory;
    }

    juce::File PresetManager::getPresetSearchRoot() const
    {
        if (searchRootOverride != juce::File() && searchRootOverride.isDirectory())
            return searchRootOverride;

        return getPluginDataDirectory().getChildFile ("Presets");
    }

    void PresetManager::setPresetSearchRoot (const juce::File& directory)
    {
        searchRootOverride = directory;

        if (auto* store = getStore())
        {
            store->setValue (presetRootKeyName, directory.getFullPathName());
            store->saveIfNeeded();
        }

        refresh();
    }

    // --- Indexing -----------------------------------------------------------

    void PresetManager::refresh()
    {
        index.clear();

        indexDirectory (getFactoryPresetDirectory(), true);

        const auto root = getPresetSearchRoot();
        const auto factoryRoot = getFactoryPresetDirectory();

        for (const auto& entry : juce::RangedDirectoryIterator (root, true, juce::String ("*") + fileExtension,
                                                                juce::File::findFiles))
        {
            const auto file = entry.getFile();

            if (file.isAChildOf (factoryRoot))
                continue;   // already indexed as factory

            PresetInfo info;
            info.file = file;
            info.isFactory = false;

            PresetData data;
            juce::String error;

            if (load (file, data, error) != Status::ok)
            {
                juce::Logger::writeToLog ("[warn] [presets] Skipping unreadable preset: " + file.getFullPathName());
                continue;
            }

            info.name = data.name.isNotEmpty() ? data.name : file.getFileNameWithoutExtension();
            info.category = data.category;
            info.author = data.author;
            info.tags = data.tags;
            info.isFavourite = isFavourite (file);

            index.push_back (std::move (info));
        }

        std::sort (index.begin(), index.end(), [] (const PresetInfo& a, const PresetInfo& b)
        {
            if (a.category != b.category)
                return a.category < b.category;

            return a.name.compareIgnoreCase (b.name) < 0;
        });
    }

    void PresetManager::indexDirectory (const juce::File& directory, bool isFactory)
    {
        if (! directory.isDirectory())
            return;

        for (const auto& entry : juce::RangedDirectoryIterator (directory, true,
                                                                juce::String ("*") + fileExtension,
                                                                juce::File::findFiles))
        {
            const auto file = entry.getFile();

            PresetData data;
            juce::String error;

            if (load (file, data, error) != Status::ok)
            {
                juce::Logger::writeToLog ("[warn] [presets] Skipping unreadable preset: " + file.getFullPathName());
                continue;
            }

            PresetInfo info;
            info.file = file;
            info.name = data.name.isNotEmpty() ? data.name : file.getFileNameWithoutExtension();
            info.category = data.category;
            info.author = data.author;
            info.tags = data.tags;
            info.isFactory = isFactory;
            info.isFavourite = isFavourite (file);

            index.push_back (std::move (info));
        }
    }

    std::vector<PresetManager::PresetInfo> PresetManager::filter (const juce::String& category,
                                                                  const juce::String& searchText,
                                                                  bool favouritesOnly) const
    {
        std::vector<PresetInfo> matches;
        const auto query = searchText.trim().toLowerCase();

        for (const auto& info : index)
        {
            if (category.isNotEmpty() && ! category.equalsIgnoreCase ("All")
                && ! info.category.equalsIgnoreCase (category))
                continue;

            if (favouritesOnly && ! info.isFavourite)
                continue;

            if (query.isNotEmpty())
            {
                const auto haystack = (info.name + " " + joinTags (info.tags) + " " + info.author).toLowerCase();

                if (! haystack.contains (query))
                    continue;
            }

            matches.push_back (info);
        }

        return matches;
    }

    const PresetManager::PresetInfo* PresetManager::findByName (const juce::String& name) const
    {
        for (const auto& info : index)
            if (info.name.equalsIgnoreCase (name))
                return &info;

        return nullptr;
    }

    // --- Favourites ---------------------------------------------------------

    bool PresetManager::isFavourite (const juce::File& file) const
    {
        auto* store = getStore();

        if (store == nullptr)
            return false;

        juce::StringArray favourites;
        favourites.addTokens (store->getValue (favouritesKeyName), "|", "");

        return favourites.contains (file.getFullPathName());
    }

    void PresetManager::setFavourite (const juce::File& file, bool shouldBeFavourite)
    {
        auto* store = getStore();

        if (store == nullptr)
            return;

        juce::StringArray favourites;
        favourites.addTokens (store->getValue (favouritesKeyName), "|", "");
        favourites.removeEmptyStrings();

        const auto path = file.getFullPathName();

        if (shouldBeFavourite)
            favourites.addIfNotAlreadyThere (path);
        else
            favourites.removeString (path);

        store->setValue (favouritesKeyName, favourites.joinIntoString ("|"));
        store->saveIfNeeded();

        for (auto& info : index)
            if (info.file == file)
                info.isFavourite = shouldBeFavourite;
    }

    // --- Read / write -------------------------------------------------------

    PresetManager::Status PresetManager::load (const juce::File& file,
                                               PresetData& data,
                                               juce::String& errorMessage) const
    {
        if (! file.existsAsFile())
        {
            errorMessage = "Could not read preset: " + file.getFileNameWithoutExtension();
            return Status::fileMissing;
        }

        if (file.getSize() > maxPresetFileSizeBytes)
        {
            errorMessage = "Could not read preset: " + file.getFileNameWithoutExtension();
            return Status::tooLarge;
        }

        juce::var parsed;

        try
        {
            parsed = juce::JSON::parse (file.loadFileAsString());
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog (juce::String ("[warn] [presets] JSON threw: ") + e.what());
            errorMessage = "Could not read preset: " + file.getFileNameWithoutExtension();
            return Status::parseError;
        }

        // Not `isObject()`: juce::var reports an array as an object as well, so a
        // file containing `[1, 2, 3]` would sail through and then read every field
        // as a default. Only a real JSON object has a DynamicObject behind it.
        if (parsed.getDynamicObject() == nullptr)
        {
            errorMessage = "Could not read preset: " + file.getFileNameWithoutExtension();
            return Status::parseError;
        }

        const auto schema = static_cast<int> (parsed.getProperty ("schema_version", 1));

        if (schema <= 0)
        {
            errorMessage = "Preset schema version is invalid.";
            return Status::invalidData;
        }

        if (schema > currentSchemaVersion)
        {
            errorMessage = "This preset was made with a newer version of TEXTURE.";
            return Status::schemaTooNew;
        }

        // Only mutate `data` once everything has parsed cleanly.
        PresetData parsedData;
        parsedData.schemaVersion = schema;
        parsedData.name = parsed.getProperty ("preset_name", file.getFileNameWithoutExtension()).toString();
        parsedData.category = parsed.getProperty ("category", "Cinematic").toString();
        parsedData.author = parsed.getProperty ("author", "User").toString();
        parsedData.samplePath = parsed.getProperty ("sample_path", "").toString();

        if (schema == 1)
        {
            parsedData.samplePaths[0] = parsedData.samplePath;
        }
        else if (const auto* samples = parsed.getProperty ("samples", juce::var()).getArray())
        {
            if (samples->size() > 2)
            {
                errorMessage = "Preset contains too many sample layers.";
                return Status::invalidData;
            }

            for (int layer = 0; layer < samples->size(); ++layer)
            {
                const auto& sampleVar = samples->getReference (layer);
                auto* sampleObject = sampleVar.getDynamicObject();

                if (sampleObject == nullptr)
                {
                    errorMessage = "Preset sample data is malformed.";
                    return Status::invalidData;
                }

                parsedData.samplePaths[static_cast<size_t> (layer)]
                    = sampleVar.getProperty ("path", "").toString();

                const auto startVar = sampleVar.getProperty ("start", 0.0);
                double start = 0.0;

                if (! readNumericParameter (startVar, start) || start < 0.0 || start > 1.0)
                {
                    errorMessage = "Preset sample start position is invalid.";
                    return Status::invalidData;
                }

                parsedData.sampleStarts[static_cast<size_t> (layer)] = static_cast<float> (start);
            }

            parsedData.samplePath = parsedData.samplePaths[0];
        }

        if (const auto* tagArray = parsed.getProperty ("tags", juce::var()).getArray())
            for (const auto& tag : *tagArray)
                parsedData.tags.add (tag.toString());

        const auto parameterVar = parsed.getProperty ("parameters", juce::var());

        if (! parameterVar.isVoid())
        {
            auto* parameterObject = parameterVar.getDynamicObject();

            if (parameterObject == nullptr)
            {
                errorMessage = "Preset parameter data is malformed.";
                return Status::invalidData;
            }

            for (const auto& property : parameterObject->getProperties())
            {
                const auto id = property.name.toString();

                if (! isKnownParameterID (id))
                    continue;

                if (! parameterValueIsValid (id, property.value))
                {
                    errorMessage = "Preset parameter '" + id + "' is invalid or out of range.";
                    return Status::invalidData;
                }

                parsedData.parameters.set (property.name, property.value);
            }
        }

        data = std::move (parsedData);
        errorMessage.clear();

        return Status::ok;
    }

    PresetManager::Status PresetManager::save (const juce::File& file,
                                               const PresetData& data,
                                               juce::String& errorMessage) const
    {
        juce::DynamicObject::Ptr root (new juce::DynamicObject());
        root->setProperty ("schema_version", currentSchemaVersion);
        root->setProperty ("preset_name", data.name);
        root->setProperty ("category", data.category);
        root->setProperty ("author", data.author);

        juce::Array<juce::var> tagArray;

        for (const auto& tag : data.tags)
            tagArray.add (tag);

        root->setProperty ("tags", tagArray);

        juce::Array<juce::var> sampleArray;

        for (int layer = 0; layer < 2; ++layer)
        {
            juce::DynamicObject::Ptr sampleObject (new juce::DynamicObject());
            auto path = data.samplePaths[static_cast<size_t> (layer)];

            if (layer == 0 && path.isEmpty())
                path = data.samplePath;

            const auto start = data.sampleStarts[static_cast<size_t> (layer)];

            if (! std::isfinite (start) || start < 0.0f || start > 1.0f)
            {
                errorMessage = "Preset sample start position is invalid.";
                return Status::invalidData;
            }

            sampleObject->setProperty ("path", path);
            sampleObject->setProperty ("start", start);
            sampleArray.add (juce::var (sampleObject.get()));
        }

        root->setProperty ("samples", sampleArray);

        // Retain the schema-1 field as a human-readable compatibility hint.
        root->setProperty ("sample_path", data.samplePaths[0].isNotEmpty()
                                              ? data.samplePaths[0]
                                              : data.samplePath);

        juce::DynamicObject::Ptr parameterObject (new juce::DynamicObject());

        for (int i = 0; i < data.parameters.size(); ++i)
        {
            const auto name = data.parameters.getName (i);
            const auto id = name.toString();
            const auto& value = data.parameters.getValueAt (i);

            if (! isKnownParameterID (id))
                continue;

            if (! parameterValueIsValid (id, value))
            {
                errorMessage = "Preset parameter '" + id + "' is invalid or out of range.";
                return Status::invalidData;
            }

            parameterObject->setProperty (name, value);
        }

        root->setProperty ("parameters", juce::var (parameterObject.get()));

        const auto text = juce::JSON::toString (juce::var (root.get()), false);

        if (text.getNumBytesAsUTF8() > static_cast<size_t> (maxPresetFileSizeBytes))
        {
            errorMessage = "Preset is too large to save.";
            return Status::writeFailed;
        }

        file.getParentDirectory().createDirectory();

        if (! file.replaceWithText (text))
        {
            errorMessage = "Could not write preset: " + file.getFileNameWithoutExtension();
            return Status::writeFailed;
        }

        errorMessage.clear();
        return Status::ok;
    }

    juce::File PresetManager::resolveSamplePath (const juce::String& storedPath) const
    {
        if (storedPath.isEmpty())
            return {};

        // 1. Absolute path, if it still exists.
        if (juce::File::isAbsolutePath (storedPath))
        {
            const juce::File absolute (storedPath);

            if (absolute.existsAsFile())
                return absolute;
        }

        // A stored path may be prefixed with "factory/" for bundled content.
        auto relative = storedPath;

        if (relative.startsWithIgnoreCase ("factory/") || relative.startsWithIgnoreCase ("factory\\"))
            relative = relative.substring (8);

        // 2. Relative to the factory samples directory.
        const auto inFactory = getFactorySampleDirectory().getChildFile (relative);

        if (inFactory.existsAsFile())
            return inFactory;

        // 3. Relative to the user preset folder.
        const auto inPresets = getPresetSearchRoot().getChildFile (relative);

        if (inPresets.existsAsFile())
            return inPresets;

        // 4. Last chance: bare filename anywhere in the factory folder.
        const auto byName = getFactorySampleDirectory()
                                .getChildFile (juce::File::createLegalFileName (
                                    storedPath.fromLastOccurrenceOf ("/", false, false)));

        if (byName.existsAsFile())
            return byName;

        return {};
    }

    juce::String PresetManager::makeStorablePath (const juce::File& sampleFile) const
    {
        if (sampleFile == juce::File())
            return {};

        const auto factoryDirectory = getFactorySampleDirectory();

        if (sampleFile.isAChildOf (factoryDirectory))
            return "factory/" + sampleFile.getRelativePathFrom (factoryDirectory).replaceCharacter ('\\', '/');

        return sampleFile.getFullPathName();
    }

    void PresetManager::installFactoryPresetsIfNeeded()
    {
        const auto root = getFactoryPresetDirectory();

        for (const auto& spec : factoryPresets)
        {
            const auto directory = root.getChildFile (spec.category);
            directory.createDirectory();

            const auto file = directory.getChildFile (juce::String (spec.name) + fileExtension);

            if (file.existsAsFile())
                continue;

            PresetData data;
            data.name = spec.name;
            data.category = spec.category;
            data.author = "Factory";
            data.tags = splitTags (spec.tags);
            data.samplePath = spec.sample;
            data.setParameter (params::morph, spec.morph);
            data.setParameter (params::attack, spec.attackMs);
            data.setParameter (params::decay, spec.decayMs);
            data.setParameter (params::sustain, spec.sustain);
            data.setParameter (params::release, spec.releaseMs);
            data.setParameter (params::reverbOn, spec.reverbOn ? 1.0f : 0.0f);
            data.setParameter (params::gainDb, spec.gainDb);
            data.setParameter (params::lfoRate, params::lfoRateDefaultHz);
            data.setParameter (params::lfoDepth, 0.0f);
            data.setParameter (params::lfoSync, 0.0f);
            data.setParameter (params::lfoDivision, static_cast<float> (params::lfoDivisionDefault));
            data.setParameter (params::layerBlend, 0.0f);

            juce::String error;

            if (save (file, data, error) != Status::ok)
                juce::Logger::writeToLog ("[warn] [presets] Could not install factory preset: " + error);
        }
    }

    juce::File PresetManager::getDefaultPresetFile() const
    {
        return getFactoryPresetDirectory().getChildFile ("Cinematic")
                                          .getChildFile (juce::String ("Warm Pad") + fileExtension);
    }

    juce::String PresetManager::describe (Status status)
    {
        switch (status)
        {
            case Status::ok:            return "OK";
            case Status::fileMissing:   return "Preset file is missing.";
            case Status::tooLarge:      return "Preset file is too large.";
            case Status::parseError:    return "Preset file could not be parsed.";
            case Status::schemaTooNew:  return "This preset was made with a newer version of TEXTURE.";
            case Status::invalidData:    return "Preset contains invalid or out-of-range data.";
            case Status::writeFailed:   return "Preset could not be written.";
        }

        return "Unknown preset status.";
    }
}
