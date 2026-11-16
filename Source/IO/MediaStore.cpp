#include "MediaStore.h"
#include <juce_cryptography/juce_cryptography.h>

#include "AtomicFileWriter.h"
#include "MediaManifest.h"

namespace texture
{
    MediaStore::MediaStore (juce::File target)
        : directory (target)
    {
    }

    juce::File MediaStore::defaultDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Texture")
                   .getChildFile ("Media");
    }

    juce::File MediaStore::fileFor (const juce::String& sha256, const juce::String& extension) const
    {
        return directory.getChildFile (sha256 + "." + extension.toLowerCase());
    }

    MediaStore::CommitResult MediaStore::commit (const juce::File& staged,
                                                 const juce::String& sha256,
                                                 const juce::String& extension) const
    {
        CommitResult result;
        result.file = fileFor (sha256, extension);

        if (! BundleFormat::isSha256 (sha256) || ! BundleFormat::isAllowedExtension (extension.toLowerCase()))
        {
            result.message = "The sample has an invalid content identity.";
            return result;
        }

        if (! staged.existsAsFile())
        {
            result.message = "The staged sample is missing.";
            return result;
        }

        const auto size = staged.getSize();

        // Reuse identical content already in the store.
        if (result.file.existsAsFile() && result.file.getSize() == size
            && juce::SHA256 (result.file).toHexString() == sha256)
        {
            result.succeeded = true;
            return result;
        }

        if (! directory.createDirectory())
        {
            result.message = "The sample library folder could not be created.";
            return result;
        }

        AtomicFileWriter writer (result.file);
        juce::String error;
        auto output = writer.openStream (error);

        if (output == nullptr)
        {
            result.message = error;
            return result;
        }

        juce::FileInputStream input (staged);

        if (! input.openedOk() || output->writeFromInputStream (input, -1) != size)
        {
            output->flush();
            output.reset();
            writer.abandon();
            result.message = "The sample could not be copied into the library.";
            return result;
        }

        output->flush();
        const auto flushed = output->getStatus().wasOk();
        output.reset();

        if (! flushed || juce::SHA256 (writer.getTemporaryFile()).toHexString() != sha256)
        {
            writer.abandon();
            result.message = "The copied sample did not match its content hash.";
            return result;
        }

        if (writer.commit (size, error) != AtomicFileWriter::Status::ok)
        {
            result.message = error;
            return result;
        }

        result.succeeded = true;
        return result;
    }
}
