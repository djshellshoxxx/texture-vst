#include "TestHelpers.h"

#include "../Source/IO/ExportPlanner.h"

namespace texture::tests
{
    namespace
    {
        ExportPlanner::Request request (ExportPlanner::Mode mode)
        {
            ExportPlanner::Request value;
            value.mode = mode;
            value.note = 60;
            value.velocity = 96;
            value.holdSeconds = 2.0;
            value.tailSeconds = 4.0;
            value.repeats = 1;
            value.gapSeconds = 0.5;
            value.chordShape = 1;
            value.sampleRate = 48000.0;
            return value;
        }

        int countEvents (const ExportPlanner::Plan& plan, bool on)
        {
            int count = 0;

            for (const auto& event : plan.events)
                if (event.on == on)
                    ++count;

            return count;
        }
    }

    class ExportPlannerTests : public juce::UnitTest
    {
    public:
        ExportPlannerTests()
            : juce::UnitTest ("Export planner", "texture") {}

        void runTest() override
        {
            beginTest ("legacy two-hit layout keeps the original sample boundaries exactly");
            // hold 1.5 s, tail 4 s, gap 0.5 s, release 1.25 s at 48 kHz (dyadic values, so
            // the expected samples are exact): first hit 3.25 s, second 6.75 s, total 10.5 s.
            const auto legacy = ExportPlanner::planLegacy (48000.0, 1.5, 4.0, 0.5, 1.25, 60, 0.9f);
            expect (legacy.valid);
            expectEquals (legacy.totalSamples, static_cast<juce::int64> (504000));
            expectEquals (static_cast<int> (legacy.events.size()), 4);
            if (legacy.events.size() == 4)
            {
                expectEquals (legacy.events[0].sample, static_cast<juce::int64> (0));
                expect (legacy.events[0].on);
                expectEquals (legacy.events[1].sample, static_cast<juce::int64> (72000));
                expect (! legacy.events[1].on);
                expectEquals (legacy.events[2].sample, static_cast<juce::int64> (180000));
                expect (legacy.events[2].on);
                expectEquals (legacy.events[3].sample, static_cast<juce::int64> (252000));
                expect (! legacy.events[3].on);
                expectWithinAbsoluteError (legacy.events[2].velocity, 0.9f, 1.0e-6f);
            }

            beginTest ("legacy output is capped at 60 seconds, as before");
            const auto longLegacy = ExportPlanner::planLegacy (48000.0, 30.0, 30.0, 5.0, 8.0, 60, 0.9f);
            expectEquals (longLegacy.totalSamples, static_cast<juce::int64> (60 * 48000));

            beginTest ("single note holds, releases, then runs its tail");
            const auto single = ExportPlanner::plan (request (ExportPlanner::Mode::singleNote), 1.2);
            expect (single.valid, single.message);
            expectWithinAbsoluteError (single.durationSeconds, 7.2, 1.0e-9);
            expectEquals (single.totalSamples, static_cast<juce::int64> (345600));
            expectEquals (countEvents (single, true), 1);
            expectEquals (countEvents (single, false), 1);
            if (single.events.size() == 2)
            {
                expectEquals (single.events[1].sample, static_cast<juce::int64> (96000));
                expectEquals (single.events[0].note, 60);
            }

            beginTest ("velocity 1 to 127 maps to 0 to 1 on the same scale as the patch");
            auto loud = request (ExportPlanner::Mode::singleNote);
            loud.velocity = 127;
            const auto full = ExportPlanner::plan (loud, 1.2);
            if (! full.events.empty())
                expectWithinAbsoluteError (full.events[0].velocity, 1.0f, 1.0e-6f);

            beginTest ("chord mode sounds every note of the selected shape at the same instants");
            const auto major = ExportPlanner::plan (request (ExportPlanner::Mode::chord), 1.2);
            expect (major.valid, major.message);
            expectEquals (countEvents (major, true), 3);
            expectEquals (countEvents (major, false), 3);
            int root = 0, third = 0, fifth = 0, other = 0;
            for (const auto& event : major.events)
            {
                if (! event.on || event.sample != 0)
                    continue;

                if (event.note == 60)      ++root;
                else if (event.note == 64) ++third;
                else if (event.note == 67) ++fifth;
                else                       ++other;
            }
            expectEquals (root, 1);
            expectEquals (third, 1);
            expectEquals (fifth, 1);
            expectEquals (other, 0);

            auto single2 = request (ExportPlanner::Mode::chord);
            single2.chordShape = 0;
            expectEquals (countEvents (ExportPlanner::plan (single2, 1.2), true), 1);

            beginTest ("repeated phrase places each hit on its exact sample");
            // hold 0.5 s, gap 0.25 s: hits start every 0.75 s; release 0.1 s, tail 0.2 s.
            auto phrase = request (ExportPlanner::Mode::repeatedPhrase);
            phrase.holdSeconds = 0.5;
            phrase.gapSeconds = 0.25;
            phrase.tailSeconds = 0.2;
            phrase.repeats = 3;
            phrase.chordShape = 0;
            const auto repeated = ExportPlanner::plan (phrase, 0.1);
            expect (repeated.valid, repeated.message);
            expectWithinAbsoluteError (repeated.durationSeconds, 2.3, 1.0e-9);
            expectEquals (countEvents (repeated, true), 3);
            expectEquals (countEvents (repeated, false), 3);

            const std::array<juce::int64, 3> expectedOn { 0, 36000, 72000 };
            const std::array<juce::int64, 3> expectedOff { 24000, 60000, 96000 };
            int matchedOn = 0;
            int matchedOff = 0;
            for (const auto& event : repeated.events)
            {
                for (const auto sample : expectedOn)
                    if (event.on && event.sample == sample)
                        ++matchedOn;

                for (const auto sample : expectedOff)
                    if (! event.on && event.sample == sample)
                        ++matchedOff;
            }
            expectEquals (matchedOn, 3);
            expectEquals (matchedOff, 3);

            beginTest ("a retrigger at the same sample releases the old note before striking the new one");
            auto touching = phrase;
            touching.gapSeconds = 0.0;
            touching.repeats = 2;
            const auto retrigger = ExportPlanner::plan (touching, 0.1);
            expect (retrigger.valid);
            bool sawOffFirst = false;
            for (size_t i = 1; i < retrigger.events.size(); ++i)
                if (retrigger.events[i - 1].sample == retrigger.events[i].sample)
                    sawOffFirst = ! retrigger.events[i - 1].on && retrigger.events[i].on;
            expect (sawOffFirst, "note-off must precede note-on at equal samples");

            beginTest ("sample boundaries scale exactly with the export rate");
            auto at96 = request (ExportPlanner::Mode::singleNote);
            at96.sampleRate = 96000.0;
            const auto high = ExportPlanner::plan (at96, 1.2);
            expectEquals (high.totalSamples, static_cast<juce::int64> (691200));

            auto at44 = request (ExportPlanner::Mode::singleNote);
            at44.sampleRate = 44100.0;
            const auto low = ExportPlanner::plan (at44, 1.2);
            expectEquals (low.totalSamples, static_cast<juce::int64> (317520));

            beginTest ("a render that would exceed 120 seconds is rejected, not truncated");
            auto tooLong = request (ExportPlanner::Mode::repeatedPhrase);
            tooLong.holdSeconds = 30.0;
            tooLong.gapSeconds = 5.0;
            tooLong.repeats = 3;
            tooLong.tailSeconds = 30.0;
            const auto rejected = ExportPlanner::plan (tooLong, 8.0);
            expect (! rejected.valid);
            expect (rejected.message.contains ("limit"), rejected.message);
            expect (rejected.events.empty());

            auto exactlyLimit = tooLong;
            exactlyLimit.repeats = 2;
            const auto accepted = ExportPlanner::plan (exactlyLimit, 8.0);
            expect (accepted.valid, accepted.message);
            expect (accepted.durationSeconds <= ExportPlanner::maxDurationSeconds);

            beginTest ("every control rejects out-of-range values with a reason");
            const auto check = [] (const ExportPlanner::Request& value)
            {
                return ! ExportPlanner::plan (value, 0.5).valid;
            };

            auto bad = request (ExportPlanner::Mode::singleNote);
            bad.note = 128;
            expect (check (bad));
            bad = request (ExportPlanner::Mode::singleNote);
            bad.note = -1;
            expect (check (bad));
            bad = request (ExportPlanner::Mode::singleNote);
            bad.velocity = 0;
            expect (check (bad));
            bad.velocity = 128;
            expect (check (bad));
            bad = request (ExportPlanner::Mode::singleNote);
            bad.holdSeconds = 0.05;
            expect (check (bad));
            bad.holdSeconds = 30.5;
            expect (check (bad));
            bad = request (ExportPlanner::Mode::singleNote);
            bad.tailSeconds = -0.1;
            expect (check (bad));
            bad = request (ExportPlanner::Mode::repeatedPhrase);
            bad.repeats = 0;
            expect (check (bad));
            bad.repeats = 17;
            expect (check (bad));
            bad = request (ExportPlanner::Mode::repeatedPhrase);
            bad.gapSeconds = 5.5;
            expect (check (bad));
            bad = request (ExportPlanner::Mode::singleNote);
            bad.sampleRate = 32000.0;
            expect (check (bad));
            bad = request (ExportPlanner::Mode::chord);
            bad.chordShape = 4;
            expect (check (bad));
            bad = request (ExportPlanner::Mode::chord);
            bad.note = 126;   // major third and fifth would pass MIDI 127
            expect (check (bad), "a chord above MIDI 127 is refused, not wrapped");

            expect (ExportPlanner::plan (request (ExportPlanner::Mode::legacyTwoHit), 1.2).valid,
                    "legacy mode uses its own, unrestricted schedule");
        }
    };

    static ExportPlannerTests exportPlannerTests;
}
