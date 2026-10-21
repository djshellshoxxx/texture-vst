#include "TestHelpers.h"
#include "../Source/Utility/VersionedRandomStreams.h"

namespace texture::tests
{
    class VersionedRandomStreamsTests final : public juce::UnitTest
    {
    public:
        VersionedRandomStreamsTests() : juce::UnitTest ("Versioned random streams", "texture") {}

        void runTest() override
        {
            using Streams = VersionedRandomStreams;
            constexpr auto lane = Streams::Lane::grainPosition;
            beginTest ("algorithm v1 golden vectors");
            expectEquals (Streams::value (0, lane, 0, 0), UINT64_C (0x4396D60DBD8537AF));
            expectEquals (Streams::value (UINT64_C (0x123456789ABCDEF0), lane, 7, 3), UINT64_C (0xA9C45E969CF26BBC));

            beginTest ("same coordinates reproduce and distinct lanes are isolated");
            const auto first = Streams::value (42, lane, 9, 2);
            expectEquals (Streams::value (42, lane, 9, 2), first);
            expect (Streams::value (42, Streams::Lane::pan, 9, 2) != first);
            expect (Streams::value (42, lane, 10, 2) != first);
            expect (Streams::value (42, lane, 9, 3) != first);

            beginTest ("seed metadata uses a strict sixteen-digit hex encoding");
            const auto text = Streams::seedToHex (UINT64_C (0xFEDCBA9876543210));
            expectEquals (juce::String (text), juce::String ("fedcba9876543210"));
            std::uint64_t parsed = 0;
            expect (Streams::parseSeedHex (text, parsed));
            expectEquals (parsed, UINT64_C (0xFEDCBA9876543210));
            expect (! Streams::parseSeedHex ("123", parsed));
            expect (! Streams::parseSeedHex ("zzzzzzzzzzzzzzzz", parsed));

            beginTest ("unit floats stay in [0, 1)");
            for (std::uint64_t i = 0; i < 10000; ++i)
            {
                const auto value = Streams::unitFloat (UINT64_C (0xFEDCBA9876543210),
                                                       Streams::Lane::reverse, 123, i);
                expect (value >= 0.0f && value < 1.0f);
            }
        }
    };

    static VersionedRandomStreamsTests versionedRandomStreamsTests;
}
