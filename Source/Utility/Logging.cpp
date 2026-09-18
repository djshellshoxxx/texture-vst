#include "Logging.h"

#include <memory>

namespace texture
{
    namespace
    {
        juce::CriticalSection& loggerLock()
        {
            static juce::CriticalSection lock;
            return lock;
        }

        std::unique_ptr<juce::FileLogger>& sharedLogger()
        {
            static std::unique_ptr<juce::FileLogger> logger;
            return logger;
        }

        int& clientCount()
        {
            static int count = 0;
            return count;
        }

        /** Renames log.txt to log_1.txt etc. once it grows past the rotation size. */
        void rotateIfNeeded (const juce::File& logFile)
        {
            if (! logFile.existsAsFile() || logFile.getSize() < Logging::rotateAtBytes)
                return;

            const auto directory = logFile.getParentDirectory();
            const auto stem = logFile.getFileNameWithoutExtension();

            // Drop the oldest, then shuffle the rest along.
            directory.getChildFile (stem + "_" + juce::String (Logging::rotatedFilesToKeep) + ".txt")
                     .deleteFile();

            for (int i = Logging::rotatedFilesToKeep - 1; i >= 1; --i)
            {
                const auto from = directory.getChildFile (stem + "_" + juce::String (i) + ".txt");

                if (from.existsAsFile())
                    from.moveFileTo (directory.getChildFile (stem + "_" + juce::String (i + 1) + ".txt"));
            }

            logFile.moveFileTo (directory.getChildFile (stem + "_1.txt"));
        }
    }

    juce::File Logging::getLogDirectory()
    {
        auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                             .getChildFile ("Texture");
        directory.createDirectory();
        return directory;
    }

    juce::File Logging::getLogFile()
    {
        return getLogDirectory().getChildFile ("log.txt");
    }

    juce::String Logging::describe (LogLevel level)
    {
        switch (level)
        {
            case LogLevel::info:  return "info";
            case LogLevel::warn:  return "warn";
            case LogLevel::error: return "error";
        }

        return "info";
    }

    void Logging::addClient()
    {
        const juce::ScopedLock lock (loggerLock());

        if (++clientCount() > 1 && sharedLogger() != nullptr)
            return;

        const auto logFile = getLogFile();
        rotateIfNeeded (logFile);

        sharedLogger() = std::make_unique<juce::FileLogger> (
            logFile,
            "TEXTURE log",
            0);   // rotation is handled here, not by FileLogger

        juce::Logger::setCurrentLogger (sharedLogger().get());
    }

    void Logging::removeClient()
    {
        const juce::ScopedLock lock (loggerLock());

        if (--clientCount() > 0)
            return;

        clientCount() = 0;

        juce::Logger::setCurrentLogger (nullptr);
        sharedLogger().reset();
    }

    void Logging::write (LogLevel level, const juce::String& component, const juce::String& message)
    {
        const auto line = "[" + juce::Time::getCurrentTime().toISO8601 (true) + "] "
                        + "[" + describe (level) + "] "
                        + "[" + component + "] "
                        + message;

        const juce::ScopedLock lock (loggerLock());

        if (sharedLogger() != nullptr)
            sharedLogger()->logMessage (line);

        DBG (line);
    }

    juce::String Logging::readLogText (int maxBytes)
    {
        const juce::ScopedLock lock (loggerLock());

        const auto file = getLogFile();

        if (! file.existsAsFile())
            return "(no log file yet)";

        const auto text = file.loadFileAsString();

        if (text.getNumBytesAsUTF8() <= static_cast<size_t> (maxBytes))
            return text;

        return "... (truncated) ...\n" + text.getLastCharacters (maxBytes);
    }

    void Logging::clearLog()
    {
        const juce::ScopedLock lock (loggerLock());

        const auto directory = getLogDirectory();

        getLogFile().deleteFile();

        for (int i = 1; i <= rotatedFilesToKeep; ++i)
            directory.getChildFile ("log_" + juce::String (i) + ".txt").deleteFile();
    }
}
