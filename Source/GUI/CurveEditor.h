#pragma once

#include "../Modulation/ModulationCurve.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace texture
{
    /** Draws and edits the Curve shape of the MORPH LFO (F14).

        Holds a copy of the curve to show and drags on, and never touches the processor.
        Edits go through the pure operations in CurveEdit.h. A drag or a key press changes
        the copy as you go and reports the result once, on release or key down, through
        onCommit: one edit is one undo step, decided by the owner.

        Mouse: drag a point; double-click empty space to add a point; right-click an
        interior point to remove it. Keyboard: Left/Right/Up/Down move the selected point,
        Tab selects the next point, Delete removes an interior point.
    */
    class CurveEditor final : public juce::Component
    {
    public:
        CurveEditor();

        /** Shows a curve. Ignored while the user is dragging. */
        void setCurve (const ModulationCurve& curve);
        const ModulationCurve& getCurve() const noexcept { return curve; }

        /** Called with the new curve when an edit is complete. */
        std::function<void (const ModulationCurve&)> onCommit;

        void paint (juce::Graphics& g) override;
        void resized() override {}
        void mouseDown (const juce::MouseEvent& event) override;
        void mouseDrag (const juce::MouseEvent& event) override;
        void mouseUp (const juce::MouseEvent& event) override;
        void mouseDoubleClick (const juce::MouseEvent& event) override;
        bool keyPressed (const juce::KeyPress& key) override;

    private:
        juce::Rectangle<float> plotArea() const;
        juce::Point<float> toPlot (const CurvePoint& point) const;
        CurvePoint fromPlot (juce::Point<float> position) const;
        int nearestPoint (juce::Point<float> position) const;
        void commitIfChanged();
        void selectNext();

        ModulationCurve curve;
        ModulationCurve dragStart;
        int selected { 0 };
        int dragging { -1 };
        bool changedDuringDrag { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditor)
    };
}
