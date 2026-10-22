#include "MutationPlanner.h"

#include "../Utility/VersionedRandomStreams.h"

#include <algorithm>
#include <cmath>

namespace texture
{
    namespace
    {
        float drawUnit (std::uint64_t seed, std::size_t fieldIndex, std::uint64_t drawIndex) noexcept
        {
            return VersionedRandomStreams::unitFloat (seed, VersionedRandomStreams::Lane::mutation,
                                                      static_cast<std::uint64_t> (fieldIndex), drawIndex);
        }
    }

    float MutationPlanner::reflectIntoUnit (float value) noexcept
    {
        // One reflection covers any step within maxRelativeStep; the guard keeps the
        // loop bounded for a value that is somehow far outside the unit interval.
        for (int guard = 0; guard < 8 && (value < 0.0f || value > 1.0f); ++guard)
        {
            if (value < 0.0f)
                value = -value;

            if (value > 1.0f)
                value = 2.0f - value;
        }

        return std::clamp (value, 0.0f, 1.0f);
    }

    MutationPlan MutationPlanner::plan (std::span<const MutationField> fields,
                                        const LockPolicy::LockSet& locks,
                                        const MutationRequest& request,
                                        std::uint64_t currentRevision)
    {
        MutationPlan result;

        if (fields.size() > maxFields
            || ! std::isfinite (request.strength)
            || request.strength < 0.0f || request.strength > 1.0f)
        {
            result.status = PlanStatus::invalidInput;
            return result;
        }

        for (const auto& field : fields)
        {
            if (! std::isfinite (field.normalised) || field.normalised < 0.0f || field.normalised > 1.0f
                || ((field.kind != FieldKind::continuous) && field.choiceCount < 2))
            {
                result.status = PlanStatus::invalidInput;
                return result;
            }
        }

        if (request.baselineRevision != currentRevision)
        {
            result.status = PlanStatus::staleRevision;
            return result;
        }

        if (request.strength <= 0.0f)
        {
            result.status = PlanStatus::noOp;
            return result;
        }

        result.status = PlanStatus::ok;
        const float strength = request.strength;

        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            const auto& field = fields[index];

            if (! field.eligible || (request.groupMask & groupBit (field.group)) == 0)
                continue;

            const auto isCategorical = field.kind == FieldKind::categorical;

            if (isCategorical && ! request.allowCategorical)
                continue;

            if (locks.isLocked (field.id))
            {
                ++result.lockedSkipped;
                continue;
            }

            const float current = field.normalised;
            float next = current;

            if (isCategorical)
            {
                const float chance = categoricalBaseChance * strength;

                if (drawUnit (request.seed, index, 2) < chance)
                {
                    const auto steps = field.choiceCount - 1;
                    const auto currentChoice = static_cast<int> (std::lround (current * static_cast<float> (steps)));
                    auto pick = static_cast<int> (drawUnit (request.seed, index, 3) * static_cast<float> (steps));
                    pick = std::clamp (pick, 0, steps - 1);

                    if (pick >= currentChoice)
                        ++pick;

                    next = static_cast<float> (pick) / static_cast<float> (steps);
                }
            }
            else
            {
                const float delta = (drawUnit (request.seed, index, 0) + drawUnit (request.seed, index, 1)) - 1.0f;
                next = reflectIntoUnit (current + maxRelativeStep * strength * delta);

                if (field.kind == FieldKind::discrete)
                {
                    const auto steps = static_cast<float> (field.choiceCount - 1);
                    next = std::round (next * steps) / steps;
                }
            }

            if (std::abs (next - current) > 0.0f)
                result.changes.push_back ({ index, next });
        }

        return result;
    }
}
