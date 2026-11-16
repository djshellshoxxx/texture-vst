#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace texture
{
    /** One point of a user curve, both coordinates normalised. */
    struct CurvePoint
    {
        float x { 0.0f };   // position in the cycle, [0, 1]
        float y { 0.0f };   // value, [-1, 1]
    };

    /** A bounded user shape for the MORPH LFO's Curve mode (F14).

        Plain data: no allocation, no JUCE. Valid curves have 2 to 32 points, sorted by a
        strictly increasing x, with the first at x = 0 and the last at x = 1, and every
        coordinate finite and inside its range. Piecewise linear between points. Nothing is
        extrapolated. The default is a five-point sine-like shape. */
    class ModulationCurve final
    {
    public:
        static constexpr int minPoints = 2;
        static constexpr int maxPoints = 32;

        /** The default shape: one full cycle of a sine-like curve. */
        ModulationCurve() noexcept;

        /** Copies the points if they form a valid curve. Returns false, and leaves `out`
            unchanged, when they do not. Allocation-free. */
        static bool fromPoints (const CurvePoint* points, int count, ModulationCurve& out) noexcept;

        /** Convenience for tests and state parsing (message thread). */
        static bool fromPoints (const std::vector<CurvePoint>& points, ModulationCurve& out);

        /** The point list shape rules, without building a curve. */
        static bool arePointsValid (const CurvePoint* points, int count) noexcept;

        bool isValid() const noexcept { return arePointsValid (points.data(), count); }

        int getNumPoints() const noexcept { return count; }
        const CurvePoint& getPoint (int index) const noexcept;

        /** Value at a cycle position. Positions outside [0, 1] are clamped to the ends. A
            curve that is not valid evaluates to zero. */
        float evaluate (float phase) const noexcept;

        bool operator== (const ModulationCurve& other) const noexcept;
        bool operator!= (const ModulationCurve& other) const noexcept { return ! (*this == other); }

    private:
        std::array<CurvePoint, maxPoints> points {};
        int count { 0 };
    };
}
