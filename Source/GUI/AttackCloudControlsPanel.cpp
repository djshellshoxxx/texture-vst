#include "AttackCloudControlsPanel.h"

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
    }

    AttackCloudControlsPanel::AttackCloudControlsPanel (TextureAudioProcessor& owner, std::function<void()> layoutChanged)
        : onLayoutChanged (std::move (layoutChanged)),
          onToggle (owner, params::attackCloudOn, "On",
                    "Plays the first moments of the sample as they are, then fades into the granular cloud."),
          holdKnob (owner, params::attackCloudHoldMs, "Hold",
                    "How long the sample's start plays unchanged before the cloud begins. Zero starts the fade at once."),
          transitionKnob (owner, params::attackCloudTransitionMs, "Fade",
                          "How long the crossfade from the sample's start into the cloud takes."),
          originalKnob (owner, params::attackCloudOriginal, "Level",
                        "How loud the sample's start is while it plays unchanged. Zero leaves only the cloud.")
    {
        header.setButtonText ("Attack to cloud");
        header.setTooltip ("Show or hide attack to cloud: the sample's start, then the granular cloud.");
        header.setClickingTogglesState (true);
        header.setToggleState (false, juce::dontSendNotification);
        header.onClick = [this] { if (onLayoutChanged) onLayoutChanged(); };
        addAndMakeVisible (header);

        note.setFont (TextureLookAndFeel::labelFont());
        note.setColour (juce::Label::textColourId, colours::textMuted);
        note.setMinimumHorizontalScale (0.6f);
        note.setText ("Times and level apply to the next note; a note already sounding keeps its own.",
                      juce::dontSendNotification);
        addAndMakeVisible (note);

        addAndMakeVisible (onToggle);
        addAndMakeVisible (holdKnob);
        addAndMakeVisible (transitionKnob);
        addAndMakeVisible (originalKnob);
        holdKnob.setShowsLabel (true);
        transitionKnob.setShowsLabel (true);
        originalKnob.setShowsLabel (true);
    }

    AttackCloudControlsPanel::~AttackCloudControlsPanel() = default;

    int AttackCloudControlsPanel::preferredHeight() const
    {
        int height = rowHeight;                                    // header

        if (header.getToggleState())
            height += gap / 2 + rowHeight                          // switch
                    + gap / 2 + knobHeight                         // hold, fade, level
                    + gap / 2 + rowHeight;                         // note

        return height;
    }

    void AttackCloudControlsPanel::resized()
    {
        auto area = getLocalBounds();
        header.setBounds (area.removeFromTop (rowHeight).removeFromLeft (180));

        const auto open = header.getToggleState();
        onToggle.setVisible (open);
        holdKnob.setVisible (open);
        transitionKnob.setVisible (open);
        originalKnob.setVisible (open);
        note.setVisible (open);

        if (! open)
            return;

        area.removeFromTop (gap / 2);
        onToggle.setBounds (area.removeFromTop (rowHeight).removeFromLeft (120));

        area.removeFromTop (gap / 2);
        auto knobRow = area.removeFromTop (knobHeight);

        for (auto* knob : std::initializer_list<ParameterKnob*> { &holdKnob, &transitionKnob, &originalKnob })
        {
            knob->setBounds (knobRow.removeFromLeft (knobWidth));
            knobRow.removeFromLeft (gap / 2);
        }

        area.removeFromTop (gap / 2);
        note.setBounds (area.removeFromTop (rowHeight));
    }
}
