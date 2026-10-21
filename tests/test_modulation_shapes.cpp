#include "TestHelpers.h"

#include "../Source/Modulation/CurveEdit.h"
#include "../Source/Modulation/CurveMailbox.h"
#include "../Source/Modulation/HeldNoteCounter.h"
#include "../Source/Modulation/ModulationCurve.h"
#include "../Source/Modulation/ModulationOscillator.h"
#include "../Source/Modulation/MorphLFO.h"
#include "../Source/Presets/LockPolicy.h"
#include "../Source/Presets/PresetManager.h"
#include "../Source/Utility/DspHelpers.h"
#include "../Source/Utility/StateSchema.h"

#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <vector>

// F14 modulation shapes: the curve rules, the waveforms and their continuity, the golden
// random sequences, the retrigger and transport rules of the LFO, and the state, preset and
// lock contracts.

namespace texture::tests
{
    namespace
    {
        constexpr double lfoRate = 48000.0;

        std::vector<CurvePoint> points (std::initializer_list<CurvePoint> list)
        {
            return std::vector<CurvePoint> (list);
        }

        WaveformInput input (LfoShape shape, double phase, std::int64_t cycle = 0,
                             std::uint64_t seed = 0x5445585455524531ULL)
        {
            WaveformInput value;
            value.shape = shape;
            value.phase = phase;
            value.cycle = cycle;
            value.seed = seed;
            return value;
        }

        /** Legacy reference: exactly the arithmetic MorphLFO used before F14. */
        struct LegacyReference
        {
            double phase { 0.0 };
            float rateHz { 0.2f };

            float advance (int numSamples)
            {
                const auto value = std::sin (static_cast<float> (phase) * dsp::twoPi);
                const auto increment = static_cast<double> (rateHz) / lfoRate;
                phase += increment * static_cast<double> (juce::jmax (0, numSamples));
                phase -= std::floor (phase);
                return value;
            }
        };
    }

    class ModulationShapeTests : public juce::UnitTest
    {
    public:
        ModulationShapeTests()
            : juce::UnitTest ("Modulation shapes", "texture") {}

        void runTest() override
        {
            beginTest ("a curve needs two to 32 points, sorted, from 0 to 1, inside its ranges");
            {
                ModulationCurve curve;
                expect (curve.isValid(), "the default curve is valid");
                expectEquals (curve.getNumPoints(), 5);

                expect (! ModulationCurve::fromPoints (points ({ { 0.0f, 0.0f } }), curve), "one point");
                expect (ModulationCurve::fromPoints (points ({ { 0.0f, 0.0f }, { 1.0f, 1.0f } }), curve));

                std::vector<CurvePoint> many;
                for (int i = 0; i <= 32; ++i)
                    many.push_back ({ static_cast<float> (i) / 32.0f, 0.0f });
                expect (! ModulationCurve::fromPoints (many, curve), "33 points");

                // Thirty-two points run from 0 to 1 inclusive.
                std::vector<CurvePoint> thirtyTwo;
                for (int i = 0; i < 32; ++i)
                    thirtyTwo.push_back ({ static_cast<float> (i) / 31.0f, 0.0f });
                expect (ModulationCurve::fromPoints (thirtyTwo, curve), "32 points");

                expect (! ModulationCurve::fromPoints (points ({ { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.4f, 0.0f }, { 1.0f, 0.0f } }), curve), "unsorted");
                expect (! ModulationCurve::fromPoints (points ({ { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } }), curve), "duplicate x");
                expect (! ModulationCurve::fromPoints (points ({ { 0.1f, 0.0f }, { 1.0f, 0.0f } }), curve), "first x not zero");
                expect (! ModulationCurve::fromPoints (points ({ { 0.0f, 0.0f }, { 0.9f, 0.0f } }), curve), "last x not one");
                expect (! ModulationCurve::fromPoints (points ({ { 0.0f, 1.5f }, { 1.0f, 0.0f } }), curve), "y above one");
                expect (! ModulationCurve::fromPoints (points ({ { 0.0f, 0.0f }, { 1.5f, 0.0f } }), curve), "x above one");
                expect (! ModulationCurve::fromPoints (points ({ { 0.0f, std::nanf ("") }, { 1.0f, 0.0f } }), curve), "NaN");
                expect (! ModulationCurve::fromPoints (points ({ { 0.0f, 0.0f }, { 1.0f, std::numeric_limits<float>::infinity() } }), curve), "infinity");
            }

            beginTest ("a curve is piecewise linear, holds its ends, and never extrapolates");
            {
                ModulationCurve curve;
                expect (ModulationCurve::fromPoints (points ({ { 0.0f, -1.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } }), curve));
                expectWithinAbsoluteError (curve.evaluate (0.0f), -1.0f, 1.0e-6f);
                expectWithinAbsoluteError (curve.evaluate (0.25f), 0.0f, 1.0e-6f);
                expectWithinAbsoluteError (curve.evaluate (0.5f), 1.0f, 1.0e-6f);
                expectWithinAbsoluteError (curve.evaluate (0.75f), 0.5f, 1.0e-6f);
                expectWithinAbsoluteError (curve.evaluate (1.0f), 0.0f, 1.0e-6f);
                expectWithinAbsoluteError (curve.evaluate (-3.0f), -1.0f, 1.0e-6f);
                expectWithinAbsoluteError (curve.evaluate (7.0f), 0.0f, 1.0e-6f);
                expectWithinAbsoluteError (curve.evaluate (std::nanf ("")), -1.0f, 1.0e-6f);
            }

            beginTest ("curve edits keep endpoints, spacing and range, and reject what breaks the rules");
            {
                ModulationCurve base;
                expect (ModulationCurve::fromPoints (points ({ { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 1.0f, 0.0f } }), base));

                ModulationCurve out;
                expect (curveEdit::movePoint (base, 1, 0.999f, 2.0f, out), "a move past the neighbour is accepted and clamped");
                expectWithinAbsoluteError (out.getPoint (1).x, 1.0f - curveEdit::minimumSpacing, 1.0e-6f,
                                           "an interior point stops short of its right neighbour");
                expectWithinAbsoluteError (out.getPoint (1).y, 1.0f, 1.0e-6f, "y is limited to one");

                expect (curveEdit::movePoint (base, 0, 0.3f, -0.4f, out));
                expect (juce::exactlyEqual (out.getPoint (0).x, 0.0f) && juce::exactlyEqual (out.getPoint (0).y, -0.4f),
                        "the first point keeps x = 0");
                expect (curveEdit::movePoint (base, 2, 0.1f, 0.1f, out));
                expect (juce::exactlyEqual (out.getPoint (2).x, 1.0f), "the last point keeps x = 1");

                expect (! curveEdit::movePoint (base, 3, 0.5f, 0.0f, out), "no such point");
                expect (! curveEdit::movePoint (base, 1, std::nanf (""), 0.0f, out), "non-finite input");

                expect (curveEdit::insertPoint (base, 0.25f, 0.5f, out), "a point between two others");
                expectEquals (out.getNumPoints(), 4);
                expect (out.isValid());
                expect (! curveEdit::insertPoint (base, 0.5f + 0.001f, 0.0f, out), "too close to a neighbour");
                expect (! curveEdit::insertPoint (base, 0.0f, 0.0f, out), "an endpoint is not an insertion");

                expect (curveEdit::removePoint (base, 1, out), "an interior point can go");
                expectEquals (out.getNumPoints(), 2);
                expect (! curveEdit::removePoint (out, 0, out), "endpoints stay");
                expect (! curveEdit::removePoint (out, 1, out), "the curve keeps two points");

                std::vector<CurvePoint> full;
                for (int i = 0; i < 32; ++i)
                    full.push_back ({ static_cast<float> (i) / 31.0f, 0.0f });
                ModulationCurve capped;
                expect (ModulationCurve::fromPoints (full, capped));
                expect (! curveEdit::insertPoint (capped, 0.5f, 0.0f, out), "32 points is the maximum");
            }

            beginTest ("the mailbox hands over whole curves and keeps the previous one when it cannot read");
            {
                CurveMailbox mailbox;
                ModulationCurve reader;
                expect (! mailbox.tryRead (reader) || reader.isValid(), "an unwritten mailbox never yields a bad curve");

                ModulationCurve written;
                expect (ModulationCurve::fromPoints (points ({ { 0.0f, 0.5f }, { 0.3f, -0.5f }, { 1.0f, 0.25f } }), written));
                mailbox.publish (written);
                expect (mailbox.tryRead (reader));
                expect (reader == written, "the reader must see exactly what was published");

                ModulationCurve longer;
                std::vector<CurvePoint> thirty;
                for (int i = 0; i < 32; ++i)
                    thirty.push_back ({ static_cast<float> (i) / 31.0f, std::sin (static_cast<float> (i)) });
                expect (ModulationCurve::fromPoints (thirty, longer));
                mailbox.publish (longer);
                expect (mailbox.tryRead (reader) && reader == longer, "a longer curve replaces a shorter one");
            }

            beginTest ("sine, triangle and curve shapes keep their documented values");
            {
                // Sine must match the legacy arithmetic exactly: the default shape.
                for (int step = 0; step < 100; ++step)
                {
                    const auto phase = static_cast<double> (step) / 100.0;
                    const auto expected = std::sin (static_cast<float> (phase) * dsp::twoPi);
                    expect (juce::exactlyEqual (evaluateWaveform (input (LfoShape::sine, phase)), expected),
                            "sine must reproduce the legacy LFO exactly");
                }

                expectWithinAbsoluteError (evaluateWaveform (input (LfoShape::triangle, 0.0)), 0.0f, 1.0e-6f);
                expectWithinAbsoluteError (evaluateWaveform (input (LfoShape::triangle, 0.25)), 1.0f, 1.0e-6f);
                expectWithinAbsoluteError (evaluateWaveform (input (LfoShape::triangle, 0.5)), 0.0f, 1.0e-6f);
                expectWithinAbsoluteError (evaluateWaveform (input (LfoShape::triangle, 0.75)), -1.0f, 1.0e-6f);

                float largestStep = 0.0f;
                float previous = evaluateWaveform (input (LfoShape::triangle, 0.0));
                for (int step = 1; step <= 10000; ++step)
                {
                    const auto value = evaluateWaveform (input (LfoShape::triangle, static_cast<double> (step) / 10000.0));
                    largestStep = juce::jmax (largestStep, std::abs (value - previous));
                    previous = value;
                }
                expect (largestStep < 0.001f, "triangle must be continuous: largest step " + juce::String (largestStep));

                ModulationCurve curve;
                expect (ModulationCurve::fromPoints (points ({ { 0.0f, 0.2f }, { 0.4f, -0.8f }, { 1.0f, 0.2f } }), curve));
                WaveformInput shaped = input (LfoShape::curve, 0.4);
                shaped.curve = &curve;
                expectWithinAbsoluteError (evaluateWaveform (shaped), -0.8f, 1.0e-6f);
                shaped.curve = nullptr;
                expect (juce::exactlyEqual (evaluateWaveform (shaped), 0.0f), "no curve is silence, not a crash");
            }

            beginTest ("random values are bounded, reproducible per seed and cycle, and differ between seeds");
            {
                for (std::int64_t cycle = -20; cycle <= 20; ++cycle)
                {
                    const auto value = randomCycleValue (0x1234ULL, cycle);
                    expect (value >= -1.0f && value <= 1.0f);
                    expect (juce::exactlyEqual (value, randomCycleValue (0x1234ULL, cycle)));
                }

                expect (! juce::exactlyEqual (randomCycleValue (0x1234ULL, 3), randomCycleValue (0x1235ULL, 3)),
                        "a different seed must give a different value");
            }

            beginTest ("golden random sequences are pinned");
            {
                // Pinned from this build. Changing the random lane, the draw order or the
                // shape arithmetic changes these; a new algorithm version must record why.
                const std::array<float, 6> expectedSteps { {
                    randomCycleValue (0x5445585455524531ULL, 0), randomCycleValue (0x5445585455524531ULL, 1),
                    randomCycleValue (0x5445585455524531ULL, 2), randomCycleValue (0x5445585455524531ULL, 3),
                    randomCycleValue (0x5445585455524531ULL, 4), randomCycleValue (0x5445585455524531ULL, 5) } };

                const std::array<float, 6> golden { {
                    -0.296500444f, 0.440162063f, 0.260787129f, 0.030821562f, 0.691561460f, 0.725092292f } };

                for (size_t i = 0; i < golden.size(); ++i)
                    expectWithinAbsoluteError (expectedSteps[i], golden[i], 1.0e-7f,
                                               "golden random value " + juce::String (static_cast<int> (i))
                                               + " is now " + juce::String (expectedSteps[i], 9));
            }

            beginTest ("step random holds a value per cycle, and slews between cycles over the slew time");
            {
                const auto flat = evaluateWaveform (input (LfoShape::stepRandom, 0.1, 4));
                expect (juce::exactlyEqual (flat, evaluateWaveform (input (LfoShape::stepRandom, 0.9, 4))),
                        "with no slew a cycle holds one value");

                WaveformInput slewed = input (LfoShape::stepRandom, 0.0, 4);
                slewed.slewSamples = 480.0;
                slewed.elapsedSamples = 0.0;
                const auto start = evaluateWaveform (slewed);
                expectWithinAbsoluteError (start, randomCycleValue (slewed.seed, 3), 1.0e-6f,
                                           "the slew starts from the previous cycle's value");

                slewed.elapsedSamples = 480.0;
                expectWithinAbsoluteError (evaluateWaveform (slewed), randomCycleValue (slewed.seed, 4), 1.0e-6f,
                                           "after the slew the value is this cycle's");
            }

            beginTest ("smooth random is continuous at the cycle boundary and stays bounded");
            {
                const auto before = evaluateWaveform (input (LfoShape::smoothRandom, 0.999999, 7));
                const auto after = evaluateWaveform (input (LfoShape::smoothRandom, 0.0, 8));
                expectWithinAbsoluteError (before, after, 1.0e-4f, "smooth random must not jump at the boundary");

                for (int step = 0; step <= 100; ++step)
                {
                    const auto value = evaluateWaveform (input (LfoShape::smoothRandom, static_cast<double> (step) / 100.0, 2));
                    expect (value >= -1.0f && value <= 1.0f);
                }
            }

            beginTest ("the curve wraps with a crossfade over the slew time, not a click");
            {
                ModulationCurve curve;
                expect (ModulationCurve::fromPoints (points ({ { 0.0f, -1.0f }, { 1.0f, 1.0f } }), curve));

                WaveformInput wrap = input (LfoShape::curve, 0.0, 3);
                wrap.curve = &curve;
                wrap.slewSamples = 960.0;
                wrap.elapsedSamples = 0.0;
                expectWithinAbsoluteError (evaluateWaveform (wrap), 1.0f, 1.0e-6f,
                                           "at the boundary the output continues from the previous cycle's end");

                wrap.elapsedSamples = 960.0;
                expectWithinAbsoluteError (evaluateWaveform (wrap), -1.0f, 1.0e-6f,
                                           "after the slew the output follows the curve");
            }

            beginTest ("every shape stays finite for odd phases, cycles and seeds");
            {
                ModulationCurve curve;
                const std::array<double, 6> phases { -1.0, 0.0, 0.5, 1.0, 1.0e9, std::numeric_limits<double>::quiet_NaN() };

                for (const auto shape : { LfoShape::sine, LfoShape::triangle, LfoShape::stepRandom,
                                          LfoShape::smoothRandom, LfoShape::curve })
                    for (const auto phase : phases)
                        for (const std::int64_t cycle : { std::int64_t { -1000000 }, std::int64_t { 0 }, std::int64_t { 1000000 } })
                        {
                            WaveformInput value = input (shape, phase, cycle, UINT64_C (0xFFFFFFFFFFFFFFFF));
                            value.curve = &curve;
                            value.slewSamples = 4800.0;
                            value.elapsedSamples = 17.0;
                            const auto out = evaluateWaveform (value);
                            expect (std::isfinite (out) && out >= -1.0f && out <= 1.0f,
                                    "non-finite or out-of-range output");
                        }

                expectEquals (static_cast<int> (lfoShapeFromIndex (9)), static_cast<int> (LfoShape::sine));
                expectEquals (static_cast<int> (lfoShapeFromIndex (-1)), static_cast<int> (LfoShape::sine));
                expectEquals (static_cast<int> (lfoRetriggerFromIndex (7)), static_cast<int> (LfoRetrigger::legacy));
            }

            beginTest ("Sine, Legacy and zero phase reproduce the original LFO sample for sample");
            {
                MorphLFO lfo;
                lfo.prepare (lfoRate);
                lfo.setRateHz (1.0f);
                LegacyReference reference;
                reference.rateHz = lfo.getEffectiveRateHz();

                for (const int block : { 64, 17, 128, 512, 1, 333 })
                {
                    expectWithinAbsoluteError (lfo.advanceBlock (block), reference.advance (block), 0.0f,
                                               "legacy LFO diverged");
                }
            }

            beginTest ("a shape's period follows the rate and the tempo division");
            {
                MorphLFO lfo;
                lfo.prepare (lfoRate);
                lfo.setShape (LfoShape::triangle);
                lfo.setRateHz (2.0f);

                // 2 Hz: a cycle is 24000 samples. Compare the value one cycle apart.
                lfo.advanceBlock (0);
                const auto start = lfo.getCurrentValue();
                const std::int64_t cycleSamples = 24000;
                lfo.advanceBlock (static_cast<int> (cycleSamples));
                lfo.advanceBlock (0);
                expectWithinAbsoluteError (lfo.getCurrentValue(), start, 1.0e-3f, "a 2 Hz triangle repeats each 24000 samples");

                MorphLFO synced;
                synced.prepare (lfoRate);
                synced.setShape (LfoShape::triangle);
                synced.setSyncEnabled (true);
                synced.setDivisionIndex (2);                        // 1/4: one beat per cycle
                synced.updateFromPlayHead (120.0, true, 0.0);
                synced.advanceBlock (0);
                const auto first = synced.getCurrentValue();
                synced.updateFromPlayHead (120.0, true, 1.0);      // one beat later: one cycle later
                synced.advanceBlock (0);
                expectWithinAbsoluteError (synced.getCurrentValue(), first, 1.0e-5f);
                expectEquals (static_cast<int> (synced.getCycle()), 1);
            }

            beginTest ("host position is authoritative in Host mode, including negative pre-roll");
            {
                MorphLFO lfo;
                lfo.prepare (lfoRate);
                lfo.setRetrigger (LfoRetrigger::host);
                lfo.setDivisionIndex (2);                           // 1/4
                lfo.setSyncEnabled (false);                         // Host ignores the Sync switch

                lfo.updateFromPlayHead (120.0, true, -0.5);          // half a beat before the start
                expectWithinAbsoluteError (lfo.getPhase(), 0.5, 1.0e-9, "pre-roll must not clamp to zero");
                expectEquals (static_cast<int> (lfo.getCycle()), -1);

                lfo.updateFromPlayHead (120.0, false, 2.25);         // stopped: frozen at the position
                lfo.advanceBlock (4800);
                expectWithinAbsoluteError (lfo.getPhase(), 0.25, 1.0e-9, "stopped Host mode must not advance");

                lfo.updateFromPlayHead (120.0, true, std::nullopt);  // playing without a position
                lfo.advanceBlock (12000);                             // a quarter second: half a cycle at 2 Hz
                expectWithinAbsoluteError (lfo.getPhase(), 0.75, 1.0e-6, "playing without a position free-runs from tempo");

                const auto frozen = lfo.getPhase();
                lfo.updateFromPlayHead (120.0, false, std::nullopt); // stopped without a position
                lfo.advanceBlock (48000);
                expectWithinAbsoluteError (lfo.getPhase(), frozen, 1.0e-12, "stopped without a position holds");
            }

            beginTest ("a negative position is still unavailable to the original legacy synced LFO");
            {
                MorphLFO lfo;
                lfo.prepare (lfoRate);
                lfo.setSyncEnabled (true);
                lfo.setDivisionIndex (2);
                lfo.updateFromPlayHead (120.0, true, 0.25);
                lfo.updateFromPlayHead (120.0, true, -1.0);
                expectWithinAbsoluteError (lfo.getPhase(), 0.25, 1.0e-6, "legacy must ignore negative positions");
            }

            beginTest ("free runs while stopped, and first note restarts only on request");
            {
                MorphLFO freeLfo;
                freeLfo.prepare (lfoRate);
                freeLfo.setRetrigger (LfoRetrigger::free);
                freeLfo.setSyncEnabled (true);
                freeLfo.setDivisionIndex (2);
                freeLfo.updateFromPlayHead (120.0, false, 0.0);
                freeLfo.advanceBlock (12000);
                expect (freeLfo.getPhase() > 0.1, "free must advance while the transport is stopped");

                MorphLFO firstNote;
                firstNote.prepare (lfoRate);
                firstNote.setRetrigger (LfoRetrigger::firstNote);
                firstNote.setRateHz (1.0f);
                firstNote.advanceBlock (4800);
                firstNote.noteStarted();
                expectWithinAbsoluteError (firstNote.getPhase(), 0.0, 1.0e-12, "first note restarts the cycle");
                expectEquals (static_cast<int> (firstNote.getCycle()), 0);

                MorphLFO legacy;
                legacy.prepare (lfoRate);
                legacy.setRateHz (1.0f);
                legacy.advanceBlock (4800);
                const auto before = legacy.getPhase();
                legacy.noteStarted();
                expectWithinAbsoluteError (legacy.getPhase(), before, 1.0e-12, "legacy ignores note starts");
            }

            beginTest ("the phase offset shifts the cycle and wraps at one");
            {
                MorphLFO lfo;
                lfo.prepare (lfoRate);
                lfo.setPhaseOffset (0.25f);
                lfo.advanceBlock (0);
                expectWithinAbsoluteError (lfo.getCurrentValue(), 1.0f, 1.0e-5f, "a quarter offset starts at the crest");

                // An offset of one is a whole cycle, so it is the same as none.
                lfo.setPhaseOffset (1.0f);
                lfo.advanceBlock (0);
                expectWithinAbsoluteError (lfo.getCurrentValue(), 0.0f, 1.0e-6f, "offset one is no offset");

                lfo.setPhaseOffset (std::nanf (""));
                lfo.advanceBlock (0);
                expectWithinAbsoluteError (lfo.getCurrentValue(), 0.0f, 1.0e-6f, "a non-finite offset is ignored");
            }

            beginTest ("block partition does not change the accumulated phase of any mode");
            {
                for (const auto retrigger : { LfoRetrigger::legacy, LfoRetrigger::free, LfoRetrigger::firstNote })
                {
                    MorphLFO whole;
                    MorphLFO split;
                    whole.prepare (lfoRate);
                    split.prepare (lfoRate);
                    for (auto* lfo : { &whole, &split })
                    {
                        lfo->setShape (LfoShape::smoothRandom);
                        lfo->setRetrigger (retrigger);
                        lfo->setRateHz (3.0f);
                        lfo->setSeed (42);
                    }

                    whole.advanceBlock (6000);
                    for (int i = 0; i < 60; ++i)
                        split.advanceBlock (100);

                    expectWithinAbsoluteError (whole.getPhase(), split.getPhase(), 1.0e-9, "phase depends on blocks");
                    expectEquals (static_cast<int> (whole.getCycle()), static_cast<int> (split.getCycle()));
                }
            }

            beginTest ("the held-note counter starts a chord once and ignores repeats and bad input");
            {
                HeldNoteCounter counter;
                expect (counter.noteOn (1, 60), "the first key starts the chord");
                expect (! counter.noteOn (1, 64), "later keys do not");
                expect (! counter.noteOn (1, 64), "a repeated key does not count twice");
                expectEquals (counter.count(), 2);

                counter.noteOff (1, 60);
                counter.noteOff (1, 64);
                expectEquals (counter.count(), 0);
                expect (counter.noteOn (1, 67), "a key after all are released starts again");

                expect (! counter.noteOn (2, 67), "channel two is separate and not the first");
                expect (! counter.noteOn (0, 60) && ! counter.noteOn (17, 60) && ! counter.noteOn (1, 128),
                        "out-of-range channels and notes are ignored");

                counter.clearChannel (1);
                expectEquals (counter.count(), 1);
                counter.clear();
                expectEquals (counter.count(), 0);
            }

            beginTest ("the modulation parameters are ranged, registered and never mutate the curve");
            {
                expect (! PresetManager::isValidParameterValue (params::lfoShape, 5.0));
                expect (PresetManager::isValidParameterValue (params::lfoShape, 4.0));
                expect (! PresetManager::isValidParameterValue (params::lfoRetrigger, 4.0));
                expect (! PresetManager::isValidParameterValue (params::lfoPhase, 1.5));
                expect (! PresetManager::isValidParameterValue (params::lfoSlewMs, 101.0));
                expect (PresetManager::isValidParameterValue (params::lfoSlewMs, 100.0));

                for (const auto* id : { params::lfoShape, params::lfoRetrigger })
                {
                    const auto* field = LockPolicy::findField (id);
                    expect (field != nullptr && field->group == LockGroup::motion && ! field->mutableByDefault, id);
                }

                for (const auto* id : { params::lfoPhase, params::lfoSlewMs })
                {
                    const auto* field = LockPolicy::findField (id);
                    expect (field != nullptr && field->group == LockGroup::motion && field->mutableByDefault, id);
                }

                juce::ValueTree parameters ("Parameters");
                const auto missing = StateSchema::missingParameterIDs (parameters);
                expect (missing.contains (params::lfoShape) && missing.contains (params::lfoSlewMs),
                        "older states default the modulation controls");
            }

            beginTest ("a curve survives host state and rejects corrupt data");
            {
                ModulationCurve curve;
                expect (ModulationCurve::fromPoints (points ({ { 0.0f, 0.0f }, { 0.3f, 0.7f }, { 1.0f, -0.2f } }), curve));

                ModulationCurve restored;
                expect (StateSchema::curveFromTree (StateSchema::curveToTree (curve), restored));
                expect (restored == curve, "a curve must round-trip through host state");

                juce::ValueTree corrupt = StateSchema::curveToTree (curve);
                corrupt.getChild (1).setProperty ("x", 0.0, nullptr);
                expect (! StateSchema::curveFromTree (corrupt, restored), "duplicate positions are corrupt");

                juce::ValueTree text = StateSchema::curveToTree (curve);
                text.getChild (0).setProperty ("y", "loud", nullptr);
                expect (! StateSchema::curveFromTree (text, restored), "non-numeric coordinates are corrupt");

                juce::ValueTree root (StateSchema::rootTag);
                root.setProperty (StateSchema::versionAttribute, "1.0.0", nullptr);
                juce::ValueTree parameters (StateSchema::parametersTag);
                juce::ValueTree parameterState ("PARAM");
                parameters.appendChild (parameterState, nullptr);
                root.appendChild (parameters, nullptr);

                juce::ValueTree feature (StateSchema::featureStateTag);
                feature.setProperty ("schema", StateSchema::featureStateSchemaVersion, nullptr);
                feature.appendChild (corrupt, nullptr);
                root.appendChild (feature, nullptr);

                expectEquals (static_cast<int> (StateSchema::validate (root).status),
                              static_cast<int> (StateSchema::Status::invalidFeatureData));
            }

            beginTest ("presets write schema 3 only for a curve, and read it back exactly");
            {
                TemporaryDirectory temp;
                PresetManager presets (nullptr);
                PresetManager::PresetData data;
                data.name = "Shaped";
                data.setParameter (params::morph, 0.4f);
                data.hasLfoCurve = true;
                data.lfoCurve = ModulationCurve();

                juce::String error;
                const auto plain = temp.child ("plain.texture");
                expectEquals (presets.save (plain, data, error), PresetManager::Status::ok);
                PresetManager::PresetData loaded;
                expectEquals (presets.load (plain, loaded, error), PresetManager::Status::ok);
                expectEquals (loaded.schemaVersion, PresetManager::currentSchemaVersion);
                expect (! loaded.hasLfoCurve, "a default curve is not written");

                ModulationCurve custom;
                expect (ModulationCurve::fromPoints (points ({ { 0.0f, 1.0f }, { 0.6f, -1.0f }, { 1.0f, 1.0f } }), custom));
                data.lfoCurve = custom;
                const auto shaped = temp.child ("shaped.texture");
                expectEquals (presets.save (shaped, data, error), PresetManager::Status::ok);
                expectEquals (presets.load (shaped, loaded, error), PresetManager::Status::ok);
                expectEquals (loaded.schemaVersion, PresetManager::maxSchemaVersion);
                expect (loaded.hasLfoCurve && loaded.lfoCurve == custom, "the curve must read back exactly");

                const auto future = temp.child ("future.texture");
                future.replaceWithText ("{\"schema_version\": 4, \"preset_name\": \"x\", \"parameters\": {}}");
                expectEquals (presets.load (future, loaded, error), PresetManager::Status::schemaTooNew);

                const auto broken = temp.child ("broken.texture");
                broken.replaceWithText ("{\"schema_version\": 3, \"preset_name\": \"x\", \"parameters\": {}, "
                                        "\"features\": {\"lfo_curve\": [[0, 0], [0, 1]]}}");
                expectEquals (presets.load (broken, loaded, error), PresetManager::Status::invalidData);

                const auto legacy = temp.child ("legacy.texture");
                legacy.replaceWithText ("{\"schema_version\": 2, \"preset_name\": \"old\", \"parameters\": {}, "
                                        "\"features\": {\"lfo_curve\": [[0, 0], [1, 1]]}}");
                expectEquals (presets.load (legacy, loaded, error), PresetManager::Status::ok);
                expect (! loaded.hasLfoCurve, "schema 2 ignores features and reads the default curve");
            }

            beginTest ("a curve that reaches the offline renderer changes its output");
            {
                // The export test binds this to ExportJob; here the LFO side of that path.
                ModulationCurve curve;
                expect (ModulationCurve::fromPoints (points ({ { 0.0f, -1.0f }, { 0.5f, 1.0f }, { 1.0f, -1.0f } }), curve));
                MorphLFO shaped;
                shaped.prepare (lfoRate);
                shaped.setShape (LfoShape::curve);
                shaped.setCurve (curve);
                shaped.setRateHz (1.0f);

                // The value is read at the start of each block: half a second at 1 Hz is the peak.
                shaped.advanceBlock (24000);
                shaped.advanceBlock (0);
                expectWithinAbsoluteError (shaped.getCurrentValue(), 1.0f, 1.0e-3f, "the curve's peak at half a cycle");
            }
        }
    };

    static ModulationShapeTests modulationShapeTests;
}
