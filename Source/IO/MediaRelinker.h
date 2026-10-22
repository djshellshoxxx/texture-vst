#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <vector>

namespace texture
{
    struct MediaRelinkLimits
    {
        int maxDepth { 4 };
        int maxFilesVisited { 5000 };
        int maxHashes { 64 };
        int maxCandidates { 32 };
    };

    /** Bounded search of one user-chosen folder for a missing sample (F05).

        A content (SHA-256) match is the strong result. A filename-only match is weaker:
        the caller must audition it and obtain explicit confirmation before use. The
        search never leaves the folder it was given, follows no links, and skips
        filesystem roots, so it cannot quietly scan the whole computer.
    */
    class MediaRelinker final
    {
    public:
        struct Expectation
        {
            juce::String sha256;             // empty when unknown
            juce::int64 byteSize { -1 };     // negative when unknown
            juce::String fileName;           // empty when unknown
        };

        enum class MatchKind { content, name };

        struct Candidate
        {
            juce::File file;
            MatchKind kind { MatchKind::name };
        };

        using Limits = MediaRelinkLimits;

        struct Result
        {
            bool cancelled { false };
            bool limitReached { false };
            int filesVisited { 0 };
            /** Content matches first, then filename-only matches. */
            std::vector<Candidate> candidates;
        };

        static bool isSearchableFolder (const juce::File& folder);

        static Result search (const juce::File& folder,
                              const Expectation& expectation,
                              const std::atomic<bool>& cancelled,
                              const Limits& limits = {});
    };
}
