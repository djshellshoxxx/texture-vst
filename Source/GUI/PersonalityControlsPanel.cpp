#include "PersonalityControlsPanel.h"

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
        constexpr int choiceWidth = 150;

        /** One musical example per style, shown under the selector (F16 Help). */
        const char* exampleFor (int style) noexcept
        {
            switch (style)
            {
                case 1:  return "Cloud: long, soft, wide grains. A slow pad at MORPH 80% becomes a lush bed.";
                case 2:  return "Pulse: a grid that tightens as MORPH rises. A pluck at MORPH 60% shimmers in sixteenths.";
                case 3:  return "Fracture: short, gritty, reversed grains. A vocal at MORPH 90% breaks into glass.";
                case 4:  return "Swarm: detuned, wide grains. One held note at MORPH 70% opens into a chorus.";
                default: return "Legacy: MORPH shapes the grains exactly as before.";
            }
        }
    }

    PersonalityControlsPanel::PersonalityControlsPanel (TextureAudioProcessor& owner, std::function<void()> layoutChanged)
        : processor (owner),
          onLayoutChanged (std::move (layoutChanged)),
          styleBox (owner, params::morphPersonality,
                    juce::StringArray { "Legacy", "Cloud", "Pulse", "Fracture", "Swarm" },
                    "The character MORPH takes: Legacy is the original. Each other style changes the grains, "
                    "and Pulse also puts them on a grid. Switches take effect over 20 ms."),
          intensityKnob (owner, params::personalityIntensity, "Intensity",
                         "How far the style departs from Legacy. Zero is Legacy; one is the full style.")
    {
        header.setButtonText ("Character");
        header.setTooltip ("Show or hide the MORPH personality: the style and its intensity.");
        header.setClickingTogglesState (true);
        header.setToggleState (false, juce::dontSendNotification);
        header.onClick = [this] { if (onLayoutChanged) onLayoutChanged(); };
        addAndMakeVisible (header);

        example.setFont (TextureLookAndFeel::labelFont());
        example.setColour (juce::Label::textColourId, colours::textMuted);
        example.setMinimumHorizontalScale (0.6f);
        addAndMakeVisible (example);

        addAndMakeVisible (styleBox);
        addAndMakeVisible (intensityKnob);
        intensityKnob.setShowsLabel (true);

        refresh();
    }

    PersonalityControlsPanel::~PersonalityControlsPanel() = default;

    int PersonalityControlsPanel::preferredHeight() const
    {
        int height = rowHeight;                                    // header

        if (header.getToggleState())
            height += gap / 2 + rowHeight                          // style selector
                    + gap / 2 + knobHeight                         // intensity
                    + gap / 2 + rowHeight;                         // example

        return height;
    }

    void PersonalityControlsPanel::refresh()
    {
        example.setText (exampleFor (processor.getPersonalityStyle()), juce::dontSendNotification);
    }

    void PersonalityControlsPanel::resized()
    {
        auto area = getLocalBounds();
        header.setBounds (area.removeFromTop (rowHeight).removeFromLeft (120));

        const auto open = header.getToggleState();
        styleBox.setVisible (open);
        intensityKnob.setVisible (open);
        example.setVisible (open);

        if (! open)
            return;

        area.removeFromTop (gap / 2);
        styleBox.setBounds (area.removeFromTop (rowHeight).removeFromLeft (choiceWidth));

        area.removeFromTop (gap / 2);
        intensityKnob.setBounds (area.removeFromTop (knobHeight).removeFromLeft (knobWidth));

        area.removeFromTop (gap / 2);
        example.setBounds (area.removeFromTop (rowHeight));
    }
}
