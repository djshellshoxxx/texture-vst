#include "TestHelpers.h"

#include "../Source/Presets/PresetManager.h"
#include "../Source/Presets/SoundSnapshot.h"
#include "../Source/Presets/VariationHistory.h"
#include "../Source/Presets/VariationPreview.h"

#include <string>
#include <vector>

namespace texture::tests
{
    namespace
    {
        using History = VariationHistory<int>;

        /** A stand-in sound: the preview host reads and writes this value. */
        struct FakeSound
        {
            int value { 0 };
            int undoStack { 0 };      // number of preview steps that undo can still revert
            int previewsApplied { 0 };
        };

        struct FakeHost
        {
            FakeSound& sound;
            int captures { 0 };

            int captureState() { ++captures; return sound.value; }

            void applyPreview (int target)
            {
                sound.undoStack = sound.value == target ? sound.undoStack : sound.undoStack + 1;
                sound.value = target;
                ++sound.previewsApplied;
            }

            void restorePreview (int before)
            {
                // Models the processor: undo is exact for the newest preview step.
                sound.value = before;
                if (sound.undoStack > 0)
                    --sound.undoStack;
            }
        };
    }

    class VariationHistoryTests : public juce::UnitTest
    {
    public:
        VariationHistoryTests()
            : juce::UnitTest ("Variation history", "texture") {}

        void runTest() override
        {
            beginTest ("the history starts empty and newest-first navigation has nothing to show");
            {
                History history;
                expect (history.empty());
                expect (history.getCursor() == History::noCursor);
                expect (! history.moveNext());
                expect (! history.movePrevious());
            }

            beginTest ("the history keeps at most 32 entries and evicts the oldest unpinned first");
            {
                History history;

                for (int i = 0; i < 40; ++i)
                    expect (history.add (i, "Randomize", 1000 + i) == History::AddResult::added);

                expectEquals (static_cast<int> (history.size()), 32);
                expectEquals (history.at (0).payload, 8);
                expectEquals (history.at (31).payload, 39);
            }

            beginTest ("pinned entries survive eviction, and at most eight can be pinned");
            {
                History history;
                for (int i = 0; i < 32; ++i)
                    history.add (i, "Mutate", i);

                for (std::size_t i = 0; i < History::maxPinned; ++i)
                    expect (history.setPinned (i, true), juce::String ("pin ") + juce::String (static_cast<int> (i)));

                expect (! history.setPinned (History::maxPinned, true), "a ninth pin must be refused");
                expectEquals (static_cast<int> (history.pinnedCount()), 8);

                // The oldest unpinned entry is 8, so it is the one that goes.
                expect (history.add (100, "Mutate", 100) == History::AddResult::added);
                expectEquals (history.at (0).payload, 0);
                expectEquals (history.at (7).payload, 7);
                expectEquals (history.at (8).payload, 9);
                expectEquals (static_cast<int> (history.size()), 32);
            }

            beginTest ("eight pins always leave room, so a full history keeps accepting variations");
            {
                // The pin cap is below capacity, so there is always an unpinned candidate.
                History history;
                for (int i = 0; i < 32; ++i)
                    history.add (i, "Mutate", i);

                for (std::size_t i = 0; i < History::maxPinned; ++i)
                    history.setPinned (i, true);

                for (int i = 0; i < 100; ++i)
                    expect (history.add (1000 + i, "Mutate", i) == History::AddResult::added);

                expectEquals (static_cast<int> (history.size()), 32);
                expectEquals (static_cast<int> (history.pinnedCount()), 8);
                expectEquals (history.at (0).payload, 0);
            }

            beginTest ("walking the history moves the cursor and never adds an entry");
            {
                History history;
                for (int i = 0; i < 5; ++i)
                    history.add (i, "Randomize", i);

                const auto size = history.size();
                const auto revision = history.revision();
                expectEquals (static_cast<int> (history.getCursor()), 4);

                expect (history.movePrevious());
                expect (history.movePrevious());
                expectEquals (static_cast<int> (history.getCursor()), 2);
                expect (history.moveNext());
                expectEquals (static_cast<int> (history.getCursor()), 3);

                for (int i = 0; i < 10; ++i)
                    history.moveNext();
                expectEquals (static_cast<int> (history.getCursor()), 4);
                expect (! history.moveNext(), "no wrap at the newest entry");

                expectEquals (static_cast<int> (history.size()), static_cast<int> (size));
                expect (history.revision() != revision, "the GUI needs to see cursor moves");
            }

            beginTest ("after an eviction the cursor sits on the newest variation");
            {
                History history;
                for (int i = 0; i < 32; ++i)
                    history.add (i, "Mutate", i);

                history.movePrevious();
                history.movePrevious();
                history.add (99, "Mutate", 99);

                expectEquals (static_cast<int> (history.getCursor()), static_cast<int> (history.size() - 1));
                expectEquals (history.at (history.getCursor()).payload, 99);
            }

            beginTest ("clear empties the history, its pins and its cursor");
            {
                History history;
                for (int i = 0; i < 4; ++i)
                    history.add (i, "Randomize", i);
                history.setPinned (1, true);

                history.clear();
                expect (history.empty());
                expectEquals (static_cast<int> (history.pinnedCount()), 0);
                expect (history.getCursor() == History::noCursor);
            }

            beginTest ("a preview restores the exact sound from before it, even after a second preview");
            {
                FakeSound sound;
                sound.value = 7;
                FakeHost host { sound };

                VariationPreview<int> preview;
                expect (! preview.isActive());

                preview.begin (host, 2, 42);
                expect (preview.isActive());
                expectEquals (sound.value, 42);
                expectEquals (static_cast<int> (preview.getActiveIndex()), 2);

                // Moving to another variation restores first, so the restore point stays 7.
                preview.begin (host, 5, 99);
                expectEquals (sound.value, 99);
                expectEquals (preview.getRemembered(), 7);

                preview.cancel (host);
                expect (! preview.isActive());
                expectEquals (sound.value, 7);
                expectEquals (sound.undoStack, 0);
            }

            beginTest ("accepting a preview keeps the sound and ends the preview");
            {
                FakeSound sound;
                sound.value = 1;
                FakeHost host { sound };

                VariationPreview<int> preview;
                preview.begin (host, 0, 55);
                preview.accept();
                expect (! preview.isActive());
                expectEquals (sound.value, 55);

                preview.cancel (host);
                expectEquals (sound.value, 55, "cancelling after accept must not revert");
            }

            beginTest ("PresetManager's range rules are the single validator for snapshots");
            {
                expect (PresetManager::isValidParameterValue (params::morph, 0.5));
                expect (! PresetManager::isValidParameterValue (params::morph, 1.5));
                expect (! PresetManager::isValidParameterValue (params::morph, std::nan ("")));
                expect (! PresetManager::isValidParameterValue ("not_a_parameter", 0.0));
                expect (PresetManager::isValidParameterValue (params::voiceMode, 2.0));
                expect (! PresetManager::isValidParameterValue (params::voiceMode, 3.0));
                expect (! PresetManager::isValidParameterValue (params::toneFilterOn, 0.5));
                expect (! PresetManager::isValidParameterValue (params::glideMs, 2500.0));
            }

            beginTest ("retained media is counted once per buffer, however many variations share it");
            {
                const auto shared = makeTestSample (48000.0, 1.0, 220.0);
                const auto other = makeTestSample (48000.0, 2.0, 330.0);
                const auto sharedBytes = static_cast<std::size_t> (shared->getNumSamples())
                                       * static_cast<std::size_t> (shared->getNumChannels()) * sizeof (float);
                const auto otherBytes = static_cast<std::size_t> (other->getNumSamples())
                                      * static_cast<std::size_t> (other->getNumChannels()) * sizeof (float);

                expectEquals (uniqueMediaBytes ({ shared.get(), shared.get(), shared.get() }), sharedBytes);
                expectEquals (uniqueMediaBytes ({ shared.get(), other.get(), nullptr, shared.get() }),
                              sharedBytes + otherBytes);
                expectEquals (uniqueMediaBytes ({}), std::size_t { 0 });
            }
        }
    };

    static VariationHistoryTests variationHistoryTests;
}
