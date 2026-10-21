#include "AtomicFileWriter.h"

namespace texture
{
    AtomicFileWriter::AtomicFileWriter (const juce::File& target)
        : destination (target)
    {
    }

    AtomicFileWriter::~AtomicFileWriter()
    {
        abandon();
    }

    std::unique_ptr<juce::FileOutputStream> AtomicFileWriter::openStream (juce::String& errorMessage)
    {
        errorMessage.clear();

        if (destination == juce::File() || destination.isDirectory())
        {
            errorMessage = "No valid destination was chosen.";
            return nullptr;
        }

        const auto directory = destination.getParentDirectory();

        if (! directory.isDirectory() && ! directory.createDirectory())
        {
            errorMessage = "Could not create " + directory.getFullPathName() + ".";
            return nullptr;
        }

        // A sibling in the same directory keeps the final rename on one volume.
        temporary = directory.getChildFile ("." + destination.getFileName() + "."
                                            + juce::Uuid().toDashedString() + ".partial");
        committed = false;

        auto stream = std::make_unique<juce::FileOutputStream> (temporary);

        if (! stream->openedOk())
        {
            errorMessage = "Could not write to " + directory.getFullPathName() + ".";
            temporary = juce::File();
            return nullptr;
        }

        return stream;
    }

    AtomicFileWriter::Status AtomicFileWriter::commit (juce::int64 expectedBytes, juce::String& errorMessage)
    {
        errorMessage.clear();

        if (committed || ! temporary.existsAsFile())
        {
            errorMessage = "Nothing was written.";
            return Status::cannotWrite;
        }

        if (expectedBytes >= 0 && temporary.getSize() != expectedBytes)
        {
            errorMessage = "The file was incomplete when writing finished.";
            abandon();
            return Status::sizeMismatch;
        }

        if (! temporary.replaceFileIn (destination))
        {
            errorMessage = "Could not replace " + destination.getFileName()
                         + ". The file may be open in another application.";
            abandon();
            return Status::cannotReplace;
        }

        committed = true;
        temporary = juce::File();
        return Status::ok;
    }

    void AtomicFileWriter::abandon() noexcept
    {
        if (! committed && temporary != juce::File())
        {
            temporary.deleteFile();
            temporary = juce::File();
        }
    }

    juce::String AtomicFileWriter::describe (Status status)
    {
        switch (status)
        {
            case Status::ok:                     return "OK";
            case Status::cannotCreateDirectory:  return "The destination folder could not be created.";
            case Status::cannotCreateTemporary:  return "A temporary file could not be created.";
            case Status::cannotWrite:            return "The file could not be written.";
            case Status::sizeMismatch:           return "The file was incomplete when writing finished.";
            case Status::cannotReplace:          return "The destination could not be replaced.";
        }

        return "Unknown write status.";
    }
}
