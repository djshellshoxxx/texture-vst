#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace texture
{
    /** Writes a file through a sibling temporary file and replaces the destination only
        after every write, flush and size check has succeeded (F05, F06).

        A failure or cancellation removes the temporary file and leaves any existing
        destination untouched. Message-thread and worker-thread safe as long as one
        instance is used from one thread at a time.
    */
    class AtomicFileWriter final
    {
    public:
        enum class Status
        {
            ok,
            cannotCreateDirectory,
            cannotCreateTemporary,
            cannotWrite,
            sizeMismatch,
            cannotReplace
        };

        explicit AtomicFileWriter (const juce::File& destination);
        ~AtomicFileWriter();

        AtomicFileWriter (const AtomicFileWriter&) = delete;
        AtomicFileWriter& operator= (const AtomicFileWriter&) = delete;

        /** Creates the temporary file and returns a stream into it, or nullptr with
            `errorMessage` set. Ownership of the stream passes to the caller, who must
            release or delete it before commit().
        */
        std::unique_ptr<juce::FileOutputStream> openStream (juce::String& errorMessage);

        /** Replaces the destination with the temporary file. When `expectedBytes` is
            non-negative the temporary file must have exactly that size. Call only after
            the stream has been closed. Returns the outcome; on failure the temporary
            file is removed.
        */
        Status commit (juce::int64 expectedBytes, juce::String& errorMessage);

        /** Removes the temporary file if the writer was not committed. */
        void abandon() noexcept;

        const juce::File& getTemporaryFile() const noexcept { return temporary; }
        const juce::File& getDestination() const noexcept { return destination; }

        static juce::String describe (Status status);

    private:
        juce::File destination;
        juce::File temporary;
        bool committed { false };
    };
}
