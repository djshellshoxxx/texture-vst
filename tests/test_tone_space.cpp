#include "TestHelpers.h"

#include "../Source/Effects/Biquad.h"
#include "../Source/Effects/EffectsChain.h"
#include "../Source/Effects/ToneSpace.h"
#include "../Source/Presets/LockPolicy.h"
#include "../Source/Presets/PresetManager.h"
#include "../Source/Utility/ParameterIDs.h"
#include "../Source/Utility/StateSchema.h"

#include <cmath>
#include <limits>
#include <vector>

namespace texture::tests
{
    namespace
    {
        constexpr double toneRate = 48000.0;

        float maxAbsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
        {
            float worst = 0.0f;
            for (int ch = 0; ch < a.getNumChannels(); ++ch)
                for (int i = 0; i < a.getNumSamples(); ++i)
                    worst = juce::jmax (worst, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
            return worst;
        }

        bool allFinite (const juce::AudioBuffer<float>& buffer)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    if (! std::isfinite (buffer.getSample (ch, i)))
                        return false;
            return true;
        }

        /** RMS of a sine at `frequency` after running it through one biquad. */
        double filteredSineGainDb (const dsp::BiquadCoefficients& coefficients, double frequency)
        {
            dsp::Biquad filter;
            filter.setCoefficients (coefficients);

            const int settle = 4800;
            const int measure = 9600;
            double sumIn = 0.0;
            double sumOut = 0.0;

            for (int i = 0; i < settle + measure; ++i)
            {
                const auto x = static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                             * frequency * i / toneRate));
                const auto y = filter.processSample (x);

                if (i >= settle)
                {
                    sumIn += static_cast<double> (x) * x;
                    sumOut += static_cast<double> (y) * y;
                }
            }

            return 10.0 * std::log10 (sumOut / juce::jmax (1.0e-30, sumIn));
        }

        /** Deterministic stereo noise at a fixed level. */
        juce::AudioBuffer<float> makeNoise (int numChannels, int numSamples, juce::int64 seed, float level)
        {
            juce::AudioBuffer<float> buffer (numChannels, numSamples);
            juce::Random random (seed);

            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    buffer.setSample (ch, i, level * (random.nextFloat() * 2.0f - 1.0f));

            return buffer;
        }

        /** Renders the same input through a chain in fixed-size blocks. */
        juce::AudioBuffer<float> renderThrough (EffectsChain& chain, const juce::AudioBuffer<float>& input,
                                                int blockSize)
        {
            juce::AudioBuffer<float> output (input.getNumChannels(), input.getNumSamples());
            output.makeCopyOf (input);

            for (int start = 0; start < input.getNumSamples(); start += blockSize)
            {
                const auto length = juce::jmin (blockSize, input.getNumSamples() - start);
                juce::AudioBuffer<float> block (output.getArrayOfWritePointers(), output.getNumChannels(),
                                                start, length);
                chain.process (block, 0, length);
            }

            return output;
        }

        /** The Independent values that reproduce Follow MORPH at `morph` exactly. */
        ToneSpaceSettings parityWithFollow (float morph)
        {
            const auto targets = deriveMorphTargets (morph);

            ToneSpaceSettings settings;
            settings.independent = true;
            settings.drive = (targets.saturatorDrive - 0.1f) / 0.25f;
            settings.chorusAmount = targets.chorusDepth / 0.6f;
            settings.reverbSend = targets.reverbSend;
            settings.tiltDb = targets.highShelfTiltDb;
            settings.width = 1.0f;
            settings.filterOn = false;
            return settings;
        }
    }

    class ToneSpaceTests : public juce::UnitTest
    {
    public:
        ToneSpaceTests()
            : juce::UnitTest ("Tone and space", "texture") {}

        void runTest() override
        {
            beginTest ("new controls are registered, default to Follow MORPH and are lockable");
            {
                for (const auto* id : { params::toneSpaceMode, params::toneHpHz, params::toneLpHz,
                                        params::toneFilterOn, params::toneTiltDb, params::spaceWidth,
                                        params::spaceReverb, params::spaceChorus, params::toneDrive })
                {
                    expect (params::isToneSpaceParameter (id), id);
                    const auto* field = LockPolicy::findField (id);
                    expect (field != nullptr && field->group == LockGroup::toneSpace, id);
                }

                expectEquals (params::defaultValueFor (params::toneSpaceMode), 0.0f);
                expectEquals (params::defaultValueFor (params::toneFilterOn), 0.0f);
                expectEquals (params::defaultValueFor (params::toneHpHz), 20.0f);
                expectEquals (params::defaultValueFor (params::toneLpHz), 20000.0f);
                expectEquals (params::defaultValueFor (params::spaceWidth), 1.0f);
                expectWithinAbsoluteError (params::defaultValueFor (params::spaceReverb), 0.3f, 1.0e-6f);
                expectWithinAbsoluteError (params::defaultValueFor (params::spaceChorus), 0.2f, 1.0e-6f);
                expectWithinAbsoluteError (params::defaultValueFor (params::toneDrive), 0.1f, 1.0e-6f);
                expectEquals (params::defaultValueFor (params::toneTiltDb), 0.0f);
            }

            beginTest ("the default settings resolve to exactly the MORPH-derived values");
            {
                for (const float morph : { 0.0f, 0.2f, 0.35f, 0.7f, 1.0f })
                {
                    const auto targets = deriveMorphTargets (morph);
                    const auto resolved = ToneSpaceRouter::resolve (targets, ToneSpaceSettings {});

                    expectEquals (resolved.saturatorDrive, targets.saturatorDrive);
                    expectEquals (resolved.chorusDepth, targets.chorusDepth);
                    expectEquals (resolved.reverbSend, targets.reverbSend);
                    expectEquals (resolved.highShelfTiltDb, targets.highShelfTiltDb);
                    expectEquals (resolved.filterMix, 0.0f);
                    expectEquals (resolved.width, 1.0f);
                }
            }

            beginTest ("Follow MORPH ignores the independent values, even while they move");
            {
                ToneSpaceRouter router;
                router.prepare (toneRate);
                const auto morph = deriveMorphTargets (0.35f);
                router.reset (morph);

                ToneSpaceSettings settings;
                settings.independent = false;
                settings.reverbSend = 1.0f;
                settings.tiltDb = -12.0f;
                settings.width = 0.0f;
                settings.filterOn = true;

                for (int block = 0; block < 20; ++block)
                {
                    const auto resolved = router.advance (morph, settings, 128);
                    expectEquals (resolved.reverbSend, morph.reverbSend);
                    expectEquals (resolved.highShelfTiltDb, morph.highShelfTiltDb);
                    expectEquals (resolved.width, 1.0f);
                    expectEquals (resolved.filterMix, 0.0f);
                }
            }

            beginTest ("Independent mode maps each control onto the documented target range");
            {
                ToneSpaceSettings settings;
                settings.independent = true;
                settings.drive = 1.0f;
                settings.chorusAmount = 1.0f;
                settings.reverbSend = 0.75f;
                settings.tiltDb = -6.0f;
                settings.width = 2.0f;
                settings.filterOn = true;
                settings.hpHz = 300.0f;
                settings.lpHz = 4000.0f;

                const auto resolved = ToneSpaceRouter::resolve (deriveMorphTargets (0.0f), settings);
                expectWithinAbsoluteError (resolved.saturatorDrive, 0.35f, 1.0e-6f);
                expectWithinAbsoluteError (resolved.chorusDepth, 0.6f, 1.0e-6f);
                expectWithinAbsoluteError (resolved.reverbSend, 0.75f, 1.0e-6f);
                expectWithinAbsoluteError (resolved.highShelfTiltDb, -6.0f, 1.0e-6f);
                expectEquals (resolved.filterMix, 1.0f);
                expectWithinAbsoluteError (resolved.hpHz, 300.0f, 1.0e-3f);
                expectWithinAbsoluteError (resolved.lpHz, 4000.0f, 1.0e-3f);
                expectEquals (resolved.width, 2.0f);
            }

            beginTest ("non-finite and out-of-range settings fall back or clamp, never extremes");
            {
                ToneSpaceSettings settings;
                settings.independent = true;
                settings.hpHz = std::numeric_limits<float>::quiet_NaN();
                settings.lpHz = 1.0e9f;
                settings.tiltDb = std::numeric_limits<float>::infinity();
                settings.width = -4.0f;
                settings.reverbSend = 7.0f;
                settings.chorusAmount = -1.0f;
                settings.drive = std::numeric_limits<float>::quiet_NaN();

                const auto clean = sanitiseToneSpace (settings);
                expectEquals (clean.hpHz, params::toneHpDefaultHz);
                expectEquals (clean.lpHz, params::toneLpMaxHz);
                expectEquals (clean.tiltDb, params::toneTiltDefaultDb);
                expectEquals (clean.width, params::spaceWidthMin);
                expectEquals (clean.reverbSend, params::spaceReverbMax);
                expectEquals (clean.chorusAmount, params::spaceChorusMin);
                expectEquals (clean.drive, params::toneDriveDefault);
            }

            beginTest ("a mode change ramps over 20 ms, then lands exactly on the goal");
            {
                ToneSpaceRouter router;
                router.prepare (toneRate);
                const auto morph = deriveMorphTargets (0.35f);
                router.reset (morph);

                ToneSpaceSettings independent;
                independent.independent = true;
                independent.reverbSend = 0.0f;

                const auto start = router.advance (morph, independent, 0);
                expectWithinAbsoluteError (start.reverbSend, morph.reverbSend, 1.0e-6f);

                const int transitionSamples = static_cast<int> (ToneSpaceRouter::transitionSeconds * toneRate);
                const auto half = router.advance (morph, independent, transitionSamples / 2);
                expect (router.isTransitioning());
                expectWithinAbsoluteError (half.reverbSend, 0.5f * (morph.reverbSend + 0.0f), 0.02f);

                const auto done = router.advance (morph, independent, transitionSamples);
                expect (! router.isTransitioning());
                expectEquals (done.reverbSend, 0.0f);
            }

            beginTest ("returning to Follow MORPH restores the legacy values exactly");
            {
                ToneSpaceRouter router;
                router.prepare (toneRate);
                const auto morph = deriveMorphTargets (0.6f);
                router.reset (morph);

                ToneSpaceSettings independent = parityWithFollow (0.2f);
                for (int block = 0; block < 10; ++block)
                    router.advance (morph, independent, 256);

                ToneSpaceSettings follow;
                for (int block = 0; block < 10; ++block)
                {
                    const auto resolved = router.advance (morph, follow, 256);
                    if (! router.isTransitioning())
                    {
                        expect (juce::exactlyEqual (resolved.saturatorDrive, morph.saturatorDrive));
                        expect (juce::exactlyEqual (resolved.chorusDepth, morph.chorusDepth));
                        expect (juce::exactlyEqual (resolved.reverbSend, morph.reverbSend));
                        expect (juce::exactlyEqual (resolved.highShelfTiltDb, morph.highShelfTiltDb));
                        expect (juce::exactlyEqual (resolved.filterMix, 0.0f));
                        expect (juce::exactlyEqual (resolved.width, 1.0f));
                    }
                }
            }

            beginTest ("a biquad low-pass is 12 dB per octave and passes low frequencies");
            {
                const auto lowPass = dsp::makeBiquad (dsp::BiquadShape::lowPass, toneRate, 1000.0, dsp::butterworthQ);
                // Butterworth second order: |H| = 1 / sqrt(1 + (f/fc)^4).
                // One octave above cutoff is -12.3 dB; a decade above is about -40 dB.
                expectWithinAbsoluteError (filteredSineGainDb (lowPass, 100.0), 0.0, 0.5);
                expectWithinAbsoluteError (filteredSineGainDb (lowPass, 2000.0), -12.3, 0.5);
                const auto decade = filteredSineGainDb (lowPass, 10000.0);
                // Bilinear warping steepens the digital response a little above the analog
                // -40 dB, so the window is ±2 dB around the analog value.
                expect (decade > -44.0 && decade < -38.0, "a decade above cutoff should sit near -40 dB, measured " + juce::String (decade, 2));
            }

            beginTest ("a biquad high-pass removes low frequencies");
            {
                const auto highPass = dsp::makeBiquad (dsp::BiquadShape::highPass, toneRate, 1000.0, dsp::butterworthQ);
                expect (filteredSineGainDb (highPass, 50.0) < -24.0);
                expectWithinAbsoluteError (filteredSineGainDb (highPass, 10000.0), 0.0, 0.5);
            }

            beginTest ("cutoffs are clamped below 0.45 of the sample rate and stay stable");
            {
                for (const double rate : { 8000.0, 22050.0, 44100.0, 96000.0, 192000.0 })
                {
                    for (const auto shape : { dsp::BiquadShape::lowPass, dsp::BiquadShape::highPass })
                    {
                        const auto c = dsp::makeBiquad (shape, rate, 1.0e6, dsp::butterworthQ);
                        expect (std::isfinite (c.b0) && std::isfinite (c.a1) && std::isfinite (c.a2));
                        // Stable second-order section: the poles lie inside the unit circle.
                        expect (std::abs (c.a2) < 1.0);
                        expect (std::abs (c.a1) < 1.0 + c.a2);
                    }
                }
            }

            beginTest ("filter, width and shelf stay finite at every extreme and sample rate");
            {
                for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
                {
                    for (const int blockSize : { 1, 7, 128, 512 })
                    {
                        EffectsChain chain;
                        chain.prepare (rate, 512, 2);
                        chain.setReverbEnabled (true);
                        chain.setOutputGainDb (0.0f);

                        ToneSpaceSettings extreme;
                        extreme.independent = true;
                        extreme.filterOn = true;
                        extreme.hpHz = params::toneHpMaxHz;
                        extreme.lpHz = params::toneLpMinHz;
                        extreme.tiltDb = params::toneTiltMaxDb;
                        extreme.width = params::spaceWidthMax;
                        extreme.reverbSend = 1.0f;
                        extreme.chorusAmount = 1.0f;
                        extreme.drive = 1.0f;

                        chain.setToneSpace (extreme);
                        chain.setMorphTargets (deriveMorphTargets (1.0f));

                        const auto input = makeNoise (2, 4096, 99, 1.0f);
                        const auto output = renderThrough (chain, input, blockSize);
                        expect (allFinite (output), juce::String ("non-finite output at ") + juce::String (rate)
                                                    + " Hz, block " + juce::String (blockSize));

                        float peak = 0.0f;
                        for (int ch = 0; ch < 2; ++ch)
                            peak = juce::jmax (peak, output.getMagnitude (ch, 0, output.getNumSamples()));
                        expect (peak < 50.0f, "extreme settings should not run away");

                        extreme.tiltDb = params::toneTiltMinDb;
                        extreme.width = params::spaceWidthMin;
                        chain.setToneSpace (extreme);
                        const auto second = renderThrough (chain, input, blockSize);
                        expect (allFinite (second));
                    }
                }
            }

            beginTest ("independent values equal to Follow MORPH reproduce Follow MORPH");
            {
                const auto input = makeNoise (2, 8192, 7, 0.25f);

                EffectsChain follow;
                follow.prepare (toneRate, 512, 2);
                follow.setReverbEnabled (true);
                follow.setOutputGainDb (0.0f);
                follow.setMorphTargets (deriveMorphTargets (0.35f));

                EffectsChain independent;
                independent.prepare (toneRate, 512, 2);
                independent.setReverbEnabled (true);
                independent.setOutputGainDb (0.0f);
                independent.setToneSpace (parityWithFollow (0.35f));
                independent.setMorphTargets (deriveMorphTargets (0.35f));

                // Let the mode transition finish before comparing, then compare the rest.
                const auto warmupIn = makeNoise (2, 2048, 3, 0.1f);
                renderThrough (follow, warmupIn, 512);
                renderThrough (independent, warmupIn, 512);

                const auto a = renderThrough (follow, input, 512);
                const auto b = renderThrough (independent, input, 512);
                expectWithinAbsoluteError (maxAbsDifference (a, b), 0.0f, 1.0e-4f);
            }

            beginTest ("width 0 sums to mono and width 1 leaves the stereo image alone");
            {
                const auto input = makeNoise (2, 4096, 11, 0.4f);

                EffectsChain mono;
                mono.prepare (toneRate, 512, 2);
                mono.setOutputGainDb (0.0f);
                ToneSpaceSettings zeroWidth;
                zeroWidth.independent = true;
                zeroWidth.width = 0.0f;
                zeroWidth.reverbSend = 0.0f;
                zeroWidth.chorusAmount = 0.0f;
                mono.setToneSpace (zeroWidth);
                mono.setMorphTargets (deriveMorphTargets (0.0f));
                const auto summed = renderThrough (mono, input, 512);

                // Let the width ramp settle, then compare channels.
                float maxSide = 0.0f;
                for (int i = 2048; i < summed.getNumSamples(); ++i)
                    maxSide = juce::jmax (maxSide, std::abs (summed.getSample (0, i) - summed.getSample (1, i)));
                expectWithinAbsoluteError (maxSide, 0.0f, 1.0e-4f);

                EffectsChain wide;
                wide.prepare (toneRate, 512, 2);
                wide.setOutputGainDb (0.0f);
                ToneSpaceSettings unity = zeroWidth;
                unity.width = 1.0f;
                wide.setToneSpace (unity);
                wide.setMorphTargets (deriveMorphTargets (0.0f));
                const auto stereo = renderThrough (wide, input, 512);
                float difference = 0.0f;
                for (int i = 0; i < stereo.getNumSamples(); ++i)
                    difference = juce::jmax (difference, std::abs (stereo.getSample (0, i) - stereo.getSample (1, i)));
                expect (difference > 1.0e-3f, "unity width should keep the stereo difference");
            }

            beginTest ("a mono bus is accepted and ignores the width control");
            {
                EffectsChain chain;
                chain.prepare (toneRate, 512, 1);
                chain.setOutputGainDb (0.0f);
                ToneSpaceSettings settings;
                settings.independent = true;
                settings.width = 0.0f;
                chain.setToneSpace (settings);
                chain.setMorphTargets (deriveMorphTargets (0.35f));

                const auto input = makeNoise (1, 2048, 5, 0.3f);
                const auto output = renderThrough (chain, input, 256);
                expect (allFinite (output));
            }

            beginTest ("a filter enabled and then disabled leaves no state behind");
            {
                EffectsChain chain;
                chain.prepare (toneRate, 512, 2);
                chain.setOutputGainDb (0.0f);
                ToneSpaceSettings on;
                on.independent = true;
                on.filterOn = true;
                on.lpHz = 1000.0f;
                chain.setToneSpace (on);
                chain.setMorphTargets (deriveMorphTargets (0.0f));
                renderThrough (chain, makeNoise (2, 4096, 2, 0.5f), 512);

                ToneSpaceSettings off = on;
                off.filterOn = false;
                chain.setToneSpace (off);

                // The mix ramp (20 ms one-pole) must reach exactly zero, so the bypass is exact.
                for (int pass = 0; pass < 4; ++pass)
                    renderThrough (chain, makeNoise (2, 4096, 2, 0.5f), 512);
                expect (juce::exactlyEqual (chain.getAppliedToneSpace().filterMix, 0.0f));
            }

            beginTest ("the independent filter darkens the output when the low-pass is closed");
            {
                const auto input = makeNoise (2, 8192, 21, 0.3f);

                EffectsChain open;
                open.prepare (toneRate, 512, 2);
                open.setOutputGainDb (0.0f);
                ToneSpaceSettings reference;
                reference.independent = true;
                reference.reverbSend = 0.0f;
                reference.chorusAmount = 0.0f;
                open.setToneSpace (reference);
                open.setMorphTargets (deriveMorphTargets (0.0f));
                const auto bright = renderThrough (open, input, 512);

                EffectsChain dark;
                dark.prepare (toneRate, 512, 2);
                dark.setOutputGainDb (0.0f);
                ToneSpaceSettings closed = reference;
                closed.filterOn = true;
                closed.lpHz = 1000.0f;
                dark.setToneSpace (closed);
                dark.setMorphTargets (deriveMorphTargets (0.0f));
                const auto muffled = renderThrough (dark, input, 512);

                const auto energy = [] (const juce::AudioBuffer<float>& buffer)
                {
                    double sum = 0.0;
                    for (int i = 4096; i < buffer.getNumSamples(); ++i)
                        sum += static_cast<double> (buffer.getSample (0, i)) * buffer.getSample (0, i);
                    return sum;
                };

                expect (energy (muffled) < 0.5 * energy (bright), "a 1 kHz low-pass should remove most of white noise energy");
            }

            beginTest ("tone and space parameters are validated in presets and reject out-of-range values");
            {
                TemporaryDirectory directory;
                PresetManager presets (nullptr);
                PresetManager::PresetData data;
                data.name = "Tone test";
                data.category = "Cinematic";
                data.samplePath = "";

                for (const auto* id : params::allIDs)
                    data.setParameter (id, params::defaultValueFor (id));

                data.setParameter (params::toneSpaceMode, 1.0f);
                data.setParameter (params::toneHpHz, 180.0f);
                data.setParameter (params::toneFilterOn, 1.0f);
                data.setParameter (params::spaceWidth, 1.5f);

                const auto file = directory.child ("tone.texture");
                juce::String error;
                expect (presets.save (file, data, error) == PresetManager::Status::ok, error);

                PresetManager::PresetData loaded;
                expect (presets.load (file, loaded, error) == PresetManager::Status::ok, error);
                expectWithinAbsoluteError (loaded.getParameter (params::toneHpHz, 0.0f), 180.0f, 1.0e-3f);
                expectEquals (loaded.getParameter (params::toneSpaceMode, -1.0f), 1.0f);

                // Out of range for the documented ranges: the whole preset is refused.
                const auto bad = directory.child ("bad.texture");
                auto broken = data;
                broken.setParameter (params::spaceWidth, 3.0f);
                expect (presets.save (bad, broken, error) != PresetManager::Status::ok);
            }

            beginTest ("a session written before F07 restores every new control to its default");
            {
                // A state that carries only the original parameters, as older builds wrote it.
                juce::ValueTree parameters ("PARAMETERS");

                for (const auto* id : params::allIDs)
                {
                    const auto isNew = params::isToneSpaceParameter (id);
                    if (isNew)
                        continue;

                    juce::ValueTree param ("PARAM");
                    param.setProperty ("id", juce::String (id), nullptr);
                    param.setProperty ("value", params::defaultValueFor (id), nullptr);
                    parameters.appendChild (param, nullptr);
                }

                const auto missing = StateSchema::missingParameterIDs (parameters);
                expectEquals (missing.size(), 9);

                for (const auto* id : { params::toneSpaceMode, params::spaceWidth, params::toneDrive })
                    expect (missing.contains (id), id);

                expect (! missing.contains (params::morph));

                // A complete state reports nothing missing.
                juce::ValueTree complete = parameters.createCopy();
                for (const auto* id : params::allIDs)
                {
                    if (! params::isToneSpaceParameter (id))
                        continue;

                    juce::ValueTree param ("PARAM");
                    param.setProperty ("id", juce::String (id), nullptr);
                    complete.appendChild (param, nullptr);
                }
                expect (StateSchema::missingParameterIDs (complete).isEmpty());
            }
        }
    };

    static ToneSpaceTests toneSpaceTests;
}
