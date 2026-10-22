#include "TestHelpers.h"

#include "../Source/Effects/EffectsChain.h"
#include "../Source/Engine/GranularEngine.h"

#include <array>
#include <cmath>
#include <vector>

// Archived render fixtures (00_SHARED_CONTRACTS.md, "Compatibility and state
// ownership"). The checkpoints below were captured from the released polyphonic
// engine and effects chain before the F07/F11 changes. Any change to the default
// path must keep them within the tolerance; a deliberate DSP change must update
// them in the same commit with the reason recorded in PROGRESS.md.
//
// Tolerance is absolute and deliberately loose enough for cross-platform float
// differences; it is far smaller than any audible change.

namespace texture::tests
{
    namespace
    {
        constexpr int fixtureBlock = 128;
        constexpr double fixtureRate = 48000.0;
        constexpr float fixtureTolerance = 1.0e-5f;

        struct Checkpoint
        {
            float left;
            float right;
            float rms;
        };

        Checkpoint measure (const juce::AudioBuffer<float>& buffer)
        {
            Checkpoint point { buffer.getSample (0, 37), buffer.getSample (1, 101), 0.0f };
            double sum = 0.0;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    sum += static_cast<double> (buffer.getSample (ch, i)) * buffer.getSample (ch, i);

            point.rms = static_cast<float> (std::sqrt (sum / (2.0 * buffer.getNumSamples())));
            return point;
        }

        /** Fixed polyphonic script: two notes, a sustain-held release, and a tail. */
        std::vector<Checkpoint> renderPolyScript()
        {
            GranularEngine engine;
            engine.prepare (fixtureRate, fixtureBlock);
            engine.setAdsrParameters (250.0f, 500.0f, 0.8f, 1200.0f);

            const auto sample = makeTestSample (fixtureRate, 2.0, 220.0);
            const auto targets = deriveMorphTargets (0.35f);

            std::vector<Checkpoint> points;
            juce::AudioBuffer<float> buffer (2, fixtureBlock);

            const auto block = [&]
            {
                buffer.clear();
                engine.renderNextBlock (sample, targets, buffer, 0, fixtureBlock);
                points.push_back (measure (buffer));
            };

            engine.noteOn (1, 60, 0.8f);
            for (int i = 0; i < 12; ++i) block();

            engine.noteOn (1, 64, 0.7f);
            for (int i = 0; i < 12; ++i) block();

            engine.setSustainPedal (1, true);
            engine.noteOff (1, 60);
            for (int i = 0; i < 8; ++i) block();

            engine.setSustainPedal (1, false);
            engine.noteOff (1, 64);
            for (int i = 0; i < 12; ++i) block();

            return points;
        }

        /** Fixed effects script: deterministic noise through the chain at MORPH 0.35. */
        std::vector<Checkpoint> renderEffectsScript()
        {
            EffectsChain chain;
            chain.prepare (fixtureRate, fixtureBlock, 2);
            chain.setMorphTargets (deriveMorphTargets (0.35f));
            chain.setReverbEnabled (true);
            chain.setOutputGainDb (0.0f);

            juce::Random random (20261021);
            std::vector<Checkpoint> points;
            juce::AudioBuffer<float> buffer (2, fixtureBlock);

            for (int i = 0; i < 10; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < fixtureBlock; ++s)
                        buffer.setSample (ch, s, 0.25f * (random.nextFloat() * 2.0f - 1.0f));

                chain.process (buffer, 0, fixtureBlock);
                points.push_back (measure (buffer));
            }

            return points;
        }

        void expectMatches (juce::UnitTest& test, const std::vector<Checkpoint>& actual,
                            const std::array<Checkpoint, 6>& expected, int firstIndex)
        {
            for (size_t i = 0; i < expected.size(); ++i)
            {
                const auto& got = actual[static_cast<size_t> (firstIndex) + i];
                const auto& want = expected[i];
                const auto message = juce::String ("checkpoint ") + juce::String (static_cast<int> (i))
                                   + " actual L=" + juce::String (got.left, 9)
                                   + " R=" + juce::String (got.right, 9)
                                   + " rms=" + juce::String (got.rms, 9);

                test.expect (std::abs (got.left - want.left) <= fixtureTolerance
                             && std::abs (got.right - want.right) <= fixtureTolerance
                             && std::abs (got.rms - want.rms) <= fixtureTolerance, message);
            }
        }
    }

    class LegacyRenderFixtureTests : public juce::UnitTest
    {
    public:
        LegacyRenderFixtureTests()
            : juce::UnitTest ("Legacy render fixtures", "texture") {}

        void runTest() override
        {
            beginTest ("default polyphonic engine script matches the archived render");
            {
                const auto points = renderPolyScript();
                expectEquals (static_cast<int> (points.size()), 44);

                // Checkpoints at blocks 0, 11, 23, 30, 36 and 43.
                const std::array<Checkpoint, 6> expected {{
                    { -0.000004041f, -0.000105140f, 0.000061911f },
                    {  0.002042440f,  0.019110829f, 0.015225115f },
                    {  0.017997371f, -0.048551511f, 0.036702961f },
                    { -0.043283239f, -0.036790989f, 0.044813242f },
                    { -0.012111546f,  0.114637509f, 0.075410098f },
                    {  0.007679056f,  0.082866840f, 0.062313769f }
                }};

                const std::array<int, 6> blocks { 0, 11, 23, 30, 36, 43 };
                for (size_t i = 0; i < blocks.size(); ++i)
                {
                    const auto& got = points[static_cast<size_t> (blocks[i])];
                    const auto& want = expected[i];
                    expect (std::abs (got.left - want.left) <= fixtureTolerance
                                && std::abs (got.right - want.right) <= fixtureTolerance
                                && std::abs (got.rms - want.rms) <= fixtureTolerance,
                            juce::String ("block ") + juce::String (blocks[i])
                                + " actual L=" + juce::String (got.left, 9)
                                + " R=" + juce::String (got.right, 9)
                                + " rms=" + juce::String (got.rms, 9));
                }
            }

            beginTest ("default effects chain at MORPH 0.35 matches the archived render");
            {
                const auto points = renderEffectsScript();
                expectEquals (static_cast<int> (points.size()), 10);

                const std::array<Checkpoint, 6> expected {{
                    {  0.022988588f, -0.265974432f, 0.181138963f },
                    {  0.295124143f, -0.260800868f, 0.223663613f },
                    {  0.240771681f, -0.435865879f, 0.265808791f },
                    {  0.016414165f, -0.350229442f, 0.309629172f },
                    {  0.493639827f, -0.299348980f, 0.308586389f },
                    { -0.304103017f, -0.628925145f, 0.311002553f }
                }};
                expectMatches (*this, points, expected, 0);
            }
        }
    };

    static LegacyRenderFixtureTests legacyRenderFixtureTests;
}
