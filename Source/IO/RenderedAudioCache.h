#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace texture
{
    /** Application-managed store for rendered WAV files that may be dragged to a DAW (F15).

        Files are never deleted implicitly: a DAW may reference the original rather than
        copy it. Only `clearUnleased()`, a user action, removes anything, and it never
        removes a file that a live lease holds. Names are sanitised and carry a content
        ID (a short SHA-256), so a repeated render cannot overwrite a file already
        leased or referenced elsewhere.
    */
    class RenderedAudioCache final
    {
    public:
        /** Default cap from F15. Exceeding it asks the user to clean up or choose another folder. */
        static constexpr juce::int64 defaultCapBytes = 2LL * 1024 * 1024 * 1024;

        RenderedAudioCache (juce::File directory, juce::int64 capBytes = defaultCapBytes);
        ~RenderedAudioCache();

        RenderedAudioCache (const RenderedAudioCache&) = delete;
        RenderedAudioCache& operator= (const RenderedAudioCache&) = delete;

        /** The default location, under the plugin data folder. */
        static juce::File defaultDirectory();

        /** The user's persistent export folder. Changing it never touches existing files. */
        juce::File getDirectory() const;
        void setDirectory (const juce::File& directory);

        juce::int64 getCapBytes() const noexcept { return capBytes; }

        /** A lease keeps a file on disk while held, for example during an OS drag. Copies
            share the lease; it ends when the last copy is destroyed. */
        class Lease;
        std::shared_ptr<Lease> acquireLease (const juce::File& file) const;
        bool isLeased (const juce::File& file) const;

        /** A unique, sanitised destination for a render of about `estimatedBytes`.
            Refused when the total would exceed the cap. The file does not exist yet. */
        struct Reservation
        {
            bool succeeded { false };
            juce::File file;
            juce::String message;
        };

        Reservation reserve (const juce::String& baseName, juce::int64 estimatedBytes) const;

        /** Renames a completed render to `<base>-<content id>.wav`. Identical content that
            is already stored is reused and the new copy is removed. Returns the final file. */
        struct Finalised
        {
            bool succeeded { false };
            juce::File file;
            juce::String message;
        };

        Finalised finalise (const juce::File& written) const;

        struct Entry
        {
            juce::File file;
            juce::int64 bytes { 0 };
            bool leased { false };
        };

        std::vector<Entry> list() const;
        juce::int64 totalBytes() const;

        struct ClearResult
        {
            int removed { 0 };
            int skippedLeased { 0 };
            juce::int64 freedBytes { 0 };
        };

        /** The only deletion path. Removes unleased files in the cache folder, never
            anything outside it. */
        ClearResult clearUnleased() const;

        /** The content ID used in a name: the first 8 hex characters of SHA-256. */
        static juce::String contentId (const juce::File& file);

        /** Makes a file name safe for every platform while keeping its text readable. */
        static juce::String sanitiseBaseName (const juce::String& name);

    private:
        class Registry;

        juce::File directory;
        juce::int64 capBytes;
        std::shared_ptr<Registry> registry;
    };

    class RenderedAudioCache::Lease final
    {
    public:
        ~Lease();
        const juce::File& getFile() const noexcept { return file; }

    private:
        friend class RenderedAudioCache;
        Lease (std::shared_ptr<Registry> owner, juce::File leased);

        std::shared_ptr<Registry> owner;
        juce::File file;
    };
}
