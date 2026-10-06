#include "TestHelpers.h"

#include "../Source/Utility/MidiLearn.h"

namespace texture::tests
{
    class MidiLearnTests : public juce::UnitTest
    {
    public:
        MidiLearnTests()
            : juce::UnitTest ("MIDI learn", "texture") {}

        void runTest() override
        {
            double nowMs = 1000.0;
            MidiLearn learn (nullptr, [&nowMs] { return nowMs; });

            beginTest ("learning stays armed before the timeout");
            learn.startLearning (params::morph);
            nowMs += MidiLearn::learnTimeoutMs - 1.0;
            expect (! learn.cancelLearningIfTimedOut());
            expect (learn.isLearning());

            beginTest ("learning cancels at the timeout");
            nowMs += 1.0;
            expect (learn.cancelLearningIfTimedOut());
            expect (! learn.isLearning());
            expect (learn.getLearningParameterID().isEmpty());

            beginTest ("a completed binding cancels the timeout state");
            nowMs += 100.0;
            learn.startLearning (params::gainDb);
            learn.requestBinding (21);
            expect (learn.applyPendingBinding());
            expect (! learn.isLearning());
            nowMs += MidiLearn::learnTimeoutMs * 2.0;
            expect (! learn.cancelLearningIfTimedOut());
            expectEquals (learn.getControllerForParameter (params::gainDb), 21);

            beginTest ("reserved controllers do not complete learning");
            learn.startLearning (params::morph);
            learn.requestBinding (64);
            expect (! learn.applyPendingBinding());
            expect (learn.isLearning());
        }
    };

    static MidiLearnTests midiLearnTests;
}
