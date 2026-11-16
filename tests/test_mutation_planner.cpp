#include "TestHelpers.h"

#include "../Source/Presets/LockPolicy.h"
#include "../Source/Presets/MutationPlanner.h"
#include "../Source/Utility/VersionedRandomStreams.h"

#include <cmath>
#include <vector>

namespace texture::tests
{
    namespace
    {
        constexpr std::uint32_t allSoundGroups = MutationPlanner::groupBit (LockGroup::envelope)
                                               | MutationPlanner::groupBit (LockGroup::grain)
                                               | MutationPlanner::groupBit (LockGroup::motion)
                                               | MutationPlanner::groupBit (LockGroup::toneSpace);

        /** A fixed patch: continuous values, one bool, one choice and one stepped value. */
        std::vector<MutationField> makePatch()
        {
            std::vector<MutationField> fields;
            fields.push_back ({ "morph", LockGroup::motion, FieldKind::continuous, 0.5f, 0, true });
            fields.push_back ({ "attack", LockGroup::envelope, FieldKind::continuous, 0.25f, 0, true });
            fields.push_back ({ "decay", LockGroup::envelope, FieldKind::continuous, 0.0f, 0, true });
            fields.push_back ({ "sustain", LockGroup::envelope, FieldKind::continuous, 1.0f, 0, true });
            fields.push_back ({ "reverse", LockGroup::grain, FieldKind::continuous, 0.7f, 0, true });
            fields.push_back ({ "reverb_on", LockGroup::toneSpace, FieldKind::categorical, 1.0f, 2, true });
            fields.push_back ({ "lfo_division", LockGroup::motion, FieldKind::categorical, 0.5f, 5, true });
            fields.push_back ({ "tune", LockGroup::pitchCalibration, FieldKind::discrete, 0.5f, 49, false });
            fields.push_back ({ "gain_db", LockGroup::toneSpace, FieldKind::continuous, 0.8f, 0, false });
            return fields;
        }

        MutationRequest makeRequest (float strength, std::uint64_t seed, std::uint32_t mask = allSoundGroups,
                                     bool categorical = false, std::uint64_t revision = 7)
        {
            MutationRequest request;
            request.strength = strength;
            request.seed = seed;
            request.groupMask = mask;
            request.allowCategorical = categorical;
            request.baselineRevision = revision;
            return request;
        }

        bool sameChanges (const MutationPlan& a, const MutationPlan& b)
        {
            if (a.changes.size() != b.changes.size())
                return false;

            for (std::size_t i = 0; i < a.changes.size(); ++i)
                if (a.changes[i].fieldIndex != b.changes[i].fieldIndex
                    || std::abs (a.changes[i].normalised - b.changes[i].normalised) > 0.0f)
                    return false;

            return true;
        }
    }

    class MutationPlannerTests : public juce::UnitTest
    {
    public:
        MutationPlannerTests()
            : juce::UnitTest ("Gentle mutate planner", "texture") {}

        void runTest() override
        {
            const auto fields = makePatch();
            const LockPolicy::LockSet noLocks;

            beginTest ("zero strength is a strict no-op");
            {
                const auto plan = MutationPlanner::plan (fields, noLocks, makeRequest (0.0f, 1234), 7);
                expect (plan.status == PlanStatus::noOp);
                expect (plan.changes.empty());
            }

            beginTest ("invalid strength and non-finite values change nothing");
            {
                const auto nan = std::nanf ("");
                expect (MutationPlanner::plan (fields, noLocks, makeRequest (nan, 1), 7).status == PlanStatus::invalidInput);
                expect (MutationPlanner::plan (fields, noLocks, makeRequest (-0.1f, 1), 7).status == PlanStatus::invalidInput);
                expect (MutationPlanner::plan (fields, noLocks, makeRequest (1.1f, 1), 7).status == PlanStatus::invalidInput);

                auto broken = fields;
                broken[0].normalised = nan;
                expect (MutationPlanner::plan (broken, noLocks, makeRequest (0.5f, 1), 7).status
                        == PlanStatus::invalidInput);

                auto badChoice = fields;
                badChoice[5].choiceCount = 1;
                expect (MutationPlanner::plan (badChoice, noLocks, makeRequest (0.5f, 1), 7).status
                        == PlanStatus::invalidInput);

                std::vector<MutationField> tooMany (MutationPlanner::maxFields + 1, fields[0]);
                expect (MutationPlanner::plan (tooMany, noLocks, makeRequest (0.5f, 1), 7).status
                        == PlanStatus::invalidInput);
            }

            beginTest ("a parameter edit after the baseline rejects the stale plan");
            {
                const auto plan = MutationPlanner::plan (fields, noLocks, makeRequest (0.5f, 99, allSoundGroups, false, 7), 8);
                expect (plan.status == PlanStatus::staleRevision);
                expect (plan.changes.empty());
            }

            beginTest ("known seeds reproduce exact change sets");
            {
                const auto first = MutationPlanner::plan (fields, noLocks, makeRequest (0.5f, 42), 7);
                const auto second = MutationPlanner::plan (fields, noLocks, makeRequest (0.5f, 42), 7);
                expect (first.status == PlanStatus::ok);
                expect (! first.changes.empty());
                expect (sameChanges (first, second));

                const auto other = MutationPlanner::plan (fields, noLocks, makeRequest (0.5f, 43), 7);
                expect (! sameChanges (first, other));
            }

            beginTest ("the first draw matches the documented triangular formula on the mutation lane");
            {
                // Contract: delta = u1 + u2 - 1 from lane `mutation`, index = field position,
                // draws 0 and 1; u' = reflect(u + 0.25 * s * delta). Pins the wiring, not just
                // the arithmetic.
                const std::uint64_t seed = VersionedRandomStreams::defaultSeed;
                const float strength = 0.15f;
                const auto index = std::size_t { 0 };
                const auto u1 = VersionedRandomStreams::unitFloat (seed, VersionedRandomStreams::Lane::mutation, index, 0);
                const auto u2 = VersionedRandomStreams::unitFloat (seed, VersionedRandomStreams::Lane::mutation, index, 1);
                const auto expected = MutationPlanner::reflectIntoUnit (0.5f + 0.25f * strength * (u1 + u2 - 1.0f));

                const auto plan = MutationPlanner::plan (fields, noLocks, makeRequest (strength, seed), 7);
                expect (! plan.changes.empty());
                expect (plan.changes.front().fieldIndex == 0);
                expectWithinAbsoluteError (plan.changes.front().normalised, expected, 1.0e-7f);
            }

            beginTest ("every changed value is finite, in range and stays within the step bound");
            {
                for (std::uint64_t seed = 1; seed <= 200; ++seed)
                {
                    const auto plan = MutationPlanner::plan (fields, noLocks, makeRequest (1.0f, seed, allSoundGroups, true), 7);
                    for (const auto& change : plan.changes)
                    {
                        const auto& field = fields[change.fieldIndex];
                        expect (std::isfinite (change.normalised));
                        expect (change.normalised >= 0.0f && change.normalised <= 1.0f);

                        if (field.kind == FieldKind::continuous)
                            expect (std::abs (change.normalised - field.normalised)
                                        <= MutationPlanner::maxRelativeStep + 1.0e-6f);
                    }
                }
            }

            beginTest ("boundary values reflect back into range");
            {
                std::vector<MutationField> edges {
                    { "morph", LockGroup::motion, FieldKind::continuous, 0.0f, 0, true },
                    { "attack", LockGroup::envelope, FieldKind::continuous, 1.0f, 0, true } };

                for (std::uint64_t seed = 1; seed <= 500; ++seed)
                {
                    const auto plan = MutationPlanner::plan (edges, noLocks, makeRequest (1.0f, seed), 7);
                    for (const auto& change : plan.changes)
                        expect (change.normalised >= 0.0f && change.normalised <= 1.0f);
                }

                expectWithinAbsoluteError (MutationPlanner::reflectIntoUnit (-0.1f), 0.1f, 1.0e-6f);
                expectWithinAbsoluteError (MutationPlanner::reflectIntoUnit (1.2f), 0.8f, 1.0e-6f);
                expectWithinAbsoluteError (MutationPlanner::reflectIntoUnit (0.4f), 0.4f, 1.0e-6f);
            }

            beginTest ("the triangular step has the expected mean magnitude");
            {
                // For a triangular delta on [-1, 1], E|delta| = 1/3, so with no reflection
                // and strength 1 the mean move is 0.25 / 3 = 0.0833.
                std::vector<MutationField> centre { { "attack", LockGroup::envelope, FieldKind::continuous, 0.5f, 0, true } };
                double total = 0.0;
                int count = 0;

                for (std::uint64_t seed = 1; seed <= 4000; ++seed)
                {
                    const auto plan = MutationPlanner::plan (centre, noLocks, makeRequest (1.0f, seed), 7);
                    for (const auto& change : plan.changes)
                    {
                        total += std::abs (change.normalised - 0.5f);
                        ++count;
                    }
                }

                expect (count > 3500);
                const auto mean = total / static_cast<double> (count);
                expect (mean > 0.078 && mean < 0.089, "mean move " + juce::String (mean));
            }

            beginTest ("locked fields stay identical over 100 generated variations");
            {
                LockPolicy::LockSet locks;
                locks.setLocked ("morph", true);
                locks.setLocked ("attack", true);
                locks.setLocked ("reverb_on", true);

                for (std::uint64_t seed = 1; seed <= 100; ++seed)
                {
                    const auto plan = MutationPlanner::plan (fields, locks,
                                                             makeRequest (1.0f, seed, allSoundGroups, true), 7);
                    expect (plan.status == PlanStatus::ok);

                    for (const auto& change : plan.changes)
                    {
                        const auto id = fields[change.fieldIndex].id;
                        expect (! locks.isLocked (id), juce::String ("locked field changed: ") + juce::String (std::string (id)));
                    }
                }
            }

            beginTest ("every registered parameter is held by its lock");
            {
                // Build a one-field patch for each registered parameter from the registry, then
                // check that its lock alone prevents any change over 100 seeds.
                for (const auto& info : LockPolicy::fields())
                {
                    // Every group selected, so the lock alone decides whether the field is held.
                    const std::vector<MutationField> one {
                        { info.id, info.group, FieldKind::continuous, 0.5f, 0, true } };

                    LockPolicy::LockSet locked;
                    locked.setLocked (info.id, true);

                    for (std::uint64_t seed = 1; seed <= 100; ++seed)
                    {
                        const auto plan = MutationPlanner::plan (one, locked, makeRequest (1.0f, seed, 0xFFFFFFFFu), 7);
                        expect (plan.changes.empty(), juce::String (std::string (info.id)));
                        expectEquals (plan.lockedSkipped, 1);
                    }
                }
            }

            beginTest ("all-locked patch is an ok no-op with a locked count");
            {
                LockPolicy::LockSet locks;
                for (const auto& field : fields)
                    locks.setLocked (field.id, true);

                const auto plan = MutationPlanner::plan (fields, locks, makeRequest (1.0f, 5, allSoundGroups, true), 7);
                expect (plan.status == PlanStatus::ok);
                expect (plan.changes.empty());
                expect (plan.lockedSkipped > 0);
            }

            beginTest ("ineligible and unselected groups are never changed");
            {
                // Only grain is selected: morph (motion) and attack (envelope) must not move,
                // and tune / gain are ineligible even when their groups are selected.
                const auto plan = MutationPlanner::plan (fields, noLocks,
                                                         makeRequest (1.0f, 3, MutationPlanner::groupBit (LockGroup::grain), true), 7);
                for (const auto& change : plan.changes)
                    expect (fields[change.fieldIndex].group == LockGroup::grain, juce::String (std::string (fields[change.fieldIndex].id)));

                const auto all = MutationPlanner::plan (fields, noLocks, makeRequest (1.0f, 3, allSoundGroups, true), 7);
                for (const auto& change : all.changes)
                    expect (fields[change.fieldIndex].eligible);
            }

            beginTest ("categorical changes are off by default and rare when enabled");
            {
                int offChanges = 0;
                int onChanges = 0;

                for (std::uint64_t seed = 1; seed <= 1000; ++seed)
                {
                    const auto off = MutationPlanner::plan (fields, noLocks, makeRequest (1.0f, seed, allSoundGroups, false), 7);
                    const auto on = MutationPlanner::plan (fields, noLocks, makeRequest (1.0f, seed, allSoundGroups, true), 7);

                    for (const auto& change : off.changes)
                        if (fields[change.fieldIndex].kind == FieldKind::categorical)
                            ++offChanges;

                    for (const auto& change : on.changes)
                        if (fields[change.fieldIndex].kind == FieldKind::categorical)
                            ++onChanges;
                }

                expectEquals (offChanges, 0);
                // reverb_on and lfo_division at strength 1 give a 0.2 chance each: roughly 400 moves.
                expect (onChanges > 250 && onChanges < 550, "categorical moves " + juce::String (onChanges));
            }

            beginTest ("categorical values land on a different permitted choice");
            {
                const std::vector<MutationField> choice { { "lfo_division", LockGroup::motion, FieldKind::categorical, 0.5f, 5, true } };

                for (std::uint64_t seed = 1; seed <= 500; ++seed)
                {
                    const auto plan = MutationPlanner::plan (choice, noLocks, makeRequest (1.0f, seed, allSoundGroups, true), 7);
                    for (const auto& change : plan.changes)
                    {
                        const auto index = std::lround (change.normalised * 4.0f);
                        expect (index >= 0 && index <= 4);
                        expect (std::abs (change.normalised - 0.5f) > 0.1f);
                    }
                }
            }

            beginTest ("stepped values are quantised after mapping back");
            {
                const std::vector<MutationField> stepped { { "tune", LockGroup::envelope, FieldKind::discrete, 0.5f, 49, true } };

                for (std::uint64_t seed = 1; seed <= 300; ++seed)
                {
                    const auto plan = MutationPlanner::plan (stepped, noLocks, makeRequest (1.0f, seed), 7);
                    for (const auto& change : plan.changes)
                    {
                        const auto steps = change.normalised * 48.0f;
                        expectWithinAbsoluteError (steps, std::round (steps), 1.0e-3f);
                    }
                }
            }

            beginTest ("the revision check and locks both apply before any change is produced");
            {
                LockPolicy::LockSet locks;
                locks.setLocked ("morph", true);

                const auto stale = MutationPlanner::plan (fields, locks, makeRequest (1.0f, 9, allSoundGroups, false, 1), 2);
                expect (stale.status == PlanStatus::staleRevision);
                expect (stale.changes.empty());
                expectEquals (stale.lockedSkipped, 0);
            }
        }
    };

    static MutationPlannerTests mutationPlannerTests;
}
