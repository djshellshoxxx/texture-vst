#include "TestHelpers.h"

#include "../Source/IO/RenderedAudioCache.h"

namespace texture::tests
{
    class RenderedAudioCacheTests : public juce::UnitTest
    {
    public:
        RenderedAudioCacheTests()
            : juce::UnitTest ("Rendered audio cache", "texture") {}

        void runTest() override
        {
            beginTest ("reservations are unique, sanitised and never overwrite an existing file");
            {
                TemporaryDirectory temp;
                RenderedAudioCache cache (temp.child ("rendered"));

                const auto first = cache.reserve ("Kick/Pad: final?", 100);
                expect (first.succeeded, first.message);
                expect (first.file.isAChildOf (temp.child ("rendered")), "a reservation stays in the cache folder");
                expect (first.file.getFileName().endsWith (".wav"));
                expect (! first.file.getFileName().containsChar ('/') && ! first.file.getFileName().containsChar (':'));
                expect (! first.file.existsAsFile(), "the reservation does not create the file");

                first.file.replaceWithText ("placeholder");
                const auto second = cache.reserve ("Kick/Pad: final?", 100);
                expect (second.succeeded && second.file != first.file, "repeated names must differ");

                expect (RenderedAudioCache::sanitiseBaseName ("..hidden-name").startsWith ("hidden_name"));
                expect (RenderedAudioCache::sanitiseBaseName ("").isNotEmpty());
                expect (RenderedAudioCache::sanitiseBaseName ("a-b").containsChar ('-') == false);
            }

            beginTest ("unicode folders and file names are supported");
            {
                TemporaryDirectory temp;
                const auto folder = temp.child (juce::String::fromUTF8 ("M\xc3\xbcsik \xe2\x9c\x93"));
                RenderedAudioCache cache (folder);
                const auto reserved = cache.reserve (juce::String::fromUTF8 ("Pad \xc3\xa9t\xc3\xa9"), 10);
                expect (reserved.succeeded, reserved.message);
                expect (reserved.file.isAChildOf (folder));
                reserved.file.replaceWithText ("x");
                expect (cache.list().size() == 1);
            }

            beginTest ("a finished render is named by its content and identical audio is reused");
            {
                TemporaryDirectory temp;
                RenderedAudioCache cache (temp.child ("cache"));

                const auto a = cache.reserve ("Bell", 16);
                a.file.replaceWithText ("same audio");
                const auto finalA = cache.finalise (a.file);
                expect (finalA.succeeded, finalA.message);
                expect (finalA.file.getFileName().startsWith ("Bell-"));
                expect (finalA.file.getFileName().endsWith (RenderedAudioCache::contentId (finalA.file) + ".wav"));
                expect (! a.file.existsAsFile() || a.file == finalA.file, "the temporary name is gone");

                const auto b = cache.reserve ("Bell", 16);
                b.file.replaceWithText ("same audio");
                const auto finalB = cache.finalise (b.file);
                expect (finalB.succeeded && finalB.file == finalA.file, "identical content is stored once");
                expect (! b.file.existsAsFile());
                expectEquals (static_cast<int> (cache.list().size()), 1);

                const auto c = cache.reserve ("Bell", 16);
                c.file.replaceWithText ("different audio");
                const auto finalC = cache.finalise (c.file);
                expect (finalC.succeeded && finalC.file != finalA.file, "different audio gets its own file");
                expect (finalA.file.loadFileAsString() == "same audio", "the earlier render is never overwritten");
            }

            beginTest ("a leased file survives clearing and a lease ends with its last copy");
            {
                TemporaryDirectory temp;
                RenderedAudioCache cache (temp.child ("lease"));
                const auto reserved = cache.reserve ("Drag", 8);
                reserved.file.replaceWithText ("drag me");
                const auto finalised = cache.finalise (reserved.file);
                expect (finalised.succeeded);

                {
                    auto lease = cache.acquireLease (finalised.file);
                    auto copy = lease;
                    expect (cache.isLeased (finalised.file));
                    const auto clear = cache.clearUnleased();
                    expectEquals (clear.removed, 0);
                    expectEquals (clear.skippedLeased, 1);
                    expect (finalised.file.existsAsFile(), "a leased file is never deleted");

                    lease.reset();
                    expect (cache.isLeased (finalised.file), "a copy keeps the lease alive");
                }

                expect (! cache.isLeased (finalised.file), "the lease ends with its last copy");
                const auto cleared = cache.clearUnleased();
                expectEquals (cleared.removed, 1);
                expect (cleared.freedBytes > 0);
                expect (! finalised.file.existsAsFile());
            }

            beginTest ("the cache refuses a render that would exceed its cap, and says what to do");
            {
                TemporaryDirectory temp;
                RenderedAudioCache cache (temp.child ("capped"), 1000);

                const auto fits = cache.reserve ("A", 900);
                expect (fits.succeeded);
                fits.file.replaceWithText (juce::String::repeatedString ("x", 600));

                const auto refused = cache.reserve ("B", 600);
                expect (! refused.succeeded);
                expect (refused.message.contains ("full"), refused.message);
                expect (refused.message.contains ("Clear unused renders"), refused.message);
                expect (! refused.file.existsAsFile());

                RenderedAudioCache elsewhere (temp.child ("other"), 1000);
                expect (elsewhere.reserve ("B", 600).succeeded, "a different folder has its own cap");
            }

            beginTest ("nothing is deleted implicitly, and clearing touches only cache-owned WAV files");
            {
                TemporaryDirectory temp;
                const auto folder = temp.child ("owned");
                {
                    RenderedAudioCache cache (folder);
                    const auto reserved = cache.reserve ("Keep", 4);
                    reserved.file.replaceWithText ("keep");
                    expect (cache.finalise (reserved.file).succeeded);
                }

                folder.getChildFile ("notes.txt").replaceWithText ("not ours");
                temp.child ("outside.wav").replaceWithText ("outside");

                RenderedAudioCache reopened (folder);
                expectEquals (static_cast<int> (reopened.list().size()), 1);
                expectEquals (reopened.clearUnleased().removed, 1);
                expect (folder.getChildFile ("notes.txt").existsAsFile(), "non-WAV files are never touched");
                expect (temp.child ("outside.wav").existsAsFile(), "files outside the folder are never touched");
            }

            beginTest ("changing the export folder affects only new renders");
            {
                TemporaryDirectory temp;
                RenderedAudioCache cache (temp.child ("first"));
                const auto before = cache.reserve ("Old", 4);
                before.file.replaceWithText ("old");
                const auto finalBefore = cache.finalise (before.file);
                expect (finalBefore.succeeded);

                cache.setDirectory (temp.child ("second"));
                const auto after = cache.reserve ("New", 4);
                expect (after.file.isAChildOf (temp.child ("second")));
                expect (finalBefore.file.existsAsFile(), "existing renders stay where they are");
            }

            beginTest ("a missing or unreadable render is reported, not published");
            {
                TemporaryDirectory temp;
                RenderedAudioCache cache (temp.child ("missing"));
                const auto result = cache.finalise (temp.child ("missing/none.wav"));
                expect (! result.succeeded);
                expect (result.message.isNotEmpty());
                expect (! cache.reserve ("X", -1).succeeded);
            }
        }
    };

    static RenderedAudioCacheTests renderedAudioCacheTests;
}
