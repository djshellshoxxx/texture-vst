#include "RhythmControlsPanel.h"

#include "../PluginProcessor.h"
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
        constexpr int choiceWidth = 120;
    }

    RhythmControlsPanel::RhythmControlsPanel (TextureAudioProcessor& owner, std::function<void()> layoutChanged)
        : processor (owner),
          onLayoutChanged (std::move (layoutChanged)),
          clockBox (owner, params::grainClock, juce::StringArray { "Legacy", "Rhythmic" },
                    "Legacy places grains by MORPH as before. Rhythmic places them on a musical grid. "
                    "Rhythmic grains are not started while the host transport is stopped."),
          divisionBox (owner, params::grainDivision,
                       [] { juce::StringArray names; for (const auto* name : params::grainDivisionNames) names.add (name); return names; }(),
                       "The grid: quarter, eighth, sixteenth or thirty-second notes, or eighth and sixteenth triplets."),
          swingKnob (owner, params::grainSwing, "Swing",
                     "Delays every other grid point. Zero is straight; the maximum is about two-thirds of a step."),
          probabilityKnob (owner, params::grainProbability, "Chance",
                           "The chance that each grid point starts a grain. The draw is repeatable for a seed."),
          gateKnob (owner, params::grainGate, "Gate",
                    "Grain length as a share of its slot. Ignored while Grain Size is set manually."),
          burstKnob (owner, params::grainBurst, "Burst",
                     "Grains per grid point, spread evenly across the interval after it. Up to four."),
          firstGrainToggle (owner, params::grainFirstImmediate, "First grain now",
                            "Start a note's first grain at once instead of waiting for the next grid point."),
          tempoLabel()
    {
        header.setButtonText ("Rhythm (grid)");
        header.setTooltip ("Show or hide the rhythmic grain grid: clock, division, swing, chance, gate, burst.");
        header.setClickingTogglesState (true);
        header.setToggleState (false, juce::dontSendNotification);
        header.onClick = [this] { if (onLayoutChanged) onLayoutChanged(); };
        addAndMakeVisible (header);

        for (auto* label : { &clockLabel, &divisionLabel, &tempoLabel })
        {
            label->setFont (TextureLookAndFeel::labelFont());
            label->setColour (juce::Label::textColourId, colours::textMuted);
            label->setMinimumHorizontalScale (0.7f);
            addAndMakeVisible (*label);
        }

        clockLabel.setText ("Clock", juce::dontSendNotification);
        divisionLabel.setText ("Division", juce::dontSendNotification);

        for (auto* component : std::initializer_list<juce::Component*> { &clockBox, &divisionBox, &swingKnob,
                                                                         &probabilityKnob, &gateKnob, &burstKnob,
                                                                         &firstGrainToggle })
            addAndMakeVisible (*component);

        swingKnob.setShowsLabel (true);
        probabilityKnob.setShowsLabel (true);
        gateKnob.setShowsLabel (true);
        burstKnob.setShowsLabel (true);

        refresh();
    }

    RhythmControlsPanel::~RhythmControlsPanel() = default;

    int RhythmControlsPanel::preferredHeight() const
    {
        int height = rowHeight;                                   // header

        if (header.getToggleState())
            height += gap / 2 + rowHeight * 2                     // clock and division
                    + gap / 2 + knobHeight                        // swing, chance, gate, burst
                    + gap / 2 + rowHeight                         // first-grain toggle
                    + gap / 2 + rowHeight;                        // tempo readout

        return height;
    }

    void RhythmControlsPanel::refresh()
    {
        const auto bpm = juce::roundToInt (processor.getRhythmTempoForGui());
        tempoLabel.setText ("Tempo " + juce::String (bpm) + " BPM: the host's, or 120 when none is sent. "
                            "Legacy ignores these controls.",
                            juce::dontSendNotification);
    }

    void RhythmControlsPanel::resized()
    {
        auto area = getLocalBounds();
        header.setBounds (area.removeFromTop (rowHeight).removeFromLeft (180));

        const auto open = header.getToggleState();

        for (auto* component : std::initializer_list<juce::Component*> { &clockLabel, &clockBox, &divisionLabel,
                                                                         &divisionBox, &swingKnob, &probabilityKnob,
                                                                         &gateKnob, &burstKnob, &firstGrainToggle,
                                                                         &tempoLabel })
            component->setVisible (open);

        if (! open)
            return;

        area.removeFromTop (gap / 2);
        auto clockRow = area.removeFromTop (rowHeight);
        clockLabel.setBounds (clockRow.removeFromLeft (52));
        clockBox.setBounds (clockRow.removeFromLeft (choiceWidth));

        area.removeFromTop (gap / 2);
        auto divisionRow = area.removeFromTop (rowHeight);
        divisionLabel.setBounds (divisionRow.removeFromLeft (52));
        divisionBox.setBounds (divisionRow.removeFromLeft (choiceWidth));

        area.removeFromTop (gap / 2);
        auto knobRow = area.removeFromTop (knobHeight);

        for (auto* knob : std::initializer_list<ParameterKnob*> { &swingKnob, &probabilityKnob, &gateKnob, &burstKnob })
        {
            knob->setBounds (knobRow.removeFromLeft (knobWidth));
            knobRow.removeFromLeft (gap / 2);
        }

        area.removeFromTop (gap / 2);
        firstGrainToggle.setBounds (area.removeFromTop (rowHeight).removeFromLeft (150));

        area.removeFromTop (gap / 2);
        tempoLabel.setBounds (area.removeFromTop (rowHeight));
    }
}
