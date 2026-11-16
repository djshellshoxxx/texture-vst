#pragma once

#include "LockPolicy.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace texture
{
    /** How a value moves when Gentle Mutate changes it. */
    enum class FieldKind : std::uint8_t
    {
        continuous,   // moves by a bounded step in host-normalised space
        discrete,     // continuous step, then quantised to choiceCount values
        categorical   // off by default: occasionally jumps to another choice
    };

    /** One baseline value. The processor fills this from a captured snapshot. */
    struct MutationField
    {
        std::string_view id;
        LockGroup group { LockGroup::motion };
        FieldKind kind { FieldKind::continuous };
        float normalised { 0.0f };  // baseline, host-normalised [0, 1]
        int choiceCount { 0 };      // discrete/categorical: number of values (>= 2)
        bool eligible { false };    // registry + capability decision, computed by the caller
    };

    struct MutationRequest
    {
        float strength { 0.0f };          // [0, 1]; 0 is a strict no-op
        std::uint64_t seed { 0 };         // operation seed, not an automatable value
        std::uint32_t groupMask { 0 };    // MutationPlanner::groupBit() values
        bool allowCategorical { false };
        std::uint64_t baselineRevision { 0 };
    };

    struct FieldChange
    {
        std::size_t fieldIndex { 0 };
        float normalised { 0.0f };
    };

    enum class PlanStatus : std::uint8_t
    {
        ok,            // changes may be empty (all locked, or nothing moved)
        noOp,          // strength is zero
        invalidInput,  // non-finite values, bad strength, or too many fields
        staleRevision  // the parameters changed after the baseline was captured
    };

    struct MutationPlan
    {
        PlanStatus status { PlanStatus::noOp };
        std::vector<FieldChange> changes;
        int lockedSkipped { 0 };  // eligible, selected fields held by a lock
    };

    /** Pure planner for Gentle Mutate. No JUCE, processor or GUI dependency.

        For each eligible continuous value u, a triangular delta on [-1, 1] is drawn
        from the mutation lane and u' = reflect(u + 0.25 * s * delta) into [0, 1].
        Draws are indexed by field position, so locking one field never shifts the
        draws of another.
    */
    class MutationPlanner final
    {
    public:
        static constexpr std::size_t maxFields = 64;
        static constexpr float maxRelativeStep = 0.25f;
        static constexpr float categoricalBaseChance = 0.2f;

        static constexpr std::uint32_t groupBit (LockGroup group) noexcept
        {
            return 1u << static_cast<unsigned int> (group);
        }

        static MutationPlan plan (std::span<const MutationField> fields,
                                  const LockPolicy::LockSet& locks,
                                  const MutationRequest& request,
                                  std::uint64_t currentRevision);

        /** Folds a value back into [0, 1] by reflection at the boundaries. */
        static float reflectIntoUnit (float value) noexcept;
    };
}
