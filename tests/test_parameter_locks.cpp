#include "TestHelpers.h"

#include "../Source/Presets/LockPolicy.h"
#include "../Source/Utility/ParameterIDs.h"

namespace texture::tests
{
    class ParameterLockTests : public juce::UnitTest
    {
    public:
        ParameterLockTests()
            : juce::UnitTest ("Parameter locks", "texture") {}

        void runTest() override
        {
            beginTest ("every registered parameter has exactly one lock group");
            {
                expectEquals (static_cast<int> (LockPolicy::numFields), params::numParameters);

                for (const auto* id : params::allIDs)
                {
                    const auto* field = LockPolicy::findField (id);
                    expect (field != nullptr, juce::String ("missing lock registry entry: ") + id);
                }

                int total = 0;
                for (int group = 0; group < numLockGroups; ++group)
                    total += LockPolicy::countFieldsIn (static_cast<LockGroup> (group));

                expectEquals (total, params::numParameters);
            }

            beginTest ("pitch calibration and output gain are never mutable by default");
            {
                for (const auto* id : { params::tune, params::layer1Root, params::layer2Root,
                                        params::layer1FineCents, params::layer2FineCents, params::gainDb })
                {
                    const auto* field = LockPolicy::findField (id);
                    expect (field != nullptr);
                    expect (field == nullptr || ! field->mutableByDefault, id);
                }
            }

            beginTest ("unknown IDs cannot be locked and are reported when parsed");
            {
                LockPolicy::LockSet locks;
                expect (! locks.setLocked ("not_a_parameter", true));
                expect (! locks.isLocked ("not_a_parameter"));

                const auto parsed = LockPolicy::parseIds ({ params::morph, "legacy_knob", params::morph });
                expect (parsed.accepted);
                expectEquals (static_cast<int> (parsed.locks.lockedIds().size()), 1);
                expect (parsed.locks.isLocked (params::morph));
                expectEquals (static_cast<int> (parsed.ignoredIds.size()), 1);
                expect (parsed.ignoredIds.front() == "legacy_knob");
            }

            beginTest ("duplicate persisted IDs collapse into one lock");
            {
                const auto parsed = LockPolicy::parseIds ({ params::attack, params::attack, params::attack });
                expect (parsed.accepted);
                expectEquals (parsed.locks.countLockedTotal(), 1);
            }

            beginTest ("oversized, empty and overlong lock lists are rejected as a whole");
            {
                std::vector<std::string> tooMany (LockPolicy::maxLocks + 1, params::morph);
                expect (! LockPolicy::parseIds (tooMany).accepted);
                expect (! LockPolicy::parseIds ({ "" }).accepted);
                expect (! LockPolicy::parseIds ({ std::string (LockPolicy::maxIdLength + 1, 'x') }).accepted);
                expect (LockPolicy::parseIds (std::vector<std::string> (LockPolicy::maxLocks, "unknown")).accepted);
            }

            beginTest ("group lock state is none, some or all");
            {
                LockPolicy::LockSet locks;
                expect (locks.groupState (LockGroup::envelope) == LockPolicy::GroupState::none);

                locks.setLocked (params::attack, true);
                expect (locks.groupState (LockGroup::envelope) == LockPolicy::GroupState::some);

                locks.setGroupLocked (LockGroup::envelope, true);
                expect (locks.groupState (LockGroup::envelope) == LockPolicy::GroupState::all);
                expectEquals (locks.countLocked (LockGroup::envelope), 4);

                locks.setGroupLocked (LockGroup::envelope, false);
                expect (locks.groupState (LockGroup::envelope) == LockPolicy::GroupState::none);
                expect (locks.empty());
            }

            beginTest ("locks are stored sorted so equal sets compare equal");
            {
                LockPolicy::LockSet first;
                first.setLocked (params::release, true);
                first.setLocked (params::morph, true);

                LockPolicy::LockSet second;
                second.setLocked (params::morph, true);
                second.setLocked (params::release, true);

                expect (first == second);
                expect (first.lockedIds().front() == params::morph);
            }

            beginTest ("the lock set is bounded");
            {
                LockPolicy::LockSet locks;
                int accepted = 0;

                for (const auto& field : LockPolicy::fields())
                    if (locks.setLocked (field.id, true))
                        ++accepted;

                expectEquals (accepted, params::numParameters);
                expect (locks.countLockedTotal() <= static_cast<int> (LockPolicy::maxLocks));
            }

            beginTest ("randomize and mutate respect locks while automation and reset do not");
            {
                LockPolicy::LockSet locks;

                for (const auto* id : params::allIDs)
                {
                    locks.setLocked (id, true);

                    expect (! LockPolicy::isEditable (EditAction::randomize, id, locks), id);
                    expect (! LockPolicy::isEditable (EditAction::mutate, id, locks), id);
                    expect (LockPolicy::isEditable (EditAction::automation, id, locks), id);
                    expect (LockPolicy::isEditable (EditAction::reset, id, locks), id);
                    expect (LockPolicy::isEditable (EditAction::projectRestore, id, locks), id);
                    expect (LockPolicy::isEditable (EditAction::undoRedo, id, locks), id);
                    expect (LockPolicy::isEditable (EditAction::explicitEdit, id, locks), id);
                }

                expect (LockPolicy::isEditable (EditAction::randomize, params::morph, LockPolicy::LockSet{}));
            }

            beginTest ("lock history undoes lock changes in reverse order and is bounded");
            {
                LockHistory history;
                LockPolicy::LockSet state;
                LockPolicy::LockSet restored;

                expect (! history.canUndo());
                expect (! history.undo (restored));

                history.record (state);
                state.setLocked (params::morph, true);
                history.record (state);
                state.setLocked (params::attack, true);

                expect (history.undo (restored));
                expect (restored.isLocked (params::morph));
                expect (! restored.isLocked (params::attack));

                expect (history.undo (restored));
                expect (! restored.isLocked (params::morph));
                expect (! history.canUndo());

                for (std::size_t i = 0; i < LockHistory::maxDepth + 10; ++i)
                    history.record (LockPolicy::LockSet{});

                int undone = 0;
                while (history.undo (restored))
                    ++undone;

                expectEquals (undone, static_cast<int> (LockHistory::maxDepth));
            }
        }
    };

    static ParameterLockTests parameterLockTests;
}
