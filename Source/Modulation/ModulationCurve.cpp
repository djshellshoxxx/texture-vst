#include "ModulationCurve.h"

#include <cmath>

namespace texture
{
    namespace
    {
        constexpr float maxAbsY = 1.0f;

        bool inRange (float value, float low, float high) noexcept
        {
            return std::isfinite (value) && value >= low && value <= high;
        }

        bool isExactly (float value, float target) noexcept
        {
            return ! (value < target) && ! (value > target);
        }
    }

    ModulationCurve::ModulationCurve() noexcept
    {
        // Sine-like: 0 up to +1 at a quarter, back through 0 to -1 at three quarters.
        const std::array<CurvePoint, 5> defaults { { { 0.0f, 0.0f }, { 0.25f, 1.0f }, { 0.5f, 0.0f },
                                                     { 0.75f, -1.0f }, { 1.0f, 0.0f } } };
        for (std::size_t i = 0; i < defaults.size(); ++i)
            points[i] = defaults[i];

        count = static_cast<int> (defaults.size());
    }

    bool ModulationCurve::arePointsValid (const CurvePoint* values, int pointCount) noexcept
    {
        if (values == nullptr || pointCount < minPoints || pointCount > maxPoints)
            return false;

        for (int i = 0; i < pointCount; ++i)
        {
            const auto& point = values[i];

            if (! inRange (point.x, 0.0f, 1.0f) || ! inRange (point.y, -maxAbsY, maxAbsY))
                return false;

            if (i > 0 && ! (point.x > values[i - 1].x))
                return false;   // sorted and no duplicate positions
        }

        return isExactly (values[0].x, 0.0f) && isExactly (values[pointCount - 1].x, 1.0f);
    }

    bool ModulationCurve::fromPoints (const CurvePoint* values, int pointCount, ModulationCurve& out) noexcept
    {
        if (! arePointsValid (values, pointCount))
            return false;

        ModulationCurve result;
        result.count = pointCount;

        for (int i = 0; i < pointCount; ++i)
            result.points[static_cast<std::size_t> (i)] = values[i];

        out = result;
        return true;
    }

    bool ModulationCurve::fromPoints (const std::vector<CurvePoint>& values, ModulationCurve& out)
    {
        return fromPoints (values.data(), static_cast<int> (values.size()), out);
    }

    const CurvePoint& ModulationCurve::getPoint (int index) const noexcept
    {
        const auto clamped = count > 0 ? (index < 0 ? 0 : (index >= count ? count - 1 : index)) : 0;
        return points[static_cast<std::size_t> (clamped)];
    }

    float ModulationCurve::evaluate (float phase) const noexcept
    {
        if (! isValid())
            return 0.0f;

        if (! std::isfinite (phase) || phase <= 0.0f)
            return points[0].y;

        if (phase >= 1.0f)
            return points[static_cast<std::size_t> (count - 1)].y;

        for (int i = 0; i + 1 < count; ++i)
        {
            const auto& left = points[static_cast<std::size_t> (i)];
            const auto& right = points[static_cast<std::size_t> (i + 1)];

            if (phase <= right.x)
            {
                const auto t = (phase - left.x) / (right.x - left.x);
                return left.y + (right.y - left.y) * t;
            }
        }

        return points[static_cast<std::size_t> (count - 1)].y;
    }

    bool ModulationCurve::operator== (const ModulationCurve& other) const noexcept
    {
        if (count != other.count)
            return false;

        for (int i = 0; i < count; ++i)
        {
            const auto& a = points[static_cast<std::size_t> (i)];
            const auto& b = other.points[static_cast<std::size_t> (i)];

            if (! isExactly (a.x, b.x) || ! isExactly (a.y, b.y))
                return false;
        }

        return true;
    }
}
