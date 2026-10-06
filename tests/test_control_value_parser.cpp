#include "TestHelpers.h"

#include "../Source/Utility/ControlValueParser.h"

namespace texture::tests
{
    class ControlValueParserTests : public juce::UnitTest
    {
    public:
        ControlValueParserTests()
            : juce::UnitTest ("Control value parser", "texture") {}

        void runTest() override
        {
            beginTest ("valid decimal values are accepted");
            const auto decimal = ControlValueParser::parse ("  -3.25  ", -24.0, 6.0);
            expect (decimal.has_value());
            if (decimal.has_value())
                expectWithinAbsoluteError (*decimal, -3.25, 1.0e-9);

            beginTest ("boundary values are accepted");
            expect (ControlValueParser::parse ("-24", -24.0, 6.0).has_value());
            expect (ControlValueParser::parse ("6.0", -24.0, 6.0).has_value());

            beginTest ("out of range values are rejected instead of clamped");
            expect (! ControlValueParser::parse ("6.01", -24.0, 6.0).has_value());
            expect (! ControlValueParser::parse ("-24.01", -24.0, 6.0).has_value());

            beginTest ("malformed values are rejected");
            for (const auto* text : { "", " ", "abc", "abc1", "1abc", "--1", "1.2.3", "NaN", "inf" })
                expect (! ControlValueParser::parse (text, -24.0, 6.0).has_value(),
                        juce::String ("should reject: ") + text);

            beginTest ("scientific notation is accepted when finite and in range");
            const auto scientific = ControlValueParser::parse ("1e-2", 0.0, 1.0);
            expect (scientific.has_value());
            if (scientific.has_value())
                expectWithinAbsoluteError (*scientific, 0.01, 1.0e-12);
        }
    };

    static ControlValueParserTests controlValueParserTests;
}
