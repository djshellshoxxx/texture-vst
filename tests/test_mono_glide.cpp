#include "TestHelpers.h"
#include "../Source/Engine/GlideEnvelope.h"
#include "../Source/Engine/MonoNoteStack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace texture::tests
{
    namespace
    {
        // Exact float equality without a == comparison, which -Wfloat-equal rejects.
        bool sameValue (float a, float b) noexcept
        {
            return ! (a < b) && ! (a > b);
        }

        int expectedGlideSamples (double glideMs, double sampleRate) noexcept
        {
            return static_cast<int> (std::round (glideMs * sampleRate / 1000.0));
        }
    }

    class MonoNoteStackTests final : public juce::UnitTest
    {
    public:
        MonoNoteStackTests() : juce::UnitTest ("Mono note stack", "texture") {}

        void runTest() override
        {
            using Stack = MonoNoteStack;
            constexpr int channel = 1;
            constexpr int c4 = 60;
            constexpr int e4 = 64;
            constexpr int g4 = 67;

            beginTest ("overlapping C/E/G releases resolve correctly in every order");
            {
                // Press order E, G, C so that newest, highest, and lowest all differ.
                const std::array<int, 3> pressOrder { e4, g4, c4 };
                std::array<int, 3> releaseOrder { 0, 1, 2 };
                int permutations = 0;
                do
                {
                    ++permutations;
                    Stack stack;
                    for (const auto note : pressOrder)
                        expect (stack.noteOn (channel, note, 100));

                    std::array<bool, 3> remaining { true, true, true };
                    for (const auto releaseIndex : releaseOrder)
                    {
                        const auto released = pressOrder[static_cast<std::size_t> (releaseIndex)];
                        stack.noteOff (channel, released);
                        remaining[static_cast<std::size_t> (releaseIndex)] = false;

                        std::optional<int> newest, highest, lowest;
                        for (std::size_t i = 0; i < pressOrder.size(); ++i)
                        {
                            if (! remaining[i])
                                continue;

                            newest = pressOrder[i];
                            if (! highest || pressOrder[i] > *highest) highest = pressOrder[i];
                            if (! lowest || pressOrder[i] < *lowest) lowest = pressOrder[i];
                        }

                        const auto last = stack.select (Stack::Priority::last);
                        const auto high = stack.select (Stack::Priority::high);
                        const auto low = stack.select (Stack::Priority::low);
                        if (! newest)
                        {
                            expect (! last && ! high && ! low);
                        }
                        else
                        {
                            expect (last.has_value() && high.has_value() && low.has_value());
                            expectEquals (last->note, *newest);
                            expectEquals (high->note, *highest);
                            expectEquals (low->note, *lowest);
                        }
                    }
                    expect (stack.size() == 0);
                } while (std::next_permutation (releaseOrder.begin(), releaseOrder.end()));

                expectEquals (permutations, 6);
            }

            beginTest ("repeated pitches saturate and never underflow");
            {
                Stack stack;
                expect (stack.noteOn (channel, c4, 100));
                expect (stack.noteOn (channel, c4, 90));
                expect (stack.noteOn (channel, c4, 80));
                expectEquals (stack.pressCount (channel, c4), 3);
                expectEquals (stack.size(), 1);

                stack.noteOff (channel, c4);
                expect (stack.contains (channel, c4));
                expectEquals (stack.pressCount (channel, c4), 2);
                stack.noteOff (channel, c4);
                stack.noteOff (channel, c4);
                expect (! stack.contains (channel, c4));

                for (int i = 0; i < 1000; ++i)
                    stack.noteOff (channel, c4);
                expectEquals (stack.size(), 0);
                expect (! stack.select (Stack::Priority::last).has_value());

                expect (stack.noteOn (channel, c4, 100));
                expectEquals (stack.pressCount (channel, c4), 1);
                stack.noteOff (channel, c4);
                expect (! stack.contains (channel, c4));

                for (int i = 0; i < 300; ++i)
                    expect (stack.noteOn (channel, e4, 100));
                expectEquals (stack.pressCount (channel, e4), 255);
                for (int i = 0; i < 255; ++i)
                    stack.noteOff (channel, e4);
                expect (! stack.contains (channel, e4));
            }

            beginTest ("same pitch on different channels occupies separate slots");
            {
                Stack stack;
                expect (stack.noteOn (1, c4, 100));
                expect (stack.noteOn (2, c4, 100));
                expectEquals (stack.size(), 2);
                stack.noteOff (1, c4);
                expect (! stack.contains (1, c4));
                expect (stack.contains (2, c4));
            }

            beginTest ("sustain retains released keys until pedal-up");
            {
                Stack stack;
                stack.setSustain (true);
                expect (stack.noteOn (channel, c4, 100));
                expect (stack.noteOn (channel, e4, 100));
                stack.noteOff (channel, e4);

                expect (stack.contains (channel, e4));
                expect (! stack.isPhysicallyHeld (channel, e4));
                expectEquals (stack.select (Stack::Priority::last)->note, e4);
                expectEquals (stack.select (Stack::Priority::low)->note, c4);

                stack.noteOff (channel, c4);
                expectEquals (stack.select (Stack::Priority::high)->note, e4);

                stack.setSustain (false);
                expect (! stack.contains (channel, e4));
                expect (! stack.contains (channel, c4));
                expectEquals (stack.size(), 0);
            }

            beginTest ("re-pressing a retained key makes it held again");
            {
                Stack stack;
                stack.setSustain (true);
                expect (stack.noteOn (channel, c4, 100));
                stack.noteOff (channel, c4);
                expect (stack.noteOn (channel, c4, 70));
                expect (stack.isPhysicallyHeld (channel, c4));
                expectEquals (stack.pressCount (channel, c4), 1);
                expectEquals (stack.select (Stack::Priority::last)->velocity, 70);

                // Still sustained, so this release retains the key again.
                stack.noteOff (channel, c4);
                expect (stack.contains (channel, c4));
                expect (! stack.isPhysicallyHeld (channel, c4));
            }

            beginTest ("zero-velocity noteOn behaves as noteOff");
            {
                Stack stack;
                expect (stack.noteOn (channel, c4, 100));
                expect (stack.noteOn (channel, c4, 0));
                expect (! stack.contains (channel, c4));

                expect (stack.noteOn (channel, c4, 0));
                expectEquals (stack.size(), 0);

                stack.setSustain (true);
                expect (stack.noteOn (channel, c4, 100));
                expect (stack.noteOn (channel, c4, 0));
                expect (stack.contains (channel, c4));
                expect (! stack.isPhysicallyHeld (channel, c4));
            }

            beginTest ("priority modes choose the expected note");
            {
                Stack stack;
                expect (stack.noteOn (1, 48, 100));
                expect (stack.noteOn (2, 72, 90));
                expect (stack.noteOn (3, 60, 110));

                const auto last = stack.select (Stack::Priority::last);
                const auto high = stack.select (Stack::Priority::high);
                const auto low = stack.select (Stack::Priority::low);
                expect (last.has_value() && high.has_value() && low.has_value());
                expectEquals (last->note, 60);
                expectEquals (last->channel, 3);
                expectEquals (last->velocity, 110);
                expectEquals (high->note, 72);
                expectEquals (low->note, 48);

                // Equal pitches on two channels: high and low break ties toward the newest press.
                Stack tie;
                expect (tie.noteOn (1, c4, 50));
                expect (tie.noteOn (2, c4, 90));
                expectEquals (tie.select (Stack::Priority::high)->channel, 2);
                expectEquals (tie.select (Stack::Priority::low)->channel, 2);
                expectEquals (tie.select (Stack::Priority::high)->velocity, 90);
            }

            beginTest ("clear drops every key and resets sustain");
            {
                Stack stack;
                stack.setSustain (true);
                expect (stack.noteOn (channel, c4, 100));
                stack.noteOff (channel, c4);
                expect (stack.noteOn (channel, e4, 100));

                stack.clear();
                expectEquals (stack.size(), 0);
                expect (! stack.isSustainDown());
                expect (! stack.select (Stack::Priority::last).has_value());
            }

            beginTest ("capacity is bounded and evicts only retained keys");
            {
                Stack stack;
                for (int note = 0; note < Stack::capacity; ++note)
                    expect (stack.noteOn (channel, note, 100));
                expectEquals (stack.size(), Stack::capacity);
                expect (! stack.noteOn (channel, 100, 100));
                expectEquals (stack.size(), Stack::capacity);

                stack.setSustain (true);
                stack.noteOff (channel, 0);
                stack.noteOff (channel, 1);
                expect (stack.noteOn (channel, 100, 100));
                expectEquals (stack.size(), Stack::capacity);
                expect (! stack.contains (channel, 0));
                expect (stack.contains (channel, 1));
                expect (stack.contains (channel, 100));
            }

            beginTest ("out-of-range events are ignored");
            {
                Stack stack;
                expect (! stack.noteOn (0, c4, 100));
                expect (! stack.noteOn (17, c4, 100));
                expect (! stack.noteOn (channel, 128, 100));
                expect (! stack.noteOn (channel, -1, 100));
                expect (! stack.noteOn (channel, c4, 128));
                expect (! stack.noteOn (channel, c4, -1));
                expectEquals (stack.size(), 0);
            }
        }
    };

    class GlideEnvelopeTests final : public juce::UnitTest
    {
    public:
        GlideEnvelopeTests() : juce::UnitTest ("Glide envelope", "texture") {}

        void runTest() override
        {
            beginTest ("endpoints are exact and duration is rounded samples");
            {
                GlideEnvelope glide;
                glide.prepare (48000.0);
                glide.setImmediate (0.0f);
                glide.setTarget (12.0f, 10.0f);
                expect (glide.isGliding());

                const auto samples = expectedGlideSamples (10.0, 48000.0);
                expectEquals (samples, 480);
                float value = 0.0f;
                for (int i = 1; i < samples; ++i)
                {
                    value = glide.next();
                    expect (glide.isGliding());
                    expect (value > 0.0f && value < 12.0f);
                }

                value = glide.next();
                expect (sameValue (value, 12.0f));
                expect (sameValue (glide.value(), 12.0f));
                expect (! glide.isGliding());
                expect (sameValue (glide.next(), 12.0f));
            }

            beginTest ("duration is independent of interval within one sample");
            {
                for (const double rate : { 44100.0, 48000.0, 96000.0 })
                {
                    for (const float glideMs : { 1.0f, 37.3f, 250.0f })
                    {
                        const auto expected = expectedGlideSamples (glideMs, rate);
                        int firstCount = -1;
                        for (const float interval : { 1.0f, 12.0f, 24.0f, -36.0f })
                        {
                            GlideEnvelope glide;
                            glide.prepare (rate);
                            glide.setImmediate (0.0f);
                            glide.setTarget (interval, glideMs);

                            int count = 0;
                            while (glide.isGliding())
                            {
                                glide.next();
                                ++count;
                            }

                            expect (std::abs (count - expected) <= 1);
                            if (firstCount < 0)
                                firstCount = count;
                            expect (std::abs (count - firstCount) <= 1);
                            expect (sameValue (glide.value(), interval));
                        }
                    }
                }
            }

            beginTest ("linear in semitones with frequency-ratio endpoints");
            {
                GlideEnvelope glide;
                glide.prepare (44100.0);
                glide.setImmediate (0.0f);
                glide.setTarget (12.0f, 100.0f);

                for (int i = 0; i < 2205; ++i)
                    glide.next();
                expect (std::abs (glide.value() - 6.0f) < 1.0e-3f);

                for (int i = 2205; i < 4410; ++i)
                    glide.next();
                const auto ratio = std::pow (2.0, static_cast<double> (glide.value()) / 12.0);
                expect (std::abs (ratio - 2.0) < 1.0e-9);
            }

            beginTest ("zero, negative, tiny and unprepared glides jump immediately");
            {
                GlideEnvelope glide;
                glide.prepare (44100.0);
                glide.setImmediate (0.0f);

                glide.setTarget (5.0f, 0.0f);
                expect (! glide.isGliding());
                expect (sameValue (glide.next(), 5.0f));

                glide.setTarget (-7.0f, -3.0f);
                expect (! glide.isGliding());
                expect (sameValue (glide.next(), -7.0f));

                // 0.01 ms at 44.1 kHz is 0.441 samples, which rounds to zero.
                glide.setTarget (2.0f, 0.01f);
                expect (! glide.isGliding());
                expect (sameValue (glide.next(), 2.0f));

                GlideEnvelope unprepared;
                unprepared.setTarget (7.0f, 100.0f);
                expect (! unprepared.isGliding());
                expect (sameValue (unprepared.next(), 7.0f));
            }

            beginTest ("retarget mid-glide starts from the instantaneous pitch");
            {
                GlideEnvelope glide;
                glide.prepare (1000.0);
                glide.setImmediate (0.0f);
                glide.setTarget (12.0f, 100.0f);

                for (int i = 0; i < 30; ++i)
                    glide.next();
                const auto before = glide.value();
                expect (std::abs (before - 3.6f) < 1.0e-4f);

                glide.setTarget (-12.0f, 50.0f);
                const auto first = glide.next();
                expect (std::abs (first - (before + (-12.0f - before) / 50.0f)) < 1.0e-4f);

                for (int i = 1; i < 50; ++i)
                    glide.next();
                expect (! glide.isGliding());
                expect (sameValue (glide.value(), -12.0f));

                // A zero-time retarget mid-glide jumps to the new target.
                glide.setTarget (12.0f, 100.0f);
                for (int i = 0; i < 10; ++i)
                    glide.next();
                glide.setTarget (4.0f, 0.0f);
                expect (! glide.isGliding());
                expect (sameValue (glide.value(), 4.0f));
            }
        }
    };

    static MonoNoteStackTests monoNoteStackTests;
    static GlideEnvelopeTests glideEnvelopeTests;
}
