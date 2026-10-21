#include "TestHelpers.h"

#include "../Source/Engine/GrainClock.h"
#include "../Source/Engine/GrainRenderController.h"
#include "../Source/Engine/GranularEngine.h"
#include "../Source/Presets/LockPolicy.h"
#include "../Source/Presets/PresetManager.h"
#include "../Source/Utility/ParameterIDs.h"

#include <cmath>
#include <cstdlib>
#include <vector>

// F19 rhythmic grains: the grid scheduler (timing, swing, bursts, realignment, overflow),
// the composition the processor and the offline renderer share, the voice-level spawn rules,
// and the rules that connect the controls to locks and presets.

namespace texture::tests
{
    namespace
    {
        constexpr double clockRate = 48000.0;
        constexpr int clockBlock = 512;

        RhythmSettings gridSettings (int division, float swing = 0.0f, int burst = 1,
                                     float probability = 1.0f, float gate = 0.5f,
                                     bool firstImmediate = false)
        {
            RhythmSettings settings;
            settings.rhythmic = true;
            settings.division = division;
            settings.swing = swing;
            settings.burst = burst;
            settings.probability = probability;
            settings.gate = gate;
            settings.firstGrainImmediate = firstImmediate;
            return settings;
        }

        TransportSpan playingAt (double ppq, double bpm)
        {
            TransportSpan transport;
            transport.hasPlayhead = true;
            transport.hasPpq = true;
            transport.playing = true;
            transport.ppq = ppq;
            transport.bpm = bpm;
            return transport;
        }

        struct Hit
        {
            std::int64_t sample;   // absolute sample index of the start
            std::int64_t n;
            int sub;
            int duration;
            float interval;
        };

        /** Runs the clock over consecutive blocks at a constant tempo and collects every start as an
            absolute sample index. The transport starts at `startBeat`. */
        std::vector<Hit> collectConstant (GrainClock& clock, const RhythmSettings& settings,
                                          double startBeat, double bpm, int blocks,
                                          int blockSize = clockBlock)
        {
            std::vector<Hit> hits;
            GrainEventList list;
            const auto samplesPerBeat = 60.0 * clockRate / bpm;

            for (int b = 0; b < blocks; ++b)
            {
                const auto blockStart = static_cast<std::int64_t> (b) * blockSize;
                const auto ppq = startBeat + static_cast<double> (blockStart) / samplesPerBeat;
                clock.generate (settings, playingAt (ppq, bpm), blockSize, list);

                for (int i = 0; i < list.count; ++i)
                {
                    const auto& event = list.events[static_cast<std::size_t> (i)];
                    hits.push_back ({ blockStart + event.offset, event.gridIndex, event.subIndex,
                                      event.durationSamples, event.intervalSamples });
                }
            }

            return hits;
        }

        /** Sample index of a beat position at a constant tempo, rounded up as the clock does. */
        std::int64_t beatToSample (double beat, double bpm)
        {
            return static_cast<std::int64_t> (std::ceil (beat * 60.0 * clockRate / bpm - 1.0e-9));
        }

        double rateOf (double bpm)
        {
            return 60.0 * clockRate / bpm;
        }
    }

    class GrainClockTests : public juce::UnitTest
    {
    public:
        GrainClockTests()
            : juce::UnitTest ("Rhythmic grains", "texture-rhythm") {}

        void runTest() override
        {
            beginTest ("grid points and steps: swing delays odd points and keeps each pair's length");
            {
                const auto step = 0.25;
                expectWithinAbsoluteError (GrainClock::gridPointBeats (0, step, 0.0), 0.0, 1.0e-12);
                expectWithinAbsoluteError (GrainClock::gridPointBeats (3, step, 0.0), 0.75, 1.0e-12);

                for (const auto swing : { 0.0, 0.2, 0.5, 0.75 })
                {
                    for (std::int64_t n = -4; n <= 4; ++n)
                    {
                        const auto pair = GrainClock::gridStepBeats (n, step, swing)
                                        + GrainClock::gridStepBeats (n + 1, step, swing);
                        expectWithinAbsoluteError (pair, 2.0 * step, 1.0e-12);

                        const auto delta = GrainClock::gridPointBeats (n + 1, step, swing)
                                         - GrainClock::gridPointBeats (n, step, swing);
                        expectWithinAbsoluteError (delta, GrainClock::gridStepBeats (n, step, swing), 1.0e-12);
                    }
                }

                // Maximum swing: the odd point sits 3/4 of the way into the pair.
                expectWithinAbsoluteError (GrainClock::gridPointBeats (1, step, 0.75), 0.25 + 0.1875, 1.0e-12);
            }

            beginTest ("divisions map to their musical lengths and unknown indices fall back to 1/16");
            {
                expectWithinAbsoluteError (rhythm::divisionBeats (0), 1.0, 1.0e-12);
                expectWithinAbsoluteError (rhythm::divisionBeats (2), 0.25, 1.0e-12);
                expectWithinAbsoluteError (rhythm::divisionBeats (3), 0.125, 1.0e-12);
                expectWithinAbsoluteError (rhythm::divisionBeats (4), 1.0 / 3.0, 1.0e-12);
                expectWithinAbsoluteError (rhythm::divisionBeats (5), 1.0 / 6.0, 1.0e-12);
                expectWithinAbsoluteError (rhythm::divisionBeats (-1), 0.25, 1.0e-12);
                expectWithinAbsoluteError (rhythm::divisionBeats (99), 0.25, 1.0e-12);
            }

            beginTest ("straight grid: every start lands within one sample of its grid point at several tempos");
            {
                for (const auto bpm : { 60.0, 90.0, 133.7, 240.0, 999.0 })
                {
                    for (const auto division : { 0, 2, 3 })
                    {
                        GrainClock clock;
                        clock.prepare (clockRate);
                        const auto settings = gridSettings (division);
                        const auto hits = collectConstant (clock, settings, 0.0, bpm, 2000);
                        const auto step = rhythm::divisionBeats (division);
                        const auto samplesPerStep = step * rateOf (bpm);
                        const auto expectedCount = static_cast<std::int64_t> (std::ceil (
                            2000.0 * clockBlock / samplesPerStep));

                        // Every grid index appears once, in order, with no gaps or repeats.
                        bool ordered = true;
                        bool timed = true;

                        for (std::size_t i = 0; i < hits.size(); ++i)
                        {
                            ordered = ordered && hits[i].n == static_cast<std::int64_t> (i) && hits[i].sub == 0;
                            const auto expected = beatToSample (static_cast<double> (i) * step, bpm);
                            timed = timed && std::abs (hits[i].sample - expected) <= 1;
                        }

                        expect (ordered, "grid indices must be consecutive: bpm " + juce::String (bpm)
                                             + " division " + juce::String (division));
                        expect (timed, "a start drifted more than one sample: bpm " + juce::String (bpm));
                        expect (std::abs (static_cast<std::int64_t> (hits.size()) - expectedCount) <= 1,
                                "count " + juce::String (static_cast<int> (hits.size())) + " expected about "
                                + juce::String (static_cast<int> (expectedCount)));
                    }
                }
            }

            beginTest ("a block boundary never duplicates or drops a start");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                // One quarter note per half second: eight seconds holds exactly sixteen beats.
                const auto hits = collectConstant (clock, gridSettings (0), 0.0, 120.0, 750, clockBlock);
                expectEquals (static_cast<int> (hits.size()), 16);

                for (std::size_t i = 1; i < hits.size(); ++i)
                    expect (hits[i].sample > hits[i - 1].sample, "starts must be strictly increasing");
            }

            beginTest ("swing: odd grid points move, and the observed pattern matches the swung grid");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                const auto bpm = 120.0;
                const auto hits = collectConstant (clock, gridSettings (2, 0.6f), 0.0, bpm, 800);
                const auto step = rhythm::divisionBeats (2);
                bool timed = ! hits.empty();

                for (const auto& hit : hits)
                {
                    const auto beat = GrainClock::gridPointBeats (hit.n, step, 0.6);
                    timed = timed && std::abs (hit.sample - beatToSample (beat, bpm)) <= 1;
                }

                expect (timed, "swung starts must sit on the swung grid");
            }

            beginTest ("bursts subdivide the interval, stay inside it and keep the gate fraction");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                const auto bpm = 120.0;
                const auto hits = collectConstant (clock, gridSettings (0, 0.0f, 4, 1.0f, 0.5f), 0.0, bpm, 400);

                bool fourPerEvent = true;
                bool insideInterval = true;

                for (const auto& hit : hits)
                {
                    fourPerEvent = fourPerEvent && hit.sub >= 0 && hit.sub < 4;

                    // Each subdivision is a quarter of the beat, and the gate keeps half of that.
                    const auto subSamples = static_cast<double> (hit.interval);
                    insideInterval = insideInterval && std::abs (subSamples - rateOf (bpm) / 4.0) < 1.0;
                    insideInterval = insideInterval && hit.duration <= static_cast<int> (std::ceil (subSamples));
                }

                expect (fourPerEvent, "each grid event must produce its burst subdivisions");
                expect (insideInterval, "a burst grain must not outlast its subdivision");

                // Four starts per beat, over the beats the 400 blocks cover (about 8.5 beats).
                const auto beatsCovered = 400.0 * clockBlock / rateOf (bpm);
                expect (std::abs (static_cast<double> (hits.size()) - 4.0 * std::ceil (beatsCovered)) <= 4.0,
                        "expected four starts per beat, got " + juce::String (static_cast<int> (hits.size())));
            }

            beginTest ("gate-derived length is bounded to the supported grain range");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                // Very fast grid at high tempo with a tiny gate: clamped up to 5 ms.
                const auto fast = collectConstant (clock, gridSettings (3, 0.0f, 1, 1.0f, 0.05f), 0.0, 999.0, 50);
                const auto minSamples = static_cast<int> (std::round (rhythm::minGrainSeconds * clockRate));
                bool floorHeld = ! fast.empty();

                for (const auto& hit : fast)
                    floorHeld = floorHeld && hit.duration >= minSamples;

                expect (floorHeld, "grains shorter than 5 ms were scheduled");

                // A long grid at a slow tempo with a full gate: capped at 500 ms.
                GrainClock slow;
                slow.prepare (clockRate);
                const auto long_ = collectConstant (slow, gridSettings (0, 0.0f, 1, 1.0f, 1.0f), 0.0, 60.0, 400);
                const auto maxSamples = static_cast<int> (std::round (rhythm::maxGrainSeconds * clockRate));
                bool ceilingHeld = ! long_.empty();

                for (const auto& hit : long_)
                    ceilingHeld = ceilingHeld && hit.duration <= maxSamples;

                expect (ceilingHeld, "grains longer than 500 ms were scheduled");
            }

            beginTest ("a negative pre-roll starts at the first grid point at or after the start");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                const auto bpm = 90.0;
                const auto hits = collectConstant (clock, gridSettings (1), -1.5, bpm, 300);
                expect (! hits.empty(), "pre-roll produced no starts");

                // The first grid point at or after -1.5 beats is -1.5 itself (eighths are 0.5 beats).
                // The block starts at -1.5 beats, which is itself an eighth-note point: it starts at offset zero.
                expect (hits.front().sample == 0, "first start should be at the block start, got "
                                                  + juce::String (static_cast<int> (hits.front().sample)));
                expect (hits.front().n == -3, "first grid index should be -3, got " + juce::String (static_cast<int> (hits.front().n)));
            }

            beginTest ("tempo changes keep the count exact and never replay a passed point");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                const auto step = rhythm::divisionBeats (2);
                double ppq = 0.0;
                std::int64_t starts = 0;
                GrainEventList list;

                for (int b = 0; b < 2000; ++b)
                {
                    const auto bpm = (b / 100) % 2 == 0 ? 90.0 : 150.0;
                    clock.generate (gridSettings (2), playingAt (ppq, bpm), clockBlock, list);
                    starts += list.count;
                    ppq += static_cast<double> (clockBlock) * bpm / (60.0 * clockRate);
                }

                const auto expected = static_cast<std::int64_t> (std::ceil (ppq / step));
                expect (std::abs (starts - expected) <= 1,
                        "starts " + juce::String (static_cast<int> (starts)) + " expected " + juce::String (static_cast<int> (expected)));
            }

            beginTest ("a forward seek realigns to the grid with no starts for the skipped span");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                GrainEventList list;
                double ppq = 0.0;

                for (int b = 0; b < 200; ++b)
                {
                    clock.generate (gridSettings (2), playingAt (ppq, 120.0), clockBlock, list);
                    ppq += static_cast<double> (clockBlock) * 120.0 / (60.0 * clockRate);
                }

                const auto target = ppq + 50.0;
                clock.generate (gridSettings (2), playingAt (target, 120.0), clockBlock, list);

                bool noneBefore = true;
                for (int i = 0; i < list.count; ++i)
                {
                    const auto beat = target + static_cast<double> (list.events[static_cast<std::size_t> (i)].offset)
                                              / rateOf (120.0);
                    noneBefore = noneBefore && beat >= target - 1.0e-6;
                }

                expect (noneBefore, "a seek replayed a point before the new position");
                expect (list.count <= 2, "a seek produced a burst: " + juce::String (list.count));
                expectEquals (static_cast<int> (list.dropped), 0);
            }

            beginTest ("a loop back is realigned without a catch-up burst");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                GrainEventList list;
                double ppq = 16.0;

                for (int b = 0; b < 100; ++b)
                {
                    clock.generate (gridSettings (2), playingAt (ppq, 120.0), clockBlock, list);
                    ppq += static_cast<double> (clockBlock) * 120.0 / (60.0 * clockRate);
                }

                clock.generate (gridSettings (2), playingAt (0.0, 120.0), clockBlock, list);
                expect (list.count <= 1, "a loop back produced " + juce::String (list.count) + " starts");
            }

            beginTest ("a stopped transport creates nothing, and resuming realigns");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                GrainEventList list;
                auto stopped = playingAt (4.0, 120.0);
                stopped.playing = false;

                std::int64_t starts = 0;
                for (int b = 0; b < 200; ++b)
                {
                    clock.generate (gridSettings (2), stopped, clockBlock, list);
                    starts += list.count;
                }

                expectEquals (static_cast<int> (starts), 0);

                clock.generate (gridSettings (2), playingAt (4.0 + 1.0e-3, 120.0), clockBlock, list);
                expect (list.count <= 1, "resume produced a burst");
            }

            beginTest ("without a host transport the grid free-runs at the last valid tempo");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                GrainEventList list;
                TransportSpan none;
                none.bpm = 120.0;

                std::int64_t starts = 0;
                for (int b = 0; b < 750; ++b)
                {
                    clock.generate (gridSettings (0), none, clockBlock, list);
                    starts += list.count;
                }

                // Eight seconds at 120 BPM is sixteen beats, and a quarter note is one beat.
                expect (std::abs (starts - 16) <= 1, "free-run starts " + juce::String (static_cast<int> (starts)));

                // A non-finite or non-positive tempo keeps the last valid one.
                TransportSpan bad;
                bad.bpm = std::nan ("");
                clock.generate (gridSettings (0), bad, clockBlock, list);
                expectWithinAbsoluteError (clock.getTempoBpm(), 120.0, 1.0e-9);
            }

            beginTest ("a full event list drops the newest starts, counts them, and keeps timing");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                GrainEventList list;
                // Sixteen thousand samples of a 1/32 grid with bursts of four at 999 BPM: about 180 starts.
                clock.generate (gridSettings (3, 0.0f, 4), playingAt (0.0, 999.0), 16384, list);
                expectEquals (list.count, GrainEventList::capacity);
                expect (list.dropped > 0, "overflow was not counted");

                for (int i = 1; i < list.count; ++i)
                    expect (list.events[static_cast<std::size_t> (i)].offset
                                >= list.events[static_cast<std::size_t> (i - 1)].offset,
                            "overflow must not reorder starts");
            }

            beginTest ("a division change is held for 20 ms, then the new grid starts");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                const auto bpm = 120.0;
                const std::int64_t switchSample = 48000;   // one second in
                std::vector<std::int64_t> times;
                GrainEventList list;
                double ppq = 0.0;

                for (int b = 0; b < 300; ++b)
                {
                    const auto blockStart = static_cast<std::int64_t> (b) * clockBlock;
                    const auto division = blockStart < switchSample ? 0 : 2;   // quarter, then sixteenth
                    clock.generate (gridSettings (division), playingAt (ppq, bpm), clockBlock, list);

                    for (int i = 0; i < list.count; ++i)
                        times.push_back (blockStart + list.events[static_cast<std::size_t> (i)].offset);

                    ppq += static_cast<double> (clockBlock) * bpm / (60.0 * clockRate);
                }

                // Find the first sixteenth-spaced start: the gap to the previous start is 1/8 s.
                const auto sixteenth = static_cast<std::int64_t> (std::round (rateOf (bpm) / 4.0));
                std::int64_t firstFast = -1;

                for (std::size_t i = 1; i < times.size(); ++i)
                    if (std::abs ((times[i] - times[i - 1]) - sixteenth) <= 1 && times[i] > switchSample)
                    {
                        firstFast = times[i];
                        break;
                    }

                expect (firstFast >= 0, "the sixteenth grid never started");

                const auto holdSamples = static_cast<std::int64_t> (std::llround (rhythm::transitionSeconds * clockRate));
                expect (firstFast - switchSample >= holdSamples,
                        "the new grid started inside the 20 ms hold: " + juce::String (static_cast<int> (firstFast - switchSample)));
                expect (firstFast - switchSample <= holdSamples + 2 * static_cast<std::int64_t> (rateOf (bpm) / 2.0),
                        "the new grid waited too long after the hold");
            }

            beginTest ("legacy settings create no starts and leave the clock silent");
            {
                GrainClock clock;
                clock.prepare (clockRate);
                GrainEventList list;
                RhythmSettings legacy;
                legacy.rhythmic = false;

                for (int b = 0; b < 100; ++b)
                {
                    clock.generate (legacy, playingAt (b * 0.04, 120.0), clockBlock, list);
                    expectEquals (list.count, 0);
                }
            }

            beginTest ("the rhythm controls are mutable only while the grid is on, and never structural ones");
            {
                expect (! rhythm::isRhythmParameterMutable (params::grainClock, true), "clock must not mutate");
                expect (! rhythm::isRhythmParameterMutable (params::grainDivision, true), "division must not mutate");
                expect (! rhythm::isRhythmParameterMutable (params::grainFirstImmediate, true), "first grain is a switch");
                expect (! rhythm::isRhythmParameterMutable (params::grainSwing, false), "swing mutable while Legacy");
                expect (rhythm::isRhythmParameterMutable (params::grainSwing, true), "swing not mutable while rhythmic");
                expect (rhythm::isRhythmParameterMutable (params::grainGate, true), "gate not mutable while rhythmic");
                expect (rhythm::isRhythmParameterMutable (params::morph, false), "non-rhythm controls are not gated");
            }

            beginTest ("the lock registry covers every parameter in the same order");
            {
                const auto& fields = LockPolicy::fields();
                expectEquals (static_cast<int> (fields.size()), params::numParameters);

                for (std::size_t i = 0; i < fields.size(); ++i)
                    expect (fields[i].id == std::string_view (params::allIDs[i]),
                            "registry order differs at " + juce::String (static_cast<int> (i)));
            }

            beginTest ("presets accept in-range rhythm values and refuse out-of-range ones");
            {
                expect (PresetManager::isValidParameterValue (params::grainClock, 1.0), "rhythmic clock refused");
                expect (! PresetManager::isValidParameterValue (params::grainClock, 2.0), "clock index 2 accepted");
                expect (PresetManager::isValidParameterValue (params::grainDivision, 5.0), "division 5 refused");
                expect (! PresetManager::isValidParameterValue (params::grainDivision, 6.0), "division 6 accepted");
                expect (! PresetManager::isValidParameterValue (params::grainDivision, 1.5), "fractional division accepted");
                expect (PresetManager::isValidParameterValue (params::grainSwing, 0.75), "maximum swing refused");
                expect (! PresetManager::isValidParameterValue (params::grainSwing, 0.8), "swing above maximum accepted");
                expect (! PresetManager::isValidParameterValue (params::grainGate, 0.01), "gate below minimum accepted");
                expect (PresetManager::isValidParameterValue (params::grainBurst, 4.0), "burst 4 refused");
                expect (! PresetManager::isValidParameterValue (params::grainBurst, 0.0), "burst 0 accepted");
                expect (! PresetManager::isValidParameterValue (params::grainBurst, 2.5), "fractional burst accepted");
                expect (! PresetManager::isValidParameterValue (params::grainFirstImmediate, 0.5), "non-switch accepted");
            }

            beginTest ("the controller gives the same starts whether a span is rendered whole or in halves");
            {
                GrainRenderController whole;
                GrainRenderController halves;
                whole.prepare (clockRate);
                halves.prepare (clockRate);

                const auto settings = gridSettings (3, 0.4f, 2, 0.7f, 0.6f, true);
                TransportSpan transport = playingAt (7.25, 133.0);

                whole.beginBlock (settings, true, transport, 512);
                const auto wholeSpan = whole.renderSpan (0, 512);
                std::vector<std::int64_t> wholeStarts;

                for (int i = 0; i < wholeSpan.events->count; ++i)
                    wholeStarts.push_back (wholeSpan.events->events[static_cast<std::size_t> (i)].offset);

                halves.beginBlock (settings, true, transport, 512);
                std::vector<std::int64_t> halfStarts;

                for (const int offset : { 0, 256 })
                {
                    const auto& part = halves.renderSpan (offset, 256);
                    for (int i = 0; i < part.events->count; ++i)
                        halfStarts.push_back (offset + part.events->events[static_cast<std::size_t> (i)].offset);
                }

                expectEquals (static_cast<int> (halfStarts.size()), static_cast<int> (wholeStarts.size()));

                // Each start may move by one sample where the split rounds a position differently.
                bool same = halfStarts.size() == wholeStarts.size();
                for (std::size_t i = 0; same && i < wholeStarts.size(); ++i)
                    same = std::abs (halfStarts[i] - wholeStarts[i]) <= 1;

                expect (same, "splitting a span moved a start by more than one sample");
            }

            beginTest ("the controller reports Legacy as inactive, with no starts");
            {
                GrainRenderController controller;
                controller.prepare (clockRate);
                controller.beginBlock (RhythmSettings {}, true, playingAt (0.0, 120.0), 512);
                const auto& span = controller.renderSpan (0, 512);
                expect (! span.active, "legacy span reported as rhythmic");
                expect (span.events == nullptr, "legacy span carries an event list");
            }

            beginTest ("the engine renders bit-identically with an inactive span and with none");
            {
                const auto sample = makeTestSample (clockRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.5f);

                const auto render = [&] (const RhythmSpan* span)
                {
                    GranularEngine engine;
                    engine.prepare (clockRate, clockBlock);
                    engine.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);
                    engine.noteOn (1, 60, 0.8f);
                    engine.noteOn (1, 67, 0.6f);

                    juce::AudioBuffer<float> buffer (2, clockBlock);
                    std::vector<float> samples;

                    for (int b = 0; b < 60; ++b)
                    {
                        buffer.clear();
                        engine.renderNextBlock (sample, targets, buffer, 0, clockBlock, span);

                        for (int i = 0; i < clockBlock; ++i)
                            samples.push_back (buffer.getSample (0, i));
                    }

                    return samples;
                };

                RhythmSpan inactive;
                const auto plain = render (nullptr);
                const auto withInactive = render (&inactive);

                bool identical = plain.size() == withInactive.size();
                for (std::size_t i = 0; identical && i < plain.size(); ++i)
                    identical = juce::exactlyEqual (plain[i], withInactive[i]);

                expect (identical, "the default path changed when the rhythm span is inactive");
            }

            beginTest ("the first grain of a note starts at once only when the toggle is on");
            {
                const auto spawnsWith = [] (bool immediate, float probability)
                {
                    GrainRenderController controller;
                    controller.prepare (clockRate);
                    const auto sample = makeTestSample (clockRate, 2.0, 220.0);
                    const auto targets = deriveMorphTargets (0.5f);

                    GranularEngine engine;
                    engine.prepare (clockRate, clockBlock);
                    engine.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);

                    // The grid is off the note's start, so only the immediate grain can sound here.
                    engine.noteOn (1, 60, 0.8f);

                    juce::AudioBuffer<float> buffer (2, clockBlock);
                    double ppq = 0.5;   // half a beat in: the next straight sixteenth is 0.25 beats away

                    for (int b = 0; b < 2; ++b)
                    {
                        controller.beginBlock (gridSettings (2, 0.0f, 1, probability, 0.5f, immediate), true,
                                               playingAt (ppq, 120.0), clockBlock);
                        buffer.clear();
                        engine.renderNextBlock (sample, targets, buffer, 0, clockBlock, &controller.renderSpan (0, clockBlock));
                        ppq += static_cast<double> (clockBlock) * 120.0 / (60.0 * clockRate);
                    }

                    return engine.getGrainStartCount();
                };

                expectEquals (static_cast<int> (spawnsWith (false, 0.0f)), 0);
                expectEquals (static_cast<int> (spawnsWith (true, 0.0f)), 1);
            }

            beginTest ("grid starts follow probability: none at zero, a repeatable share in between");
            {
                const auto countAt = [] (float probability)
                {
                    GranularEngine engine;
                    engine.prepare (clockRate, clockBlock);
                    engine.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);
                    engine.setRandomConfig (0, 0x1234567ULL);
                    const auto sample = makeTestSample (clockRate, 3.0, 220.0);
                    const auto targets = deriveMorphTargets (0.5f);

                    GrainRenderController controller;
                    controller.prepare (clockRate);
                    engine.noteOn (1, 60, 0.8f);

                    juce::AudioBuffer<float> buffer (2, clockBlock);
                    double ppq = 0.0;

                    for (int b = 0; b < 375; ++b)
                    {
                        controller.beginBlock (gridSettings (2, 0.0f, 1, probability, 0.5f, false), true,
                                               playingAt (ppq, 120.0), clockBlock);
                        buffer.clear();
                        engine.renderNextBlock (sample, targets, buffer, 0, clockBlock, &controller.renderSpan (0, clockBlock));
                        ppq += static_cast<double> (clockBlock) * 120.0 / (60.0 * clockRate);
                    }

                    return static_cast<int> (engine.getGrainStartCount());
                };

                expectEquals (countAt (0.0f), 0);
                const auto full = countAt (1.0f);
                const auto half = countAt (0.5f);
                // Three seconds at 1/16 and 120 BPM is 24 grid events.
                expect (full >= 20, "full probability should start most grid events, got " + juce::String (full));
                expect (half > full / 4 && half < (full * 3) / 4, "half probability was " + juce::String (half)
                                                                 + " of " + juce::String (full));
                expectEquals (countAt (0.5f), half);   // repeatable for the same seed
            }

            beginTest ("rhythmic golden script matches its recorded render");
            {
                // Self-generated golden checkpoints for the F19 path (not archived legacy audio). A
                // deliberate change to the rhythmic path must update them, with the reason in PROGRESS.md.
                const auto sample = makeTestSample (clockRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                GranularEngine engine;
                engine.prepare (clockRate, clockBlock);
                engine.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);
                engine.setRandomConfig (0, 0x5445585455524531ULL);
                engine.noteOn (1, 60, 0.8f);

                GrainRenderController controller;
                controller.prepare (clockRate);

                juce::AudioBuffer<float> buffer (2, clockBlock);
                std::vector<float> checkpoints;

                for (int b = 0; b < 96; ++b)
                {
                    controller.beginBlock (gridSettings (2, 0.25f, 2, 1.0f, 0.4f, true), true,
                                           playingAt (b * clockBlock * 120.0 / (60.0 * clockRate), 120.0), clockBlock);
                    buffer.clear();
                    engine.renderNextBlock (sample, targets, buffer, 0, clockBlock, &controller.renderSpan (0, clockBlock));

                    if (b % 16 == 15)
                    {
                        checkpoints.push_back (buffer.getSample (0, 37));
                        checkpoints.push_back (buffer.getSample (1, 101));
                    }
                }

                // Blocks 15, 31, 47, 63, 79 and 95; samples 37 (left) and 101 (right).
                const std::array<float, 12> expected {
                    0.068697579f, 0.126651913f,
                    0.083751030f, 0.157577083f,
                    -0.059982527f, -0.032497454f,
                    0.005829266f, -0.035223566f,
                    0.051039349f, 0.229616567f,
                    -0.236987650f, 0.144763514f
                };

                expectEquals (static_cast<int> (engine.getGrainStartCount()), 18);
                expectEquals (static_cast<int> (checkpoints.size()), static_cast<int> (expected.size()));

                for (std::size_t i = 0; i < expected.size() && i < checkpoints.size(); ++i)
                    expect (std::abs (checkpoints[i] - expected[i]) <= 1.0e-5f,
                            "checkpoint " + juce::String (static_cast<int> (i)) + " actual "
                            + juce::String (checkpoints[i], 9));
            }

            beginTest ("a stopped transport starts nothing; the render stays finite and bounded");
            {
                GranularEngine engine;
                engine.prepare (clockRate, clockBlock);
                engine.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);
                engine.noteOn (1, 60, 0.8f);
                const auto sample = makeTestSample (clockRate, 3.0, 220.0);
                const auto targets = deriveMorphTargets (0.5f);

                GrainRenderController controller;
                controller.prepare (clockRate);
                TransportSpan stopped = playingAt (0.0, 120.0);
                stopped.playing = false;

                juce::AudioBuffer<float> buffer (2, clockBlock);
                bool finite = true;
                int peakGrains = 0;

                for (int b = 0; b < 300; ++b)
                {
                    controller.beginBlock (gridSettings (3, 0.75f, 4, 1.0f, 1.0f, true), true, stopped, clockBlock);
                    buffer.clear();
                    engine.renderNextBlock (sample, targets, buffer, 0, clockBlock, &controller.renderSpan (0, clockBlock));
                    finite = finite && isFinite (buffer);
                    peakGrains = juce::jmax (peakGrains, engine.getActiveGrainCount());
                }

                expect (finite, "non-finite output while stopped");
                expectEquals (static_cast<int> (engine.getGrainStartCount()), 0);
                expectEquals (peakGrains, 0);
            }

            beginTest ("an aggressive grid stays finite, bounded by the pool, and never overflows the list");
            {
                GranularEngine engine;
                engine.prepare (192000.0, clockBlock);
                engine.setAdsrParameters (5.0f, 500.0f, 1.0f, 1200.0f);
                engine.noteOn (1, 60, 0.9f);
                engine.noteOn (1, 64, 0.9f);
                const auto sample = makeTestSample (192000.0, 2.0, 220.0);
                const auto targets = deriveMorphTargets (1.0f);

                GrainRenderController controller;
                controller.prepare (192000.0);

                juce::AudioBuffer<float> buffer (2, clockBlock);
                bool finite = true;
                int peak = 0;
                std::uint64_t dropped = 0;

                for (int b = 0; b < 600; ++b)
                {
                    const auto ppq = static_cast<double> (b) * clockBlock * 999.0 / (60.0 * 192000.0);
                    controller.beginBlock (gridSettings (3, 0.75f, 4, 1.0f, 1.0f, true), true,
                                           playingAt (ppq, 999.0), clockBlock);
                    buffer.clear();
                    engine.renderNextBlock (sample, targets, buffer, 0, clockBlock, &controller.renderSpan (0, clockBlock));
                    finite = finite && isFinite (buffer);
                    peak = juce::jmax (peak, engine.getActiveGrainCount());
                    dropped = controller.getDroppedEventCount();
                }

                expect (finite, "non-finite output under an aggressive grid");
                expect (peak <= 2 * Voice::maxGrains, "more grains sounding than the pools allow: " + juce::String (peak));
                expectEquals (static_cast<int> (dropped), 0);
            }
        }
    };

    static GrainClockTests grainClockTests;
}
