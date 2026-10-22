#include "CurveEditor.h"

#include "../Modulation/CurveEdit.h"
#include "Colors.h"
#include "LookAndFeel.h"

namespace texture
{
    namespace
    {
        constexpr float pointHitRadius = 8.0f;
        constexpr float keyStepX = 0.01f;
        constexpr float keyStepY = 0.05f;
        constexpr int curveSamples = 128;
    }

    CurveEditor::CurveEditor()
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        setTitle ("LFO curve");
        setDescription ("Curve editor for the MORPH LFO. Drag points, double-click to add, right-click to remove.");
    }

    void CurveEditor::setCurve (const ModulationCurve& newCurve)
    {
        if (dragging >= 0)
            return;

        curve = newCurve;
        selected = juce::jlimit (0, juce::jmax (0, curve.getNumPoints() - 1), selected);
        repaint();
    }

    juce::Rectangle<float> CurveEditor::plotArea() const
    {
        return getLocalBounds().toFloat().reduced (6.0f);
    }

    juce::Point<float> CurveEditor::toPlot (const CurvePoint& point) const
    {
        const auto area = plotArea();
        return { area.getX() + point.x * area.getWidth(),
                 area.getBottom() - (point.y + 1.0f) * 0.5f * area.getHeight() };
    }

    CurvePoint CurveEditor::fromPlot (juce::Point<float> position) const
    {
        const auto area = plotArea();
        const auto x = (position.x - area.getX()) / juce::jmax (1.0f, area.getWidth());
        const auto y = ((area.getBottom() - position.y) / juce::jmax (1.0f, area.getHeight())) * 2.0f - 1.0f;
        return { juce::jlimit (0.0f, 1.0f, x), juce::jlimit (-1.0f, 1.0f, y) };
    }

    int CurveEditor::nearestPoint (juce::Point<float> position) const
    {
        int best = -1;
        float bestDistance = pointHitRadius;

        for (int i = 0; i < curve.getNumPoints(); ++i)
        {
            const auto distance = toPlot (curve.getPoint (i)).getDistanceFrom (position);

            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }

        return best;
    }

    void CurveEditor::commitIfChanged()
    {
        if (! (curve == dragStart) && onCommit)
            onCommit (curve);

        dragStart = curve;
    }

    void CurveEditor::selectNext()
    {
        selected = (selected + 1) % curve.getNumPoints();
        repaint();
    }

    void CurveEditor::paint (juce::Graphics& g)
    {
        const auto area = plotArea();

        g.setColour (colours::panel.darker (0.15f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

        g.setColour (colours::textMuted.withAlpha (0.25f));
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
        for (const float quarter : { 0.25f, 0.5f, 0.75f })
            g.drawVerticalLine (juce::roundToInt (area.getX() + quarter * area.getWidth()), area.getY(), area.getBottom());

        juce::Path path;

        for (int i = 0; i <= curveSamples; ++i)
        {
            const auto phase = static_cast<float> (i) / static_cast<float> (curveSamples);
            const auto point = toPlot ({ phase, curve.evaluate (phase) });

            if (i == 0)
                path.startNewSubPath (point);
            else
                path.lineTo (point);
        }

        g.setColour (colours::accentPrimary);
        g.strokePath (path, juce::PathStrokeType (1.5f));

        for (int i = 0; i < curve.getNumPoints(); ++i)
        {
            const auto center = toPlot (curve.getPoint (i));
            const juce::Rectangle<float> handle (6.0f, 6.0f);
            const auto bounds = handle.withCentre (center);

            g.setColour (i == selected && hasKeyboardFocus (false) ? colours::textPrimary : colours::panel);
            g.fillRect (bounds);
            g.setColour (colours::accentPrimary);
            g.drawRect (bounds, 1.0f);
        }

        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::accentPrimary.withAlpha (0.6f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.0f);
        }
    }

    void CurveEditor::mouseDown (const juce::MouseEvent& event)
    {
        grabKeyboardFocus();
        const auto index = nearestPoint (event.position);

        if (event.mods.isPopupMenu())
        {
            ModulationCurve removed;

            if (index > 0 && curveEdit::removePoint (curve, index, removed))
            {
                dragStart = curve;
                curve = removed;
                selected = juce::jmin (selected, curve.getNumPoints() - 1);
                commitIfChanged();
                repaint();
            }

            return;
        }

        dragging = index;
        changedDuringDrag = false;
        dragStart = curve;

        if (index >= 0)
        {
            selected = index;
            repaint();
        }
    }

    void CurveEditor::mouseDrag (const juce::MouseEvent& event)
    {
        if (dragging < 0)
            return;

        const auto target = fromPlot (event.position);
        ModulationCurve moved;

        if (curveEdit::movePoint (curve, dragging, target.x, target.y, moved) && ! (moved == curve))
        {
            curve = moved;
            changedDuringDrag = true;
            repaint();
        }
    }

    void CurveEditor::mouseUp (const juce::MouseEvent&)
    {
        if (dragging >= 0 && changedDuringDrag)
            commitIfChanged();

        dragging = -1;
        changedDuringDrag = false;
    }

    void CurveEditor::mouseDoubleClick (const juce::MouseEvent& event)
    {
        if (nearestPoint (event.position) >= 0)
            return;

        const auto where = fromPlot (event.position);
        ModulationCurve added;

        if (curveEdit::insertPoint (curve, where.x, where.y, added))
        {
            dragStart = curve;
            curve = added;

            for (int i = 0; i < curve.getNumPoints(); ++i)
                if (juce::exactlyEqual (curve.getPoint (i).x, where.x))
                    selected = i;

            commitIfChanged();
            repaint();
        }
    }

    bool CurveEditor::keyPressed (const juce::KeyPress& key)
    {
        ModulationCurve next;
        bool handled = false;

        if (key == juce::KeyPress::tabKey)
        {
            selectNext();
            return true;
        }

        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            if (curveEdit::removePoint (curve, selected, next))
            {
                dragStart = curve;
                curve = next;
                selected = juce::jmin (selected, curve.getNumPoints() - 1);
                commitIfChanged();
                repaint();
            }
            return true;
        }

        const auto& point = curve.getPoint (selected);
        float x = point.x;
        float y = point.y;

        if (key == juce::KeyPress::leftKey)       { x -= keyStepX; handled = true; }
        else if (key == juce::KeyPress::rightKey) { x += keyStepX; handled = true; }
        else if (key == juce::KeyPress::upKey)    { y += keyStepY; handled = true; }
        else if (key == juce::KeyPress::downKey)  { y -= keyStepY; handled = true; }

        if (handled && curveEdit::movePoint (curve, selected, x, y, next) && ! (next == curve))
        {
            dragStart = curve;
            curve = next;
            commitIfChanged();
            repaint();
        }

        return handled;
    }
}
