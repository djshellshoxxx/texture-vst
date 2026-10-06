#include "TestHelpers.h"

#include "../Source/Utility/MotionPreferences.h"

namespace texture::tests
{
    class MotionPreferencesTests : public juce::UnitTest
    {
    public:
        MotionPreferencesTests()
            : juce::UnitTest ("Motion preferences", "texture") {}

        void runTest() override
        {
            beginTest ("full motion is the default");
            MotionPreferences::setReducedMotion (false);
            expect (! MotionPreferences::isReducedMotion());
            expectEquals (MotionPreferences::animationDurationMs (80), 80);

            beginTest ("reduced motion disables optional animation durations");
            MotionPreferences::setReducedMotion (true);
            expect (MotionPreferences::isReducedMotion());
            expectEquals (MotionPreferences::animationDurationMs (80), 0);
            expectEquals (MotionPreferences::animationDurationMs (300), 0);

            beginTest ("preference can be restored");
            MotionPreferences::setReducedMotion (false);
            expectEquals (MotionPreferences::animationDurationMs (150), 150);
        }
    };

    static MotionPreferencesTests motionPreferencesTests;
}
