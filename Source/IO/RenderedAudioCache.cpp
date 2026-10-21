#include "RenderedAudioCache.h"

#include "../Presets/PresetManager.h"

#include <juce_cryptography/juce_cryptography.h>

#include <map>

namespace texture
{
    class RenderedAudioCache::Registry final
    {
    public:
        void add (const juce::String& key)
        {
            const juce::ScopedLock sl (lock);
            ++counts[key];
        }

        void remove (const juce::String& key)
        {
            const juce::ScopedLock sl (lock);
            const auto found = counts.find (key);

            if (found == counts.end())
                return;

            if (--found->second <= 0)
                counts.erase (found);
        }

        bool contains (const juce::String& key) const
        {
            const juce::ScopedLock sl (lock);
            return counts.find (key) != counts.end();
        }

    private:
        mutable juce::CriticalSection lock;
        std::map<juce::String, int> counts;
    };

    RenderedAudioCache::Lease::Lease (std::shared_ptr<Registry> registry, juce::File leased)
        : owner (std::move (registry)),
          file (std::move (leased))
    {
    }

    RenderedAudioCache::Lease::~Lease()
    {
        owner->remove (file.getFullPathName());
    }

    RenderedAudioCache::RenderedAudioCache (juce::File target, juce::int64 cap)
        : directory (target),
          capBytes (cap),
          registry (std::make_shared<Registry>())
    {
    }

    RenderedAudioCache::~RenderedAudioCache() = default;

    juce::File RenderedAudioCache::defaultDirectory()
    {
        return PresetManager::getPluginDataDirectory().getChildFile ("Rendered");
    }

    juce::File RenderedAudioCache::getDirectory() const
    {
        return directory;
    }

    void RenderedAudioCache::setDirectory (const juce::File& target)
    {
        // Only where new renders go. Files already in the old folder stay put.
        directory = target;
    }

    std::shared_ptr<RenderedAudioCache::Lease> RenderedAudioCache::acquireLease (const juce::File& file) const
    {
        registry->add (file.getFullPathName());
        return std::shared_ptr<Lease> (new Lease (registry, file));
    }

    bool RenderedAudioCache::isLeased (const juce::File& file) const
    {
        return registry->contains (file.getFullPathName());
    }

    RenderedAudioCache::Reservation RenderedAudioCache::reserve (const juce::String& baseName,
                                                                 juce::int64 estimatedBytes) const
    {
        Reservation result;

        if (estimatedBytes < 0)
        {
            result.message = "The render size is unknown.";
            return result;
        }

        if (totalBytes() + estimatedBytes > capBytes)
        {
            result.message = "The rendered audio folder is full (" + juce::File::descriptionOfSizeInBytes (totalBytes())
                           + " of " + juce::File::descriptionOfSizeInBytes (capBytes)
                           + " used). Clear unused renders or choose another folder.";
            return result;
        }

        if (! directory.createDirectory())
        {
            result.message = "The rendered audio folder could not be created: " + directory.getFullPathName();
            return result;
        }

        const auto stem = sanitiseBaseName (baseName);

        for (int attempt = 0; attempt < 100; ++attempt)
        {
            const auto candidate = directory.getChildFile (stem + "-" + juce::Uuid().toString().substring (0, 8) + ".wav");

            if (! candidate.exists())
            {
                result.succeeded = true;
                result.file = candidate;
                return result;
            }
        }

        result.message = "A unique file name could not be found.";
        return result;
    }

    RenderedAudioCache::Finalised RenderedAudioCache::finalise (const juce::File& written) const
    {
        Finalised result;

        if (! written.existsAsFile())
        {
            result.message = "The render is missing.";
            return result;
        }

        const auto folder = written.getParentDirectory();
        const auto id = contentId (written);
        const auto stem = written.getFileNameWithoutExtension().upToLastOccurrenceOf ("-", false, false);
        const auto target = folder.getChildFile (stem + "-" + id + ".wav");

        if (target == written)
        {
            result.succeeded = true;
            result.file = written;
            return result;
        }

        // Identical audio already stored: reuse it rather than keep two copies.
        if (target.existsAsFile() && juce::SHA256 (target).toHexString() == juce::SHA256 (written).toHexString())
        {
            written.deleteFile();
            result.succeeded = true;
            result.file = target;
            return result;
        }

        // A different file already holds that name: never overwrite it.
        auto destination = target;

        for (int attempt = 0; destination.exists() && attempt < 100; ++attempt)
            destination = folder.getChildFile (stem + "-" + id + "-" + juce::Uuid().toString().substring (0, 8) + ".wav");

        if (destination.exists() || ! written.moveFileTo (destination))
        {
            result.message = "The render could not be named and stored.";
            return result;
        }

        result.succeeded = true;
        result.file = destination;
        return result;
    }

    std::vector<RenderedAudioCache::Entry> RenderedAudioCache::list() const
    {
        std::vector<Entry> entries;

        for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, "*.wav"))
            entries.push_back ({ file, file.getSize(), isLeased (file) });

        return entries;
    }

    juce::int64 RenderedAudioCache::totalBytes() const
    {
        juce::int64 total = 0;

        for (const auto& entry : list())
            total += entry.bytes;

        return total;
    }

    RenderedAudioCache::ClearResult RenderedAudioCache::clearUnleased() const
    {
        ClearResult result;

        for (const auto& entry : list())
        {
            if (entry.leased)
            {
                ++result.skippedLeased;
                continue;
            }

            if (entry.file.deleteFile())
            {
                ++result.removed;
                result.freedBytes += entry.bytes;
            }
        }

        return result;
    }

    juce::String RenderedAudioCache::contentId (const juce::File& file)
    {
        return juce::SHA256 (file).toHexString().substring (0, 8);
    }

    juce::String RenderedAudioCache::sanitiseBaseName (const juce::String& name)
    {
        auto cleaned = juce::File::createLegalFileName (name).trim();

        // A leading dot would hide the file, and the dash is reserved for the content ID.
        while (cleaned.startsWithChar ('.'))
            cleaned = cleaned.substring (1);

        cleaned = cleaned.replaceCharacter ('-', '_');

        if (cleaned.isEmpty())
            cleaned = "Render";

        return cleaned.substring (0, 60);
    }
}
