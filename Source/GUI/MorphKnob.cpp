#include "MorphKnob.h"

#include "../PluginProcessor.h"
#include "LookAndFeel.h"

#include <cmath>

namespace texture
{
    namespace
    {
        constexpr float ringThickness = 8.0f;
        constexpr float ringGap = 6.0f;
    }

    MorphKnob::MorphKnob (TextureAudioProcessor& processorToUse)
        : ParameterKnob (processorToUse, params::morph, "MORPH",
                         "MORPH transforms the sound from clean playback at 0% to a fully "
                         "granular ambient wash at 100%. Drag vertically; Shift-drag is coarse, "
                         "Ctrl/Cmd-drag is ultra-fine. Double-click resets, Ctrl/Cmd-click "
                         "steps 0 / 35 / 70 / 100%, right-click for more.")
    {
        setShowsLabel (false);

        // Ctrl/Cmd+click cycles the four landmark positions (06_MORPH_KNOB_BEHAVIOUR.md).
        setCoarseSteps ({ 0.0, 0.35, 0.7, 1.0 });
    }

    void MorphKnob::setModulatedValue (float value)
    {
        const auto clamped = juce::jlimit (0.0f, 1.0f, value);

        if (std::abs (clamped - modulatedValue) < 0.002f)
            return;

        modulatedValue = clamped;

        if (modulationVisible)
            repaint();
    }

    void MorphKnob::setModulationVisible (bool shouldBeVisible)
    {
        if (modulationVisible == shouldBeVisible)
            return;

        modulationVisible = shouldBeVisible;
        repaint();
    }

    void MorphKnob::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        // Reserve the bottom strip for the percentage readout.
        const auto readoutHeight = 30.0f;
        auto dialArea = bounds.withTrimmedBottom (readoutHeight);

        const auto diameter = juce::jmin (dialArea.getWidth(), dialArea.getHeight()) - 4.0f;

        if (diameter <= 8.0f)
            return;

        const auto centre = dialArea.getCentre();
        const auto value = getDisplayedProportion();
        const auto hovered = isEnabled() && isMouseOverOrDragging();
        const auto accent = hovered ? colours::morphRing (value).brighter (0.08f) : colours::morphRing (value);

        const auto ringRadius = diameter * 0.5f - ringThickness * 0.5f;
        const auto bodyRadius = ringRadius - ringGap - ringThickness * 0.5f;

        const auto startAngle = TextureLookAndFeel::rotaryStartAngle;
        const auto endAngle = TextureLookAndFeel::rotaryEndAngle;
        const auto angle = startAngle + value * (endAngle - startAngle);

        // --- Unfilled ring ----------------------------------------------------
        {
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                                 startAngle, endAngle, true);

            g.setColour (colours::panelEdge.withAlpha (0.6f));
            g.strokePath (track, juce::PathStrokeType (ringThickness, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // --- LFO ghost ring ---------------------------------------------------
        if (modulationVisible)
        {
            const auto modAngle = startAngle + modulatedValue * (endAngle - startAngle);

            juce::Path ghost;
            ghost.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                                 juce::jmin (angle, modAngle), juce::jmax (angle, modAngle), true);

            g.setColour (colours::accentPro.withAlpha (0.55f));
            g.strokePath (ghost, juce::PathStrokeType (ringThickness * 0.5f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // --- Filled ring, cool blue to warm magenta ---------------------------
        if (value > 0.0005f)
        {
            juce::Path fill;
            fill.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                                startAngle, angle, true);

            juce::ColourGradient gradient (colours::accentPrimary,
                                           centre.x - ringRadius, centre.y,
                                           colours::accentSecondary,
                                           centre.x + ringRadius, centre.y,
                                           false);

            g.setGradientFill (gradient);
            g.strokePath (fill, juce::PathStrokeType (ringThickness, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }

        // --- Body -------------------------------------------------------------
        const juce::Rectangle<float> body (centre.x - bodyRadius, centre.y - bodyRadius,
                                           bodyRadius * 2.0f, bodyRadius * 2.0f);

        g.setColour (colours::shadow);
        g.fillEllipse (body.translated (0.0f, 2.0f).expanded (1.0f));

        g.setGradientFill (juce::ColourGradient (hovered ? colours::knobTop.brighter (0.08f) : colours::knobTop,
                                                 body.getCentreX(), body.getY(),
                                                 hovered ? colours::knobBottom.brighter (0.08f) : colours::knobBottom,
                                                 body.getCentreX(), body.getBottom(),
                                                 false));
        g.fillEllipse (body);

        g.setColour (colours::panelEdge);
        g.drawEllipse (body.reduced (0.5f), 1.0f);

        // --- Indicator ---------------------------------------------------------
        {
            const auto inner = bodyRadius * 0.55f;
            const auto outer = bodyRadius * 0.9f;

            const juce::Point<float> from (centre.x + std::sin (angle) * inner,
                                           centre.y - std::cos (angle) * inner);
            const juce::Point<float> to (centre.x + std::sin (angle) * outer,
                                         centre.y - std::cos (angle) * outer);

            g.setColour (accent);
            g.drawLine ({ from, to }, 3.0f);
        }

        // --- Centre label ------------------------------------------------------
        g.setColour (colours::textPrimary);
        g.setFont (TextureLookAndFeel::interfaceFont (14.0f));
        g.drawText ("MORPH", body, juce::Justification::centred, false);

        // --- Percentage readout ------------------------------------------------
        g.setColour (accent);
        g.setFont (TextureLookAndFeel::interfaceFont (18.0f, true));
        paintLearnRing (g, dialArea);

        g.drawText (juce::String (juce::roundToInt (static_cast<float> (valueToProportionOfLength (getValue())) * 100.0f)) + "%",
                    bounds.removeFromBottom (readoutHeight),
                    juce::Justification::centredTop,
                    false);
    }
}
