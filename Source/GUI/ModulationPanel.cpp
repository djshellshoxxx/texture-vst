#include "ModulationPanel.h"

#include "../PluginProcessor.h"
#include "../Modulation/ModulationCurve.h"
#include "Colors.h"
#include "LookAndFeel.h"

namespace texture
{
    namespace
    {
        constexpr int rowHeight = 22;
        constexpr int gap = 6;
        constexpr int knobWidth = 64;
        constexpr int knobHeight = 84;
        constexpr int curveHeight = 84;
        constexpr int choiceWidth = 150;
    }

    ModulationPanel::ModulationPanel (TextureAudioProcessor& owner, std::function<void()> layoutChanged)
        : processor (owner),
          onLayoutChanged (std::move (layoutChanged)),
          shapeBox (owner, params::lfoShape,
                    juce::StringArray { "Sine", "Triangle", "Step random", "Smooth random", "Curve" },
                    "The MORPH LFO's waveform. Random shapes are repeatable for a given seed. Curve uses the drawn shape. (Pro)"),
          retriggerBox (owner, params::lfoRetrigger,
                        juce::StringArray { "Legacy", "Free", "First note", "Host" },
                        "What restarts the cycle. Legacy follows tempo sync as before. Free never stops. "
                        "First note restarts on the first key of a chord. Host follows the host's position. (Pro)"),
          phaseKnob (owner, params::lfoPhase, "Phase", "Where in the cycle the LFO starts. (Pro)"),
          slewKnob (owner, params::lfoSlewMs, "Slew", "How quickly random and curve steps change at a cycle boundary. (Pro)"),
          curveEditor()
    {
        header.setButtonText ("Motion shapes (Pro)");
        header.setTooltip ("Show or hide the LFO shape, retrigger, phase, slew and curve.");
        header.setClickingTogglesState (true);
        header.setToggleState (false, juce::dontSendNotification);
        header.onClick = [this] { if (onLayoutChanged) onLayoutChanged(); };
        addAndMakeVisible (header);

        for (auto* label : { &shapeLabel, &retriggerLabel, &note })
        {
            label->setFont (TextureLookAndFeel::labelFont());
            label->setColour (juce::Label::textColourId, colours::textMuted);
            label->setMinimumHorizontalScale (0.7f);
            addAndMakeVisible (*label);
        }

        shapeLabel.setText ("Shape", juce::dontSendNotification);
        retriggerLabel.setText ("Retrigger", juce::dontSendNotification);
        note.setText ("The curve: drag a point, double-click to add, right-click to remove. Arrows move a selected point.",
                      juce::dontSendNotification);
        note.setMinimumHorizontalScale (0.6f);

        addAndMakeVisible (shapeBox);
        addAndMakeVisible (retriggerBox);

        phaseKnob.setShowsLabel (true);
        slewKnob.setShowsLabel (true);
        addAndMakeVisible (phaseKnob);
        addAndMakeVisible (slewKnob);

        curveEditor.onCommit = [this] (const ModulationCurve& edited)
        {
            processor.setModulationCurve (edited, "Edit LFO curve");
            refresh();
        };
        addAndMakeVisible (curveEditor);

        resetCurve.setTooltip ("Replace the curve with the default sine-like shape. One undo step.");
        resetCurve.onClick = [this]
        {
            processor.setModulationCurve (ModulationCurve(), "Reset LFO curve");
            refresh();
        };
        addAndMakeVisible (resetCurve);

        refresh();
    }

    ModulationPanel::~ModulationPanel() = default;

    int ModulationPanel::preferredHeight() const
    {
        int height = rowHeight;                                   // header

        if (header.getToggleState())
            height += gap / 2 + rowHeight * 2                     // shape and retrigger rows
                    + gap / 2 + knobHeight                        // phase and slew
                    + gap / 2 + curveHeight                       // curve
                    + gap / 2 + rowHeight * 2;                    // reset and note

        return height;
    }

    void ModulationPanel::refresh()
    {
        curveEditor.setCurve (processor.getModulationCurve());
    }

    void ModulationPanel::resized()
    {
        auto area = getLocalBounds();
        header.setBounds (area.removeFromTop (rowHeight).removeFromLeft (180));

        const auto open = header.getToggleState();

        for (auto* component : std::initializer_list<juce::Component*> { &shapeLabel, &shapeBox, &retriggerLabel,
                                                                         &retriggerBox, &phaseKnob, &slewKnob,
                                                                         &curveEditor, &resetCurve, &note })
            component->setVisible (open);

        if (! open)
            return;

        area.removeFromTop (gap / 2);

        auto shapeRow = area.removeFromTop (rowHeight);
        shapeLabel.setBounds (shapeRow.removeFromLeft (48));
        shapeBox.setBounds (shapeRow.removeFromLeft (choiceWidth));

        area.removeFromTop (gap / 2);
        auto retriggerRow = area.removeFromTop (rowHeight);
        retriggerLabel.setBounds (retriggerRow.removeFromLeft (48));
        retriggerBox.setBounds (retriggerRow.removeFromLeft (choiceWidth));

        area.removeFromTop (gap / 2);
        auto knobRow = area.removeFromTop (knobHeight);
        phaseKnob.setBounds (knobRow.removeFromLeft (knobWidth));
        knobRow.removeFromLeft (gap);
        slewKnob.setBounds (knobRow.removeFromLeft (knobWidth));

        area.removeFromTop (gap / 2);
        curveEditor.setBounds (area.removeFromTop (curveHeight).withWidth (juce::jmin (area.getWidth(), 260)));

        area.removeFromTop (gap / 2);
        auto actionRow = area.removeFromTop (rowHeight);
        resetCurve.setBounds (actionRow.removeFromLeft (96));
        note.setBounds (area.removeFromTop (rowHeight));
    }
}
