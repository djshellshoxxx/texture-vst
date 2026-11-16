#pragma once

#include "ModulationCurve.h"

#include <array>
#include <cmath>

namespace texture::curveEdit
{
    /** Closest two interior points may sit, and the closest a point may come to an endpoint's
        neighbour. Keeps the curve strictly increasing in x under any drag. */
    inline constexpr float minimumSpacing = 0.005f;

    /** Pure editing operations for the Curve shape (F14). Each returns a new curve that
        satisfies the curve rules, or false and leaves `out` unchanged. No GUI, no processor. */

    /** Moves one point. Endpoints keep x = 0 and x = 1; their y is free. An interior point's x
        is held between its neighbours, and y is limited to [-1, 1]. */
    inline bool movePoint (const ModulationCurve& curve, int index, float x, float y, ModulationCurve& out) noexcept
    {
        const auto count = curve.getNumPoints();

        if (index < 0 || index >= count || ! std::isfinite (x) || ! std::isfinite (y))
            return false;

        std::array<CurvePoint, ModulationCurve::maxPoints> points {};

        for (int i = 0; i < count; ++i)
            points[static_cast<std::size_t> (i)] = curve.getPoint (i);

        auto& moved = points[static_cast<std::size_t> (index)];
        moved.y = juce::jlimit (-1.0f, 1.0f, y);

        if (index == 0)
        {
            moved.x = 0.0f;
        }
        else if (index == count - 1)
        {
            moved.x = 1.0f;
        }
        else
        {
            const auto low = points[static_cast<std::size_t> (index - 1)].x + minimumSpacing;
            const auto high = points[static_cast<std::size_t> (index + 1)].x - minimumSpacing;

            if (low > high)
                return false;

            moved.x = juce::jlimit (low, high, x);
        }

        return ModulationCurve::fromPoints (points.data(), count, out);
    }

    /** Adds a point at x, which must lie inside (0, 1) and keep the spacing to its neighbours. */
    inline bool insertPoint (const ModulationCurve& curve, float x, float y, ModulationCurve& out) noexcept
    {
        const auto count = curve.getNumPoints();

        if (count >= ModulationCurve::maxPoints || ! std::isfinite (x) || ! std::isfinite (y)
            || ! (x > 0.0f && x < 1.0f))
            return false;

        int insertAt = 1;

        while (insertAt < count && curve.getPoint (insertAt).x <= x)
            ++insertAt;

        const auto& left = curve.getPoint (insertAt - 1);
        const auto& right = curve.getPoint (insertAt);

        if (x - left.x < minimumSpacing || right.x - x < minimumSpacing)
            return false;

        std::array<CurvePoint, ModulationCurve::maxPoints> points {};
        int target = 0;

        for (int i = 0; i < count; ++i)
        {
            if (i == insertAt)
                points[static_cast<std::size_t> (target++)] = { x, juce::jlimit (-1.0f, 1.0f, y) };

            points[static_cast<std::size_t> (target++)] = curve.getPoint (i);
        }

        return ModulationCurve::fromPoints (points.data(), count + 1, out);
    }

    /** Removes an interior point. The endpoints stay, and a curve keeps at least two points. */
    inline bool removePoint (const ModulationCurve& curve, int index, ModulationCurve& out) noexcept
    {
        const auto count = curve.getNumPoints();

        if (count <= ModulationCurve::minPoints || index <= 0 || index >= count - 1)
            return false;

        std::array<CurvePoint, ModulationCurve::maxPoints> points {};
        int target = 0;

        for (int i = 0; i < count; ++i)
            if (i != index)
                points[static_cast<std::size_t> (target++)] = curve.getPoint (i);

        return ModulationCurve::fromPoints (points.data(), count - 1, out);
    }
}
