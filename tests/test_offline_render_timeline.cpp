#include "TestHelpers.h"

#include "../Source/IO/OfflineRenderTimeline.h"

namespace texture::tests
{
    class OfflineRenderTimelineTests : public juce::UnitTest
    {
    public:
        OfflineRenderTimelineTests()
            : juce::UnitTest ("Offline render timeline", "texture") {}

        void runTest() override
        {
            using Timeline = OfflineRenderTimeline<4>;

            beginTest ("event boundaries split a normal render block exactly");
            const Timeline::Events events { 0, 100, 513, 900 };
            expectEquals (Timeline::nextBoundary (0, 512, events), static_cast<juce::int64> (100));
            expectEquals (Timeline::nextBoundary (100, 612, events), static_cast<juce::int64> (513));
            expectEquals (Timeline::nextBoundary (513, 1025, events), static_cast<juce::int64> (900));
            expectEquals (Timeline::nextBoundary (900, 1412, events), static_cast<juce::int64> (1412));

            beginTest ("events at the current position are applied before rendering");
            expect (Timeline::hasEventAt (0, events));
            expect (Timeline::hasEventAt (100, events));
            expect (! Timeline::hasEventAt (101, events));

            beginTest ("duplicate event positions do not create zero-length spans");
            const Timeline::Events duplicates { 0, 100, 100, 200 };
            expectEquals (Timeline::nextBoundary (100, 612, duplicates), static_cast<juce::int64> (200));

            beginTest ("events outside the current block do not shorten it");
            expectEquals (Timeline::nextBoundary (0, 64, events), static_cast<juce::int64> (64));
        }
    };

    static OfflineRenderTimelineTests offlineRenderTimelineTests;
}
