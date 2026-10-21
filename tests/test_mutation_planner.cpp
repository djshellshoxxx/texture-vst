#include "../Source/Presets/LockPolicy.h"
#include "../Source/Presets/MutationPlanner.h"
#include "../Source/Utility/ParameterIDs.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace texture::tests
{
    namespace
    {
        using Values = std::map<juce::String, float>;

        /** Every mutable parameter at mid-range, and the protected and excluded ones at their defaults. */
        Values midBaseline()
        {
            Values values;

            for (const auto& parameter : mutableParameters())
                values[parameter.id] = fromNormalised (parameter, 0.5f);

            values[params::tune] = params::tuneDefaultSemitones;
            values[params::layer1Root] = params::sourceRootDefaultMidi;
            values[params::layer2Root] = params::sourceRootDefaultMidi;
            values[params::layer1FineCents] = params::sourceFineDefaultCents;
            values[params::layer2FineCents] = params::sourceFineDefaultCents;
            values[params::gainDb] = params::gainDefaultDb;
            values[params::reverbOn] = params::reverbOnDefault ? 1.0f : 0.0f;
            values[params::lfoSync] = params::lfoSyncDefault ? 1.0f : 0.0f;
            values[params::lfoDivision] = static_cast<float> (params::lfoDivisionDefault);
            values[params::randomMode] = static_cast<float> (params::randomModeDefault);
            values[params::voiceMode] = static_cast<float> (params::voiceModeDefault);
            values[params::notePriority] = static_cast<float> (params::notePriorityDefault);
            values[params::toneFilterOn] = params::toneFilterOnDefault ? 1.0f : 0.0f;
            return values;
        }

        bool sameChanges (const std::vector<ParamChange>& a, const std::vector<ParamChange>& b)
        {
            if (a.size() != b.size())
                return false;

            for (std::size_t i = 0; i < a.size(); ++i)
                if (a[i].id != b[i].id || ! juce::exactlyEqual (a[i].value, b[i].value))
                    return false;

            return true;
        }

        bool containsId (const std::vector<ParamChange>& changes, const juce::String& id)
        {
            for (const auto& change : changes)
                if (change.id == id)
                    return true;

            return false;
        }

        LockSet allGroupsLocked()
        {
            LockSet locks;

            for (const auto group : allLockGroups)
                locks.lock (group);

            return locks;
        }
    }

    class MutationPlannerTests final : public juce::UnitTest
    {
    public:
        MutationPlannerTests() : juce::UnitTest ("Mutation planner", "texture") {}

        void runTest() override
        {
            const auto base = midBaseline();

            beginTest ("same seed reproduces the same change set");
            for (const std::uint64_t seed : { UINT64_C (1), UINT64_C (0xFEDCBA9876543210) })
            {
                const auto first = planMutation (base, LockSet{}, 0.5f, seed);
                const auto second = planMutation (base, LockSet{}, 0.5f, seed);
                expect (! first.empty());
                expect (sameChanges (first, second));
            }

            beginTest ("different seeds produce different change sets");
            expect (! sameChanges (planMutation (base, LockSet{}, 1.0f, 11),
                                   planMutation (base, LockSet{}, 1.0f, 12)));

            beginTest ("locked parameters never change");
            {
                LockSet locks;
                locks.lock (LockGroup::envelope);
                locks.lock (LockGroup::grain);

                for (std::uint64_t seed = 1; seed <= 200; ++seed)
                {
                    const auto changes = planMutation (base, locks, 1.0f, seed);

                    for (const auto& change : changes)
                        expect (! locks.isLocked (change.id), juce::String ("changed locked parameter ") + change.id);

                    expect (! containsId (changes, params::attack));
                    expect (! containsId (changes, params::release));
                    expect (! containsId (changes, params::reverse));
                    expect (! containsId (changes, params::shimmer));
                }

                expect (planMutation (base, allGroupsLocked(), 1.0f, 5).empty(), "all groups locked must be a no-op");
            }

            beginTest ("protected, choice, bool and mode parameters are never mutated");
            {
                const std::vector<juce::String> neverMutated {
                    params::tune, params::layer1Root, params::layer2Root,
                    params::layer1FineCents, params::layer2FineCents,
                    params::gainDb, params::reverbOn, params::lfoSync, params::lfoDivision,
                    params::randomMode, params::voiceMode, params::notePriority, params::toneFilterOn
                };

                for (const auto& id : neverMutated)
                    expect (findMutableParameter (id) == nullptr, juce::String ("mutable table contains ") + id);

                for (std::uint64_t seed = 1; seed <= 200; ++seed)
                {
                    for (const auto& change : planMutation (base, LockSet{}, 1.0f, seed))
                    {
                        expect (findMutableParameter (change.id) != nullptr, juce::String ("unexpected change ") + change.id);
                        expect (! isCalibrationParameter (change.id));
                    }
                }

                // Calibration stays protected even when nothing is locked.
                const LockSet none;
                expect (none.isLocked (params::tune));
                expect (none.isLocked (params::layer2Root));
            }

            beginTest ("zero, negative or non-finite amount changes nothing");
            expect (planMutation (base, LockSet{}, 0.0f, 9).empty());
            expect (planMutation (base, LockSet{}, -1.0f, 9).empty());
            expect (planMutation (base, LockSet{}, std::numeric_limits<float>::quiet_NaN(), 9).empty());

            beginTest ("every output is finite and inside its real range");
            for (int edge = 0; edge < 2; ++edge)
            {
                // Start from both extremes so clamping is exercised at each edge.
                Values values = midBaseline();

                for (const auto& parameter : mutableParameters())
                    values[parameter.id] = edge == 0 ? parameter.min : parameter.max;

                for (std::uint64_t seed = 1; seed <= 500; ++seed)
                {
                    for (const auto& change : planMutation (values, LockSet{}, 1.0f, seed))
                    {
                        const auto* parameter = findMutableParameter (change.id);
                        expect (parameter != nullptr);

                        if (parameter == nullptr)
                            continue;

                        expect (std::isfinite (change.value), juce::String ("non-finite ") + change.id);
                        expect (change.value >= parameter->min && change.value <= parameter->max,
                                juce::String ("out of range ") + change.id);
                    }
                }
            }

            beginTest ("normalised step never exceeds the strength bound");
            for (const float amount : { 0.1f, 0.5f, 1.0f })
            {
                for (std::uint64_t seed = 1; seed <= 200; ++seed)
                {
                    for (const auto& change : planMutation (base, LockSet{}, amount, seed))
                    {
                        const auto* parameter = findMutableParameter (change.id);
                        expect (parameter != nullptr);

                        if (parameter == nullptr)
                            continue;

                        const float before = toNormalised (*parameter, base.at (change.id));
                        const float after = toNormalised (*parameter, change.value);
                        expect (std::abs (after - before) <= maxNormalisedStep * amount + 1.0e-4f,
                                juce::String ("step too large for ") + change.id);
                    }
                }
            }

            beginTest ("lock lists round-trip and ignore unknown names");
            {
                LockSet locks;
                locks.lock (LockGroup::envelope);
                locks.lock (LockGroup::play);

                expect (locks.toString() == juce::String ("envelope,play"));
                expect (LockSet::fromString (locks.toString()) == locks);

                const auto parsed = LockSet::fromString (" Grain , bogus ,grain,, ");
                expect (parsed.isGroupLocked (LockGroup::grain));
                expect (! parsed.isGroupLocked (LockGroup::envelope));
                expect (LockSet::fromString ("").toString().isEmpty());
            }
        }
    };

    static MutationPlannerTests mutationPlannerTests;
}
