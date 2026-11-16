#include "TestHelpers.h"

#include "../Source/Engine/GranularEngine.h"
#include "../Source/Engine/SamplePositionController.h"
#include "../Source/Presets/LockPolicy.h"
#include "../Source/Presets/PresetManager.h"

#include <cmath>
#include <vector>

// F10 freeze and scan: the controller's rules (glide, scan advance, freeze latch and its
// acknowledgment, the unfreeze jump), what the engine reads with a known silent region, and
// the range and lock contracts.

namespace texture::tests
{
    namespace
    {
        constexpr double scanRate = 48000.0;
        constexpr int scanBlock = 128;

        /** A sample that sounds for its first `soundSeconds` and is silent afterwards. */
        SampleBufferPtr soundThenSilence (double seconds, double soundSeconds)
        {
            const auto frames = static_cast<int> (seconds * scanRate);
            juce::AudioBuffer<float> audio (2, frames);
            audio.clear();

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < static_cast<int> (soundSeconds * scanRate); ++i)
                    audio.setSample (ch, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                             * 440.0f * static_cast<float> (i) / static_cast<float> (scanRate)));

            return std::make_shared<const SampleBuffer> (std::move (audio), scanRate, juce::File(), 2, scanRate, false);
        }

        float rmsOf (const juce::AudioBuffer<float>& buffer)
        {
            double sum = 0.0;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    sum += static_cast<double> (buffer.getSample (ch, i)) * buffer.getSample (ch, i);

            return static_cast<float> (std::sqrt (sum / (2.0 * juce::jmax (1, buffer.getNumSamples()))));
        }

        ScanSettings manual (float position)
        {
            ScanSettings settings;
            settings.mode = ScanMode::manual;
            settings.position = position;
            return settings;
        }

        /** Renders `seconds` of one held note and returns the RMS of the last `measureSeconds`. */
        float renderRms (GranularEngine& engine, const SampleBufferPtr& sample, double seconds,
                         double measureSeconds, const MorphTargets& targets)
        {
            juce::AudioBuffer<float> buffer (2, scanBlock);
            const auto blocks = static_cast<int> (seconds * scanRate / scanBlock);
            const auto measureFrom = static_cast<int> ((seconds - measureSeconds) * scanRate / scanBlock);
            double sum = 0.0;
            int counted = 0;

            for (int block = 0; block < blocks; ++block)
            {
                buffer.clear();
                engine.renderNextBlock (sample, targets, buffer, 0, scanBlock);

                if (block >= measureFrom)
                {
                    sum += static_cast<double> (rmsOf (buffer)) * static_cast<double> (rmsOf (buffer));
                    ++counted;
                }
            }

            return static_cast<float> (std::sqrt (sum / juce::jmax (1, counted)));
        }
    }

    class ScanPositionTests : public juce::UnitTest
    {
    public:
        ScanPositionTests()
            : juce::UnitTest ("Scan and freeze", "texture") {}

        void runTest() override
        {
            beginTest ("Legacy with Freeze off is inactive and the centre follows the voices");
            {
                SamplePositionController controller;
                controller.prepare (scanRate);
                const auto state = controller.advance (ScanSettings {}, scanBlock, 100000.0, 0.37, 0.12f);
                expect (! state.active, "the default path must not take over the read position");
                expect (! state.latched && ! state.jumped);
                expectWithinAbsoluteError (static_cast<float> (state.centre), 0.37f, 1.0e-6f);
            }

            beginTest ("Manual glides to scan_position over 20 ms and then settles exactly");
            {
                SamplePositionController controller;
                controller.prepare (scanRate);
                controller.advance (ScanSettings {}, scanBlock, 100000.0, 0.2, 0.0f);

                const auto target = manual (0.8f);
                const auto first = controller.advance (target, 480, 100000.0, 0.2, 0.0f);
                expect (first.active);
                expect (first.centre > 0.2 && first.centre < 0.8, "the glide must be part way after 10 ms");

                const auto settled = controller.advance (target, 960, 100000.0, 0.2, 0.0f);
                expectWithinAbsoluteError (static_cast<float> (settled.centre), 0.8f, 1.0e-6f, "the glide must land");
                expectWithinAbsoluteError (static_cast<float> (controller.advance (target, scanBlock, 100000.0, 0.2, 0.0f).centre),
                                           0.8f, 1.0e-6f);
            }

            beginTest ("Scan advances by speed in source lengths per second and wraps inside the source");
            {
                SamplePositionController controller;
                controller.prepare (scanRate);
                ScanSettings scan;
                scan.mode = ScanMode::scan;
                scan.position = 0.2f;
                scan.speed = 1.0f;

                controller.advance (scan, scanBlock, 100000.0, 0.0, 0.0f);
                const auto after = controller.advance (scan, 24000, 100000.0, 0.0, 0.0f);
                expectWithinAbsoluteError (static_cast<float> (after.centre), 0.2f + 0.5f + 0.0f, 0.05f,
                                           "half a second at one source per second moves half the source");

                scan.speed = -4.0f;
                scan.position = 0.1f;
                controller.advance (scan, scanBlock, 100000.0, 0.0, 0.0f);
                const auto wrapped = controller.advance (scan, 12000, 100000.0, 0.0, 0.0f);
                expect (wrapped.centre >= 0.0 && wrapped.centre < 1.0, "the centre wraps inside [0, 1)");
                expect (std::isfinite (wrapped.centre));
            }

            beginTest ("an edit of scan_position moves a scanning centre directly");
            {
                SamplePositionController controller;
                controller.prepare (scanRate);
                ScanSettings scan;
                scan.mode = ScanMode::scan;
                scan.speed = 0.0f;
                scan.position = 0.3f;
                controller.advance (scan, scanBlock, 100000.0, 0.0, 0.0f);

                scan.position = 0.6f;
                expectWithinAbsoluteError (static_cast<float> (controller.advance (scan, scanBlock, 100000.0, 0.0, 0.0f).centre),
                                           0.6f, 1.0e-6f);
            }

            beginTest ("a freeze latches the current centre once, and stays put while legacy voices move on");
            {
                SamplePositionController controller;
                controller.prepare (scanRate);
                controller.advance (ScanSettings {}, scanBlock, 100000.0, 0.31, 0.0f);

                ScanSettings frozen;
                frozen.freeze = true;
                frozen.freezeSpread = 0.4f;
                const auto latch = controller.advance (frozen, scanBlock, 100000.0, 0.31, 0.0f);
                expect (latch.latched, "turning Freeze on must latch");
                expectWithinAbsoluteError (static_cast<float> (latch.latchedCentre), 0.31f, 1.0e-6f);
                expect (latch.active, "a frozen layer renders from the frozen centre");
                expectWithinAbsoluteError (latch.jitter, 0.4f * 0.25f, 1.0e-6f, "frozen jitter follows the spread");

                const auto later = controller.advance (frozen, scanBlock, 100000.0, 0.9, 0.0f);
                expect (! later.latched, "a latch happens once");
                expectWithinAbsoluteError (static_cast<float> (later.centre), 0.31f, 1.0e-6f,
                                           "the frozen centre must not follow the voices");
            }

            beginTest ("the processor's write of the frozen position causes no move, and a later edit does");
            {
                SamplePositionController controller;
                controller.prepare (scanRate);
                controller.advance (ScanSettings {}, scanBlock, 100000.0, 0.31, 0.0f);

                ScanSettings frozen;
                frozen.freeze = true;
                frozen.position = 0.0f;   // scan_position before the acknowledgment arrives
                controller.advance (frozen, scanBlock, 100000.0, 0.31, 0.0f);

                const auto held = controller.advance (frozen, scanBlock, 100000.0, 0.31, 0.0f);
                expectWithinAbsoluteError (static_cast<float> (held.centre), 0.31f, 1.0e-6f,
                                           "before the write arrives the centre holds");

                frozen.position = 0.31f;  // the acknowledgment
                expectWithinAbsoluteError (static_cast<float> (controller.advance (frozen, scanBlock, 100000.0, 0.31, 0.0f).centre),
                                           0.31f, 1.0e-6f, "the acknowledgment changes nothing");

                frozen.position = 0.7f;   // a user edit while frozen
                controller.advance (frozen, 480, 100000.0, 0.31, 0.0f);
                const auto moved = controller.advance (frozen, 480, 100000.0, 0.31, 0.0f);
                expectWithinAbsoluteError (static_cast<float> (moved.centre), 0.7f, 1.0e-6f, "a user edit moves it");
            }

            beginTest ("ending a freeze in Legacy hands the voices the frozen centre, then the voices own it again");
            {
                SamplePositionController controller;
                controller.prepare (scanRate);
                controller.advance (ScanSettings {}, scanBlock, 100000.0, 0.4, 0.0f);

                ScanSettings frozen;
                frozen.freeze = true;
                controller.advance (frozen, scanBlock, 100000.0, 0.4, 0.0f);

                const auto release = controller.advance (ScanSettings {}, scanBlock, 100000.0, 0.85, 0.0f);
                expect (release.jumped, "the voices must be moved");
                expectWithinAbsoluteError (static_cast<float> (release.centre), 0.4f, 1.0e-6f,
                                           "they move to the frozen centre, not the original start");

                const auto after = controller.advance (ScanSettings {}, scanBlock, 100000.0, 0.85, 0.0f);
                expect (! after.jumped && ! after.active);
                expectWithinAbsoluteError (static_cast<float> (after.centre), 0.85f, 1.0e-6f);
            }

            beginTest ("a new sample releases a freeze and clamps the saved position");
            {
                SamplePositionController controller;
                controller.prepare (scanRate);
                ScanSettings frozen;
                frozen.freeze = true;
                controller.advance (frozen, scanBlock, 100000.0, 0.5, 0.0f);
                expect (controller.isFrozen());

                controller.reset (3.0f);
                expect (! controller.isFrozen(), "a reset clears the latch");

                // In Legacy the new sample's voices start at 0, so a freeze latches 0.
                const auto legacy = controller.advance (frozen, scanBlock, 100000.0, 0.0, 0.0f);
                expect (legacy.latched, "a freeze still set latches again on the new sample");
                expectWithinAbsoluteError (static_cast<float> (legacy.latchedCentre), 0.0f, 1.0e-6f);

                // In Manual the saved position is the centre, and it is clamped to the source.
                SamplePositionController manualController;
                manualController.prepare (scanRate);
                manualController.reset (3.0f);
                ScanSettings manualFrozen = frozen;
                manualFrozen.mode = ScanMode::manual;
                const auto state = manualController.advance (manualFrozen, scanBlock, 100000.0, 0.0, 0.0f);
                expectWithinAbsoluteError (static_cast<float> (state.latchedCentre), 1.0f, 1.0e-6f,
                                           "the saved position is clamped to the source");
            }

            beginTest ("an empty source, a silent non-finite position and extreme speed are handled");
            {
                SamplePositionController controller;
                controller.prepare (scanRate);
                ScanSettings scan;
                scan.mode = ScanMode::scan;
                scan.speed = 1.0e9f;
                const auto empty = controller.advance (scan, scanBlock, 0.0, 0.2, 0.0f);
                expect (! empty.active, "no source, no scan");

                scan.position = std::nanf ("");
                scan.speed = std::numeric_limits<float>::infinity();
                const auto garbage = controller.advance (scan, scanBlock, 100000.0, 0.2, 0.0f);
                expect (std::isfinite (garbage.centre) && garbage.centre >= 0.0 && garbage.centre <= 1.0);
                expectEquals (static_cast<int> (scanModeFromIndex (9)), static_cast<int> (ScanMode::legacy));
                expect (juce::exactlyEqual (sanitiseScanSettings (ScanSettings { ScanMode::manual, 2.0f, 9.0f, false, 4.0f }).speed, 4.0f));
            }

            beginTest ("a manual position reads the sample where it is, with a silent region to tell it apart");
            {
                const auto sample = soundThenSilence (3.0, 1.0);
                MorphTargets targets = deriveMorphTargets (0.0f);

                GranularEngine early;
                early.prepare (scanRate, scanBlock);
                early.setAdsrParameters (1.0f, 1.0f, 1.0f, 1000.0f);
                early.setScanSettings (manual (0.1f));
                early.noteOn (1, 60, 0.9f);
                const auto soundRms = renderRms (early, sample, 1.0, 0.5, targets);

                GranularEngine silent;
                silent.prepare (scanRate, scanBlock);
                silent.setAdsrParameters (1.0f, 1.0f, 1.0f, 1000.0f);
                silent.setScanSettings (manual (0.9f));
                silent.noteOn (1, 60, 0.9f);
                const auto silentRms = renderRms (silent, sample, 1.0, 0.5, targets);

                expect (soundRms > 0.05f, "the sounding region must be audible");
                expect (silentRms < soundRms * 0.05f, "the silent region must stay silent: " + juce::String (silentRms));
            }

            beginTest ("a freeze in Legacy holds the read point, and without it playback passes the sound");
            {
                const auto sample = soundThenSilence (3.0, 1.0);
                const auto targets = deriveMorphTargets (0.0f);

                GranularEngine free;
                free.prepare (scanRate, scanBlock);
                free.setAdsrParameters (1.0f, 1.0f, 1.0f, 1000.0f);
                free.noteOn (1, 60, 0.9f);
                const auto freeLate = renderRms (free, sample, 2.0, 0.5, targets);

                GranularEngine held;
                held.prepare (scanRate, scanBlock);
                held.setAdsrParameters (1.0f, 1.0f, 1.0f, 1000.0f);
                held.noteOn (1, 60, 0.9f);
                ScanSettings freezeOn;
                freezeOn.freeze = true;
                freezeOn.freezeSpread = 0.0f;
                held.setScanSettings (freezeOn);
                juce::AudioBuffer<float> buffer (2, scanBlock);
                const auto lead = static_cast<int> (0.4 * scanRate / scanBlock);
                for (int block = 0; block < lead; ++block)
                {
                    buffer.clear();
                    held.renderNextBlock (sample, targets, buffer, 0, scanBlock);
                }

                double latched = 0.0;
                expect (held.takeFreezeLatch (latched), "freezing must report its captured centre");
                expect (! held.takeFreezeLatch (latched), "the report is taken once");
                const auto heldLate = renderRms (held, sample, 2.0, 0.5, targets);

                expect (freeLate < 0.01f, "without a freeze the read head passes the sound: " + juce::String (freeLate));
                expect (heldLate > 0.05f, "the frozen read point keeps sounding: " + juce::String (heldLate));
            }

            beginTest ("note-off with a freeze on still retires every voice");
            {
                const auto sample = soundThenSilence (3.0, 3.0);
                const auto targets = deriveMorphTargets (0.35f);

                GranularEngine engine;
                engine.prepare (scanRate, scanBlock);
                engine.setAdsrParameters (1.0f, 1.0f, 1.0f, 100.0f);
                ScanSettings freezeOn;
                freezeOn.freeze = true;
                engine.setScanSettings (freezeOn);
                engine.noteOn (1, 60, 0.9f);
                engine.noteOff (1, 60);

                juce::AudioBuffer<float> buffer (2, scanBlock);
                for (int block = 0; block < static_cast<int> (1.0 * scanRate / scanBlock); ++block)
                {
                    buffer.clear();
                    engine.renderNextBlock (sample, targets, buffer, 0, scanBlock);
                }

                expectEquals (engine.getActiveVoiceCount(), 0);
                expectEquals (engine.getActiveGrainCount(), 0);
            }

            beginTest ("extreme positions and a one-frame sample render finite audio without reading out of range");
            {
                juce::AudioBuffer<float> one (2, 1);
                one.setSample (0, 0, 0.5f);
                one.setSample (1, 0, 0.5f);
                const auto tiny = std::make_shared<const SampleBuffer> (std::move (one), scanRate, juce::File(), 2, scanRate, false);
                const auto targets = deriveMorphTargets (0.6f);

                for (const float position : { 0.0f, 1.0f })
                {
                    GranularEngine engine;
                    engine.prepare (scanRate, scanBlock);
                    ScanSettings scan;
                    scan.mode = ScanMode::scan;
                    scan.position = position;
                    scan.speed = 4.0f;
                    engine.setScanSettings (scan);
                    engine.noteOn (1, 60, 0.9f);

                    juce::AudioBuffer<float> buffer (2, scanBlock);
                    for (int block = 0; block < 200; ++block)
                    {
                        buffer.clear();
                        engine.renderNextBlock (tiny, targets, buffer, 0, scanBlock);
                        expect (isFinite (buffer), "non-finite output at position " + juce::String (position));
                    }
                }
            }

            beginTest ("the scan and freeze ranges are validated, and the controls are locked as a group");
            {
                expect (! PresetManager::isValidParameterValue (params::layer1ScanMode, 3.0));
                expect (PresetManager::isValidParameterValue (params::layer2ScanMode, 2.0));
                expect (! PresetManager::isValidParameterValue (params::layer1ScanPosition, 1.2));
                expect (! PresetManager::isValidParameterValue (params::layer2ScanSpeed, 4.5));
                expect (! PresetManager::isValidParameterValue (params::layer1Freeze, 0.5));
                expect (! PresetManager::isValidParameterValue (params::layer2FreezeSpread, -0.1));

                for (const auto* id : { params::layer1ScanMode, params::layer1Freeze, params::layer2ScanPosition })
                {
                    const auto* field = LockPolicy::findField (id);
                    expect (field != nullptr && field->group == LockGroup::grain && ! field->mutableByDefault, id);
                }

                juce::ValueTree parameters ("Parameters");
                const auto missing = StateSchema::missingParameterIDs (parameters);
                expect (missing.contains (params::layer1Freeze) && missing.contains (params::layer2ScanSpeed),
                        "older states default the freeze and scan controls");
            }
        }
    };

    static ScanPositionTests scanPositionTests;
}
