/*
    The unit test runner.

    Uses juce::UnitTest so there is no external framework to install
    (17_TESTING.md). CTest invokes this binary once per category.
*/

#include <juce_events/juce_events.h>

#include <cstdio>

int main (int argc, char* argv[])
{
    // A message loop and the JUCE singletons: the async sample loader needs both.
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (false);

    const juce::String category = argc > 1 ? juce::String (juce::CharPointer_UTF8 (argv[1]))
                                           : juce::String();

    if (category.isNotEmpty())
        runner.runTestsInCategory (category);
    else
        runner.runAllTests();

    int failures = 0;
    int passes = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* result = runner.getResult (i);

        if (result == nullptr)
            continue;

        failures += result->failures;
        passes += result->passes;

        if (result->failures > 0)
        {
            std::printf ("FAILED: %s / %s (%d failures)\n",
                         result->unitTestName.toRawUTF8(),
                         result->subcategoryName.toRawUTF8(),
                         result->failures);

            for (const auto& message : result->messages)
                std::printf ("    %s\n", message.toRawUTF8());
        }
    }

    std::printf ("\nTEXTURE tests: %d passed, %d failed\n", passes, failures);

    return failures > 0 ? 1 : 0;
}
