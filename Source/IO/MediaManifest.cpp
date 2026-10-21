#include "MediaManifest.h"

#include <array>
#include <cmath>

namespace texture
{
    namespace
    {
        constexpr std::array<const char*, 6> allowedExtensions { "wav", "aif", "aiff", "flac", "ogg", "mp3" };

        bool readInteger (const juce::var& value, juce::int64& result) noexcept
        {
            if (value.isInt() || value.isInt64())
            {
                result = static_cast<juce::int64> (value);
                return true;
            }

            if (value.isDouble())
            {
                const auto number = static_cast<double> (value);

                if (! std::isfinite (number) || std::abs (number - std::floor (number)) > 0.0 || std::abs (number) > 9.0e15)
                    return false;

                result = static_cast<juce::int64> (number);
                return true;
            }

            return false;
        }

        bool isPrintableAsciiExceptSeparators (juce::juce_wchar character) noexcept
        {
            return character >= 0x20 && character <= 0x7e && character != '\\' && character != ':';
        }
    }

    bool BundleFormat::isAllowedExtension (const juce::String& extension) noexcept
    {
        for (const auto* allowed : allowedExtensions)
            if (extension == allowed)
                return true;

        return false;
    }

    bool BundleFormat::isSafeRelativePath (const juce::String& path) noexcept
    {
        if (path.isEmpty() || path.length() > maxNameLength || path.startsWithChar ('/'))
            return false;

        for (auto i = 0; i < path.length(); ++i)
            if (! isPrintableAsciiExceptSeparators (path[i]) && path[i] != '/')
                return false;

        // Every component must be a plain name: no empty segments, no dot segments.
        juce::StringArray components;
        components.addTokens (path, "/", "");

        for (const auto& component : components)
            if (component.isEmpty() || component == "." || component == "..")
                return false;

        return true;
    }

    bool BundleFormat::isSha256 (const juce::String& text) noexcept
    {
        if (text.length() != 64)
            return false;

        for (auto i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];
            const auto hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');

            if (! hex)
                return false;
        }

        return true;
    }

    bool BundleFormat::isMediaEntry (const juce::String& entry) noexcept
    {
        if (! entry.startsWith (mediaFolder) || entry.length() > maxNameLength)
            return false;

        const auto name = entry.substring (juce::String (mediaFolder).length());

        if (name.containsChar ('/'))
            return false;

        const auto dot = name.lastIndexOfChar ('.');

        if (dot < 0)
            return false;

        return isSha256 (name.substring (0, dot)) && isAllowedExtension (name.substring (dot + 1));
    }

    juce::String BundleFormat::mediaEntryName (const juce::String& sha256, const juce::String& extension)
    {
        return juce::String (mediaFolder) + sha256 + "." + extension.toLowerCase();
    }

    juce::String MediaManifest::describe (Status status)
    {
        switch (status)
        {
            case Status::ok:                    return "OK";
            case Status::malformedJson:         return "The bundle manifest is not valid.";
            case Status::unsupportedSchema:     return "This bundle was made with a newer version of TEXTURE.";
            case Status::tooManyMediaEntries:   return "The bundle lists more audio files than a layer pair allows.";
            case Status::invalidLayer:          return "The bundle names an unknown sample layer.";
            case Status::duplicateLayer:        return "The bundle lists one sample layer twice.";
            case Status::invalidEntryName:      return "The bundle contains an audio file name that is not allowed.";
            case Status::invalidHash:           return "The bundle contains an invalid content hash.";
            case Status::invalidSize:           return "The bundle audio file size is out of range.";
            case Status::invalidOriginalName:   return "The bundle contains an invalid original file name.";
            case Status::invalidFormat:         return "The bundle audio format is not supported or is too long.";
            case Status::tooLong:               return "The bundle manifest is too long.";
        }

        return "Unknown bundle manifest status.";
    }

    MediaManifest::Status MediaManifest::parse (const juce::String& json,
                                                MediaManifest& result,
                                                juce::String& errorMessage)
    {
        errorMessage.clear();

        if (json.getNumBytesAsUTF8() > static_cast<size_t> (BundleFormat::maxManifestBytes))
            return Status::tooLong;

        const auto parsed = juce::JSON::parse (json);
        const auto* root = parsed.getDynamicObject();

        if (root == nullptr)
            return Status::malformedJson;

        juce::int64 schema = 0;

        if (! readInteger (parsed.getProperty ("schema_version", juce::var()), schema))
            return Status::malformedJson;

        if (schema != BundleFormat::schemaVersion)
            return Status::unsupportedSchema;

        const auto* mediaArray = parsed.getProperty ("media", juce::var()).getArray();

        if (mediaArray == nullptr)
            return Status::malformedJson;

        if (mediaArray->size() > BundleFormat::maxMediaEntries)
            return Status::tooManyMediaEntries;

        MediaManifest parsedManifest;
        bool seenLayers[2] { false, false };

        for (const auto& item : *mediaArray)
        {
            if (item.getDynamicObject() == nullptr)
                return Status::malformedJson;

            BundleMediaEntry entry;
            juce::int64 number = 0;

            if (! readInteger (item.getProperty ("layer", juce::var()), number)
                || (number != 0 && number != 1))
                return Status::invalidLayer;

            entry.layer = static_cast<int> (number);

            if (seenLayers[entry.layer])
                return Status::duplicateLayer;

            seenLayers[entry.layer] = true;

            const auto entryVar = item.getProperty ("entry", juce::var());
            entry.entryName = entryVar.toString();

            if (! entryVar.isString() || ! BundleFormat::isMediaEntry (entry.entryName))
                return Status::invalidEntryName;

            const auto hashVar = item.getProperty ("sha256", juce::var());
            entry.sha256 = hashVar.toString();

            if (! hashVar.isString() || ! BundleFormat::isSha256 (entry.sha256))
                return Status::invalidHash;

            const auto extension = entry.entryName.fromLastOccurrenceOf (".", false, false);

            if (entry.entryName != BundleFormat::mediaEntryName (entry.sha256, extension))
                return Status::invalidEntryName;

            if (! readInteger (item.getProperty ("bytes", juce::var()), entry.byteSize)
                || entry.byteSize < 1 || entry.byteSize > BundleFormat::maxMediaBytes)
                return Status::invalidSize;

            const auto nameVar = item.getProperty ("original_name", juce::var());
            entry.originalName = nameVar.toString();

            if (! nameVar.isString() || entry.originalName.isEmpty() || entry.originalName.length() > 255
                || entry.originalName.containsChar ('/') || entry.originalName.containsChar ('\\'))
                return Status::invalidOriginalName;

            for (auto i = 0; i < entry.originalName.length(); ++i)
                if (entry.originalName[i] < 0x20 || entry.originalName[i] == 0x7f)
                    return Status::invalidOriginalName;

            juce::int64 channels = 0, sampleRate = 0, frames = 0;

            if (! readInteger (item.getProperty ("channels", juce::var()), channels)
                || ! readInteger (item.getProperty ("sample_rate", juce::var()), sampleRate)
                || ! readInteger (item.getProperty ("frames", juce::var()), frames))
                return Status::invalidFormat;

            if (channels < 1 || channels > 2 || sampleRate < 8000 || sampleRate > 384000
                || frames < 1 || frames > sampleRate * BundleFormat::maxDecodedSecondsPerLayer)
                return Status::invalidFormat;

            entry.channels = static_cast<int> (channels);
            entry.sampleRate = static_cast<int> (sampleRate);
            entry.lengthFrames = frames;

            parsedManifest.media.push_back (entry);
        }

        result = std::move (parsedManifest);
        return Status::ok;
    }

    juce::String MediaManifest::toJson() const
    {
        juce::Array<juce::var> items;

        for (const auto& entry : media)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("layer", entry.layer);
            object->setProperty ("entry", entry.entryName);
            object->setProperty ("sha256", entry.sha256);
            object->setProperty ("bytes", static_cast<juce::int64> (entry.byteSize));
            object->setProperty ("original_name", entry.originalName);
            object->setProperty ("channels", entry.channels);
            object->setProperty ("sample_rate", entry.sampleRate);
            object->setProperty ("frames", static_cast<juce::int64> (entry.lengthFrames));
            items.add (juce::var (object));
        }

        auto* root = new juce::DynamicObject();
        root->setProperty ("schema_version", BundleFormat::schemaVersion);
        root->setProperty ("media", items);

        return juce::JSON::toString (juce::var (root), false);
    }

    const BundleMediaEntry* MediaManifest::findLayer (int layer) const noexcept
    {
        for (const auto& entry : media)
            if (entry.layer == layer)
                return &entry;

        return nullptr;
    }
}
