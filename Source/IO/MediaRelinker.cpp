#include "MediaRelinker.h"
#include <juce_cryptography/juce_cryptography.h>

#include "MediaManifest.h"

#include <deque>

namespace texture
{
    bool MediaRelinker::isSearchableFolder (const juce::File& folder)
    {
        return folder != juce::File() && ! folder.isRoot() && folder.isDirectory() && ! folder.isSymbolicLink();
    }

    MediaRelinker::Result MediaRelinker::search (const juce::File& folder,
                                                 const Expectation& expectation,
                                                 const std::atomic<bool>& cancelled,
                                                 const Limits& limits)
    {
        Result result;

        if (! isSearchableFolder (folder))
            return result;

        const auto hasHash = BundleFormat::isSha256 (expectation.sha256);
        const auto hasName = expectation.fileName.isNotEmpty();
        std::vector<Candidate> nameMatches;

        // Breadth-first, so shallow matches are found before the depth limit is reached.
        std::deque<std::pair<juce::File, int>> pending { { folder, 0 } };
        int hashesComputed = 0;

        while (! pending.empty())
        {
            if (cancelled.load())
            {
                result.cancelled = true;
                break;
            }

            const auto [directory, depth] = pending.front();
            pending.pop_front();

            for (const auto& child : directory.findChildFiles (juce::File::findFilesAndDirectories, false))
            {
                if (cancelled.load())
                {
                    result.cancelled = true;
                    break;
                }

                if (child.isSymbolicLink() || child.isHidden())
                    continue;

                if (child.isDirectory())
                {
                    if (depth + 1 <= limits.maxDepth)
                        pending.emplace_back (child, depth + 1);

                    continue;
                }

                if (++result.filesVisited > limits.maxFilesVisited)
                {
                    result.limitReached = true;
                    pending.clear();
                    break;
                }

                const auto extension = child.getFileExtension().fromFirstOccurrenceOf (".", false, false).toLowerCase();

                if (! BundleFormat::isAllowedExtension (extension))
                    continue;

                if (hasHash
                    && (expectation.byteSize < 0 || child.getSize() == expectation.byteSize)
                    && hashesComputed < limits.maxHashes)
                {
                    ++hashesComputed;

                    if (juce::SHA256 (child).toHexString() == expectation.sha256)
                    {
                        result.candidates.push_back ({ child, MatchKind::content });
                        continue;
                    }
                }
                else if (hasHash && hashesComputed >= limits.maxHashes)
                {
                    result.limitReached = true;
                }

                if (hasName && child.getFileName().equalsIgnoreCase (expectation.fileName))
                    nameMatches.push_back ({ child, MatchKind::name });

                if (static_cast<int> (result.candidates.size() + nameMatches.size()) >= limits.maxCandidates)
                {
                    result.limitReached = true;
                    pending.clear();
                    break;
                }
            }
        }

        for (auto& match : nameMatches)
            result.candidates.push_back (match);

        return result;
    }
}
