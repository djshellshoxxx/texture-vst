#pragma once

#include <juce_core/juce_core.h>

namespace texture
{
    /** Log severity, printed into the line prefix. */
    enum class LogLevel { info, warn, error };

    /** Process-wide file logger.

        One logger is shared by every plugin instance, installed by the first
        processor to be constructed and removed by the last to be destroyed. Lines
        are `[ISO8601] [level] [component] message` (16_ERROR_HANDLING.md).

        Nothing here may be called from the audio thread: it allocates and does file
        I/O (21_WHAT_NOT_TO_DO.md, item 3).
    */
    class Logging
    {
    public:
        /** Rotate once the active log passes this size. */
        static constexpr juce::int64 rotateAtBytes = 1024 * 1024;

        /** Keep this many rotated files alongside the active one. */
        static constexpr int rotatedFilesToKeep = 3;

        /** Installs the shared logger if it is not already installed. Ref-counted. */
        static void addClient();

        /** Balances addClient. Removes the logger when the last client goes away. */
        static void removeClient();

        /** Writes one line. Safe to call before addClient (the line is dropped). */
        static void write (LogLevel level, const juce::String& component, const juce::String& message);

        /** `<user data dir>/Texture/log.txt`. */
        static juce::File getLogFile();

        /** Directory holding the log and any crash reports. */
        static juce::File getLogDirectory();

        /** Entire current log as text, for the "copy to clipboard" button. */
        static juce::String readLogText (int maxBytes = 256 * 1024);

        /** Deletes the active log and its rotations. */
        static void clearLog();

        static juce::String describe (LogLevel level);
    };

    inline void logInfo  (const juce::String& component, const juce::String& message) { Logging::write (LogLevel::info,  component, message); }
    inline void logWarn  (const juce::String& component, const juce::String& message) { Logging::write (LogLevel::warn,  component, message); }
    inline void logError (const juce::String& component, const juce::String& message) { Logging::write (LogLevel::error, component, message); }
}
