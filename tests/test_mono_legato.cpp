#include "TestHelpers.h"

#include "../Source/Engine/GranularEngine.h"
#include "../Source/Engine/MonoNoteStack.h"
#include "../Source/Modulation/EnvelopeADSR.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace texture::tests
{
    namespace
    {
        constexpr double monoRate = 48000.0;
        constexpr int monoBlock = 512;

        MonoConfig config (VoiceMode mode, NotePriority priority = NotePriority::last,
                           float glideMs = 0.0f, GlideMode glideMode = GlideMode::overlapOnly)
        {
            MonoConfig result;
            result.voiceMode = mode;
            result.priority = priority;
            result.glideMs = glideMs;
            result.glideMode = glideMode;
            return result;
        }

        /** A fast envelope so notes sound at once and release within a few blocks. */
        void setFastEnvelope (GranularEngine& engine)
        {
            engine.setAdsrParameters (1.0f, 1.0f, 1.0f, 50.0f);
        }

        void renderBlocks (GranularEngine& engine, const SampleBufferPtr& sample,
                           const MorphTargets& targets, int blocks, int blockSamples = monoBlock)
        {
            juce::AudioBuffer<float> buffer (2, blockSamples);

            for (int i = 0; i < blocks; ++i)
            {
                buffer.clear();
                engine.renderNextBlock (sample, targets, buffer, 0, blockSamples);
            }
        }

        /** Renders long enough for every released voice to finish its fast release. */
        void renderRelease (GranularEngine& engine, const SampleBufferPtr& sample, const MorphTargets& targets)
        {
            renderBlocks (engine, sample, targets, 40);
        }

        /** Expected line key after a release under Last priority: newest remaining key. */
        int expectedLastKey (const std::vector<int>& pressOrder)
        {
            return pressOrder.empty() ? -1 : pressOrder.back();
        }
    }

    class MonoLegatoTests : public juce::UnitTest
    {
    public:
        MonoLegatoTests()
            : juce::UnitTest ("Mono legato and glide", "texture") {}

        void runTest() override
        {
            beginTest ("the defaults are Poly with no glide, Last priority and Overlap-only glide");
            {
                const MonoConfig defaults;
                expect (defaults.voiceMode == VoiceMode::poly);
                expect (defaults.priority == NotePriority::last);
                expectEquals (defaults.glideMs, 0.0f);
                expect (defaults.glideMode == GlideMode::overlapOnly);

                expectEquals (static_cast<int> (params::defaultValueFor (params::voiceMode)), 0);
                expectEquals (static_cast<int> (params::defaultValueFor (params::notePriority)), 0);
                expectEquals (params::defaultValueFor (params::glideMs), 0.0f);
                expectEquals (static_cast<int> (params::defaultValueFor (params::glideMode)), 1);
            }

            beginTest ("raw parameter values map to a valid configuration and fall back on garbage");
            {
                const auto legato = monoConfigFromParameters (2, 2, 150.0f, 0);
                expect (legato.voiceMode == VoiceMode::monoLegato);
                expect (legato.priority == NotePriority::low);
                expect (legato.glideMode == GlideMode::always);
                expectEquals (legato.glideMs, 150.0f);

                const auto garbage = monoConfigFromParameters (9, -4, std::nanf (""), 7);
                expect (garbage.voiceMode == VoiceMode::poly);
                expect (garbage.priority == NotePriority::last);
                expectEquals (garbage.glideMs, 0.0f);
                expect (garbage.glideMode == GlideMode::overlapOnly);

                expectEquals (monoConfigFromParameters (0, 0, 9000.0f, 1).glideMs, 2000.0f);
            }

            beginTest ("glide length is round(ms * rate / 1000) with zero meaning immediate");
            {
                expectEquals (glideSamplesFor (100.0f, 48000.0), 4800);
                expectEquals (glideSamplesFor (1.0f, 48000.0), 48);
                expectEquals (glideSamplesFor (0.0f, 48000.0), 0);
                expectEquals (glideSamplesFor (-5.0f, 48000.0), 0);
                expectEquals (glideSamplesFor (std::nanf (""), 48000.0), 0);
                expectEquals (glideSamplesFor (2000.0f, 44100.0), 88200);
                expectEquals (glideSamplesFor (10.0f, 44100.0), 441);
            }

            beginTest ("the held-key stack ignores releases with nothing held and never underflows");
            {
                MonoNoteStack stack;
                expect (! stack.release (1, 60));
                expect (stack.press (1, 60, 0.8f, 1));
                expect (stack.release (1, 60));
                expect (! stack.release (1, 60));
                expect (stack.empty());
            }

            beginTest ("a repeated press counts up and needs as many releases");
            {
                MonoNoteStack stack;
                for (int i = 0; i < 3; ++i)
                    expect (stack.press (1, 60, 0.5f, static_cast<std::uint64_t> (i + 1)));

                expectEquals (stack.find (1, 60)->heldCount, 3);
                expect (stack.release (1, 60));
                expect (stack.release (1, 60));
                expect (stack.isHeld (1, 60));
                expect (stack.release (1, 60));
                expect (! stack.isHeld (1, 60));
                expect (stack.empty());
            }

            beginTest ("the held count saturates instead of overflowing");
            {
                MonoNoteStack stack;
                for (int i = 0; i < MonoNoteStack::maxHeldCount + 40; ++i)
                    stack.press (1, 60, 0.5f, 1);

                expectEquals (stack.find (1, 60)->heldCount, MonoNoteStack::maxHeldCount);
            }

            beginTest ("channel is part of a key's identity");
            {
                MonoNoteStack stack;
                stack.press (1, 60, 0.5f, 1);
                stack.press (2, 60, 0.5f, 2);

                expect (stack.release (2, 60));
                expect (stack.isHeld (1, 60), "channel 2's note-off released channel 1's key");
                expect (! stack.isHeld (2, 60));
            }

            beginTest ("priority chooses last, high or low among eligible keys, with ties to the newest");
            {
                MonoNoteStack stack;
                stack.press (1, 60, 0.5f, 1);
                stack.press (1, 67, 0.5f, 2);
                stack.press (1, 64, 0.5f, 3);

                expectEquals (stack.selected (NotePriority::last)->number, 64);
                expectEquals (stack.selected (NotePriority::high)->number, 67);
                expectEquals (stack.selected (NotePriority::low)->number, 60);

                stack.press (2, 60, 0.5f, 4);
                expectEquals (stack.selected (NotePriority::last)->channel, 2);
                expectEquals (stack.selected (NotePriority::low)->channel, 2);
            }

            beginTest ("with sustain down a released key stays eligible until the pedal rises");
            {
                MonoNoteStack stack;
                stack.setPedal (1, true);
                stack.press (1, 60, 0.5f, 1);
                stack.release (1, 60);
                expect (! stack.isHeld (1, 60));
                expect (stack.selected (NotePriority::last) != nullptr, "sustained key should still be eligible");

                stack.setPedal (1, false);
                expect (stack.selected (NotePriority::last) == nullptr);
                expect (stack.empty());
            }

            beginTest ("the stack keeps a full capacity and evicts the oldest released key first");
            {
                MonoNoteStack stack;
                stack.press (1, 0, 0.5f, 1);
                stack.release (1, 0);
                for (int n = 1; n < MonoNoteStack::capacity; ++n)
                    expect (stack.press (1, n, 0.5f, static_cast<std::uint64_t> (n + 1)));

                expect (stack.press (1, 100, 0.5f, 100));
                expectEquals (stack.size(), MonoNoteStack::capacity);
                expect (stack.find (1, 0) == nullptr, "the released key should have been evicted");
                expect (stack.find (1, 100) != nullptr);
            }

            beginTest ("remove-channel and clear leave nothing behind");
            {
                MonoNoteStack stack;
                stack.press (1, 60, 0.5f, 1);
                stack.press (2, 64, 0.5f, 2);
                stack.removeChannel (1);
                expect (stack.find (1, 60) == nullptr);
                expect (stack.find (2, 64) != nullptr);
                stack.clear();
                expect (stack.empty());
            }

            beginTest ("legato overlap retargets, legato after silence restarts, retrigger always restarts");
            {
                MonoLineContext context;
                context.hasSelection = true;
                context.pressEvent = true;
                context.previousHeld = true;

                context.mode = VoiceMode::monoLegato;
                context.hadSelection = true;
                expect (decideMonoLine (context).action == MonoLineDecision::Action::retarget);

                context.hadSelection = false;
                expect (decideMonoLine (context).action == MonoLineDecision::Action::restart);

                context.mode = VoiceMode::monoRetrigger;
                context.hadSelection = true;
                expect (decideMonoLine (context).action == MonoLineDecision::Action::restart);
            }

            beginTest ("glide rules: Always glides every transition, Overlap only glides held overlaps");
            {
                MonoLineContext context;
                context.mode = VoiceMode::monoLegato;
                context.hasSelection = true;
                context.hadSelection = true;
                context.pressEvent = true;

                context.glideMode = GlideMode::overlapOnly;
                context.previousHeld = true;
                expect (decideMonoLine (context).glide);
                context.previousHeld = false;
                expect (! decideMonoLine (context).glide, "a press after release is not an overlap");

                context.pressEvent = false;
                context.previousHeld = true;
                expect (! decideMonoLine (context).glide, "a fallback to a held key is not an overlap");

                context.glideMode = GlideMode::always;
                expect (decideMonoLine (context).glide);

                context.hadSelection = false;
                context.hasLastPitch = true;
                expect (decideMonoLine (context).glide, "Always glides from the last pitch after silence");
                context.hasLastPitch = false;
                expect (! decideMonoLine (context).glide);
            }

            beginTest ("the line releases when nothing is eligible and does nothing from silence");
            {
                MonoLineContext context;
                context.mode = VoiceMode::monoLegato;
                context.hasSelection = false;
                context.hadSelection = true;
                expect (decideMonoLine (context).action == MonoLineDecision::Action::release);
                context.hadSelection = false;
                expect (decideMonoLine (context).action == MonoLineDecision::Action::none);
            }

            beginTest ("poly mode ignores the mono machinery and keeps one voice per note");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::poly));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.8f);
                engine.noteOn (1, 64, 0.8f);
                renderBlocks (engine, sample, targets, 1);
                expectEquals (engine.getActiveVoiceCount(), 2);
                expectEquals (engine.getMonoLineNote(), -1);
                engine.noteOff (1, 60);
                renderRelease (engine, sample, targets);
                engine.noteOff (1, 64);
                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 0);
            }

            beginTest ("mono legato keeps one voice for overlapping notes and moves its pitch");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.8f);
                renderBlocks (engine, sample, targets, 4);
                const auto voice = engine.getMonoLineVoiceIndex();

                engine.noteOn (1, 64, 0.8f);
                expectEquals (engine.getMonoLineVoiceIndex(), voice);
                expectEquals (engine.getMonoLineNote(), 64);
                expectEquals (engine.getActiveVoiceCount(), 1);
            }

            beginTest ("mono retrigger keeps one slot and reports the new note");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoRetrigger));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.6f);
                renderBlocks (engine, sample, targets, 4);
                engine.noteOn (1, 67, 0.9f);
                expectEquals (engine.getMonoLineNote(), 67);
                expectEquals (engine.getActiveVoiceCount(), 1);
                expectWithinAbsoluteError (engine.getMonoLineVelocity(), 0.9f, 1.0e-6f);
            }

            beginTest ("all release orders of three keys leave the newest remaining key sounding");
            {
                const std::array<int, 3> keys { 60, 64, 67 };
                std::array<int, 3> order { 0, 1, 2 };

                do
                {
                    GranularEngine engine;
                    engine.prepare (monoRate, monoBlock);
                    setFastEnvelope (engine);
                    engine.setVoiceConfig (config (VoiceMode::monoLegato));
                    const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                    const auto targets = deriveMorphTargets (0.35f);

                    std::vector<int> pressed;
                    for (const auto key : keys)
                    {
                        engine.noteOn (1, key, 0.8f);
                        pressed.push_back (key);
                    }
                    expectEquals (engine.getMonoLineNote(), 67);

                    for (const auto index : order)
                    {
                        const auto key = keys[static_cast<size_t> (index)];
                        engine.noteOff (1, key);
                        pressed.erase (std::remove (pressed.begin(), pressed.end(), key), pressed.end());
                        expectEquals (engine.getMonoLineNote(), expectedLastKey (pressed));
                        renderBlocks (engine, sample, targets, 1);
                    }

                    renderRelease (engine, sample, targets);
                    expectEquals (engine.getActiveVoiceCount(), 0);
                    expect (engine.getMonoNoteStack().empty());
                } while (std::next_permutation (order.begin(), order.end()));
            }

            beginTest ("high and low priority pick the extreme key and fall back in order");
            {
                for (const auto priority : { NotePriority::high, NotePriority::low })
                {
                    GranularEngine engine;
                    engine.prepare (monoRate, monoBlock);
                    setFastEnvelope (engine);
                    engine.setVoiceConfig (config (VoiceMode::monoLegato, priority));
                    const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                    const auto targets = deriveMorphTargets (0.35f);

                    engine.noteOn (1, 64, 0.8f);
                    engine.noteOn (1, 60, 0.8f);
                    engine.noteOn (1, 67, 0.8f);

                    const auto first = priority == NotePriority::high ? 67 : 60;
                    const auto second = priority == NotePriority::high ? 64 : 64;
                    const auto third = priority == NotePriority::high ? 60 : 67;
                    expectEquals (engine.getMonoLineNote(), first);
                    engine.noteOff (1, first);
                    renderBlocks (engine, sample, targets, 1);
                    expectEquals (engine.getMonoLineNote(), second);
                    engine.noteOff (1, second);
                    renderBlocks (engine, sample, targets, 1);
                    expectEquals (engine.getMonoLineNote(), third);
                    engine.noteOff (1, third);
                    renderRelease (engine, sample, targets);
                    expectEquals (engine.getActiveVoiceCount(), 0);
                }
            }

            beginTest ("a repeated key needs as many note-offs before it releases");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.8f);
                engine.noteOn (1, 60, 0.8f);
                engine.noteOff (1, 60);
                renderBlocks (engine, sample, targets, 2);
                expectEquals (engine.getMonoLineNote(), 60);

                engine.noteOff (1, 60);
                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 0);

                // An extra note-off must not underflow into a phantom held key.
                engine.noteOff (1, 60);
                engine.noteOn (1, 64, 0.8f);
                engine.noteOff (1, 64);
                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 0);
            }

            beginTest ("a note-off on another channel does not release this channel's key");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.8f);
                engine.noteOn (2, 60, 0.8f);
                engine.noteOff (2, 60);
                renderBlocks (engine, sample, targets, 2);
                expectEquals (engine.getActiveVoiceCount(), 1);
                expect (engine.getMonoNoteStack().isHeld (1, 60));

                engine.noteOff (1, 60);
                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 0);
            }

            beginTest ("zero-velocity note-on releases the key and never starts a note");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.8f);
                engine.noteOn (1, 60, 0.0f);
                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 0);
                expect (engine.getMonoNoteStack().empty());
            }

            beginTest ("sustain holds the line through released keys until the pedal rises");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.setSustainPedal (1, true);
                engine.noteOn (1, 60, 0.8f);
                engine.noteOn (1, 64, 0.8f);
                engine.noteOff (1, 64);
                renderBlocks (engine, sample, targets, 2);
                expectEquals (engine.getMonoLineNote(), 64);

                engine.noteOff (1, 60);
                renderBlocks (engine, sample, targets, 2);
                expectEquals (engine.getMonoLineNote(), 64);
                expectEquals (engine.getActiveVoiceCount(), 1);

                engine.setSustainPedal (1, false);
                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 0);
                expect (engine.getMonoNoteStack().empty());
            }

            beginTest ("zero glide jumps at once and keeps the same voice");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato, NotePriority::last, 0.0f));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.8f);
                renderBlocks (engine, sample, targets, 2);
                const auto voice = engine.getMonoLineVoiceIndex();
                engine.noteOn (1, 67, 0.8f);
                expectWithinAbsoluteError (engine.getMonoLinePitch(), 67.0f, 1.0e-5f);
                expectEquals (engine.getMonoLineVoiceIndex(), voice);
            }

            beginTest ("a 100 ms glide moves linearly in semitones and lands exactly on the target");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato, NotePriority::last, 100.0f, GlideMode::always));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.8f);
                renderBlocks (engine, sample, targets, 2);
                expectWithinAbsoluteError (engine.getMonoLinePitch(), 60.0f, 1.0e-4f);

                engine.noteOn (1, 67, 0.8f);
                expectWithinAbsoluteError (engine.getMonoLinePitch(), 60.0f, 1.0e-3f, "the glide starts where the line is");

                // 2400 samples is half of the 4800-sample glide.
                renderBlocks (engine, sample, targets, 4, 512);
                renderBlocks (engine, sample, targets, 1, 352);
                expectWithinAbsoluteError (engine.getMonoLinePitch(), 63.5f, 0.05f);

                // The full glide is 4800 samples, so the target is exact after the other 2400.
                renderBlocks (engine, sample, targets, 5, 480);
                expectWithinAbsoluteError (engine.getMonoLinePitch(), 67.0f, 1.0e-4f);
            }

            beginTest ("glide and retrigger keep the pitch of the grains in step with the line");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoRetrigger, NotePriority::last, 60.0f, GlideMode::overlapOnly));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.0f);

                engine.noteOn (1, 60, 0.8f);
                renderBlocks (engine, sample, targets, 3);
                // A 60 ms glide is 2880 samples. After 1024 samples the pitch is still on its way:
                // 72 - 12 * (2880 - 1024) / 2880 = 64.27.
                engine.noteOn (1, 72, 0.8f);
                renderBlocks (engine, sample, targets, 2);
                expectWithinAbsoluteError (engine.getMonoLinePitch(), 64.27f, 0.05f);

                renderBlocks (engine, sample, targets, 4);
                expectWithinAbsoluteError (engine.getMonoLinePitch(), 72.0f, 1.0e-3f);

                // The output stays finite and audible through the glide.
                juce::AudioBuffer<float> buffer (2, monoBlock);
                buffer.clear();
                engine.renderNextBlock (sample, targets, buffer, 0, monoBlock);
                expect (peakOf (buffer) > 0.0f);
            }

            beginTest ("the velocity of a retargeted line ramps to the new key and a fallback restores its own");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.4f);
                engine.noteOn (1, 67, 0.9f);
                expectWithinAbsoluteError (engine.getMonoLineVelocity(), 0.9f, 1.0e-6f);

                engine.noteOff (1, 67);
                renderBlocks (engine, sample, targets, 1);
                expectEquals (engine.getMonoLineNote(), 60);
                expectWithinAbsoluteError (engine.getMonoLineVelocity(), 0.4f, 1.0e-6f,
                                           "a fallback note sounds at the velocity it was played with");
            }

            beginTest ("switching poly to mono adopts the sounding key's voice instead of striking it again");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.8f);
                engine.noteOn (1, 64, 0.8f);
                renderBlocks (engine, sample, targets, 2);
                expectEquals (engine.getActiveVoiceCount(), 2);

                engine.setVoiceConfig (config (VoiceMode::monoLegato));
                expectEquals (engine.getMonoLineNote(), 64);
                expectEquals (engine.getMonoLineVoiceIndex(), 1);
                renderBlocks (engine, sample, targets, 4);
                expectEquals (engine.getMonoLineVoiceIndex(), 1);
                expectEquals (engine.getActiveVoiceCount(), 1, "the other key's voice should have faded out");

                engine.noteOff (1, 64);
                engine.noteOff (1, 60);
                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 0);
            }

            beginTest ("switching mono to poly keeps the line and starts only the other held keys");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.8f);
                engine.noteOn (1, 64, 0.8f);
                const auto line = engine.getMonoLineVoiceIndex();
                renderBlocks (engine, sample, targets, 2);

                engine.setVoiceConfig (config (VoiceMode::poly));
                renderBlocks (engine, sample, targets, 1);
                expectEquals (engine.getActiveVoiceCount(), 2);

                engine.noteOff (1, 60);
                engine.noteOff (1, 64);
                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 0);
                expect (line >= 0);
            }

            beginTest ("panic clears the stack, the line and the glide memory");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato, NotePriority::last, 200.0f, GlideMode::always));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 60, 0.8f);
                engine.noteOn (1, 64, 0.8f);
                engine.allNotesOff();
                expectEquals (engine.getActiveVoiceCount(), 0);
                expect (engine.getMonoNoteStack().empty());
                expectEquals (engine.getMonoLineNote(), -1);

                engine.noteOn (1, 67, 0.8f);
                expectWithinAbsoluteError (engine.getMonoLinePitch(), 67.0f, 1.0e-4f,
                                           "after panic the next note must not glide from the old key");
                renderBlocks (engine, sample, targets, 1);
            }

            beginTest ("audition notes keep the polyphonic pool and are not released by the line");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                engine.setVoiceConfig (config (VoiceMode::monoLegato));
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);

                engine.noteOn (1, 48, 0.8f, true);
                engine.noteOn (1, 60, 0.8f);
                renderBlocks (engine, sample, targets, 2);
                expectEquals (engine.getActiveVoiceCount(), 2);

                engine.noteOff (1, 60);
                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 1, "audition should still be sounding");

                engine.noteOff (1, 48, true);
                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 0);
            }

            beginTest ("a seeded stream of mixed events never leaves a stuck note");
            {
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                setFastEnvelope (engine);
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                const auto targets = deriveMorphTargets (0.35f);
                juce::Random random (424242);

                // Driver-side counts: what the host believes is held, per channel and key.
                std::array<std::array<int, 128>, 3> held {};
                std::array<int, 3> pedals { 0, 0, 0 };

                for (int step = 0; step < 4000; ++step)
                {
                    const auto roll = random.nextInt (100);
                    const auto channel = 1 + random.nextInt (2);
                    const auto key = 48 + random.nextInt (16);

                    if (roll < 40)
                    {
                        const auto velocity = random.nextInt (8) == 0 ? 0.0f : 0.2f + 0.7f * random.nextFloat();

                        if (velocity <= 0.0f)
                        {
                            // A zero-velocity note-on is a note-off to the host, so the count drops.
                            if (held[static_cast<size_t> (channel)][static_cast<size_t> (key)] > 0)
                                --held[static_cast<size_t> (channel)][static_cast<size_t> (key)];
                        }
                        else
                        {
                            ++held[static_cast<size_t> (channel)][static_cast<size_t> (key)];
                        }

                        engine.noteOn (channel, key, velocity);
                    }
                    else if (roll < 75)
                    {
                        if (held[static_cast<size_t> (channel)][static_cast<size_t> (key)] > 0)
                            --held[static_cast<size_t> (channel)][static_cast<size_t> (key)];

                        engine.noteOff (channel, key);
                    }
                    else if (roll < 85)
                    {
                        const auto down = random.nextBool();
                        pedals[static_cast<size_t> (channel)] = down ? 1 : 0;
                        engine.setSustainPedal (channel, down);
                    }
                    else if (roll < 93)
                    {
                        const auto modes = std::array<VoiceMode, 3> { VoiceMode::poly, VoiceMode::monoRetrigger,
                                                                      VoiceMode::monoLegato };
                        const auto priorities = std::array<NotePriority, 3> { NotePriority::last, NotePriority::high,
                                                                              NotePriority::low };
                        engine.setVoiceConfig (config (modes[static_cast<size_t> (random.nextInt (3))],
                                                       priorities[static_cast<size_t> (random.nextInt (3))],
                                                       static_cast<float> (random.nextInt (120)),
                                                       random.nextBool() ? GlideMode::always : GlideMode::overlapOnly));
                    }

                    renderBlocks (engine, sample, targets, 1, 256);
                }

                // Release everything the host still holds, lift both pedals, and let the tails end.
                for (int channel = 1; channel <= 2; ++channel)
                {
                    engine.setSustainPedal (channel, false);

                    for (int key = 48; key < 64; ++key)
                        for (int i = 0; i < held[static_cast<size_t> (channel)][static_cast<size_t> (key)]; ++i)
                            engine.noteOff (channel, key);
                }

                renderRelease (engine, sample, targets);
                expectEquals (engine.getActiveVoiceCount(), 0, "a note was left sounding after every key was released");
                expect (engine.getMonoNoteStack().empty());
            }

            beginTest ("the poly default renders the same voices as before the mono machinery existed");
            {
                // The poly path must not allocate a line voice or touch the stack.
                GranularEngine engine;
                engine.prepare (monoRate, monoBlock);
                engine.setVoiceConfig (MonoConfig {});
                const auto sample = makeTestSample (monoRate, 2.0, 220.0);
                engine.noteOn (1, 60, 0.8f);
                renderBlocks (engine, sample, deriveMorphTargets (0.35f), 2);
                expectEquals (engine.getMonoLineVoiceIndex(), -1);
                expect (engine.getMonoNoteStack().empty());
            }
        }
    };

    static MonoLegatoTests monoLegatoTests;
}
