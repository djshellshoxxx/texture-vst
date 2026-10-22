#pragma once

#include <juce_core/juce_core.h>

namespace texture
{
    /** Content-addressed sample storage for imported sound bundles (F05).

        Files are named by their SHA-256, so identical audio is stored once and a file
        can be checked against the hash recorded for it. The store never deletes
        anything: a file another preset references must stay until the user removes it.
    */
    class MediaStore final
    {
    public:
        explicit MediaStore (juce::File directory);

        /** `<plugin data>/Media`. */
        static juce::File defaultDirectory();

        const juce::File& getDirectory() const noexcept { return directory; }

        /** Where content with this hash and extension lives. Pure path arithmetic. */
        juce::File fileFor (const juce::String& sha256, const juce::String& extension) const;

        struct CommitResult
        {
            bool succeeded { false };
            juce::File file;
            juce::String message;
        };

        /** Copies verified staged content to its content-addressed name. Existing
            content with the same hash is reused without a second copy. The copy is
            hashed again before it is published, so a corrupt staging file never
            reaches the store. */
        CommitResult commit (const juce::File& staged,
                             const juce::String& sha256,
                             const juce::String& extension) const;

    private:
        juce::File directory;
    };
}
