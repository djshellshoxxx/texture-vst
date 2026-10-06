#include "TestHelpers.h"

#include "../Source/Utility/ControlValueClipboard.h"

#include <limits>

namespace texture::tests
{
    class ControlValueClipboardTests : public juce::UnitTest
    {
    public:
        ControlValueClipboardTests()
            : juce::UnitTest ("Control value clipboard", "texture") {}

        void runTest() override
        {
            ControlValueClipboard::clear();

            beginTest ("empty clipboard cannot paste");
            expect (! ControlValueClipboard::canPaste ("gain_db", -24.0, 6.0));

            beginTest ("same parameter and valid range can paste");
            ControlValueClipboard::copy ("gain_db", -3.5);
            expect (ControlValueClipboard::canPaste ("gain_db", -24.0, 6.0));
            const auto gain = ControlValueClipboard::valueFor ("gain_db", -24.0, 6.0);
            expect (gain.has_value());
            if (gain.has_value())
                expectWithinAbsoluteError (*gain, -3.5, 1.0e-12);

            beginTest ("different parameter identity is rejected");
            expect (! ControlValueClipboard::canPaste ("attack", 1.0, 5000.0));
            expect (! ControlValueClipboard::valueFor ("attack", 1.0, 5000.0).has_value());

            beginTest ("out-of-range stored value is rejected");
            ControlValueClipboard::copy ("gain_db", 99.0);
            expect (! ControlValueClipboard::canPaste ("gain_db", -24.0, 6.0));

            beginTest ("non-finite stored value is rejected");
            ControlValueClipboard::copy ("gain_db", std::numeric_limits<double>::infinity());
            expect (! ControlValueClipboard::canPaste ("gain_db", -24.0, 6.0));
        }
    };

    static ControlValueClipboardTests controlValueClipboardTests;
}
