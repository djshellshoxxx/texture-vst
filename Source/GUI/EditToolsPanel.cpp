#include "EditToolsPanel.h"

#include "../PluginProcessor.h"
#include "Colors.h"
#include "LookAndFeel.h"

namespace texture
{
    namespace
    {
        constexpr int cardWidth = 340;
        constexpr int cardHeight = 404;
        constexpr int rowHeight = 22;
        constexpr int gap = 4;
        constexpr int pad = 14;

        const char* mutateGroupLabel (LockGroup group)
        {
            return group == LockGroup::toneSpace ? "Tone/Space" : LockPolicy::groupName (group);
        }
    }

    EditToolsPanel::EditToolsPanel (TextureAudioProcessor& owner)
        : processor (owner)
    {
        setWantsKeyboardFocus (true);

        titleLabel.setText ("EDIT LOCKS AND MUTATE", juce::dontSendNotification);
        titleLabel.setFont (TextureLookAndFeel::labelFont());
        titleLabel.setColour (juce::Label::textColourId, colours::textPrimary);
        addAndMakeVisible (titleLabel);

        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        mutateButton.setTooltip ("Nudge the unlocked Grain, Motion and any selected groups "
                                 "around their current values. One press is one undo step.");
        mutateButton.onClick = [this] { runMutate(); };
        addAndMakeVisible (mutateButton);

        strengthLabel.setText ("Strength", juce::dontSendNotification);
        strengthLabel.setFont (TextureLookAndFeel::labelFont());
        strengthLabel.setColour (juce::Label::textColourId, colours::textMuted);
        addAndMakeVisible (strengthLabel);

        strengthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        strengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
        strengthSlider.setRange (0.0, 100.0, 1.0);
        strengthSlider.setTextValueSuffix ("%");
        strengthSlider.setTooltip ("How far Mutate may move each eligible control. Default 15%.");
        strengthSlider.onValueChange = [this] { pushMutateOptions(); };
        addAndMakeVisible (strengthSlider);

        for (std::size_t i = 0; i < mutateGroupToggles.size(); ++i)
        {
            auto& toggle = mutateGroupToggles[i];
            toggle.setButtonText (mutateGroupLabel (mutateGroups[i]));
            toggle.onClick = [this] { pushMutateOptions(); };
            addAndMakeVisible (toggle);
        }

        categoricalToggle.setTooltip ("Also allow occasional jumps for on/off and choice controls "
                                      "(reverb, LFO sync and division). Off by default.");
        categoricalToggle.onClick = [this] { pushMutateOptions(); };
        addAndMakeVisible (categoricalToggle);

        resultLabel.setFont (TextureLookAndFeel::labelFont());
        resultLabel.setColour (juce::Label::textColourId, colours::textMuted);
        addAndMakeVisible (resultLabel);

        locksHeader.setText ("LOCKS", juce::dontSendNotification);
        locksHeader.setFont (TextureLookAndFeel::labelFont());
        locksHeader.setColour (juce::Label::textColourId, colours::textPrimary);
        addAndMakeVisible (locksHeader);

        for (std::size_t i = 0; i < lockGroupButtons.size(); ++i)
        {
            auto& button = lockGroupButtons[i];
            button.onClick = [this, i] { lockGroupClicked (lockGroups[i]); };
            addAndMakeVisible (button);
        }

        unlockAllButton.setTooltip ("Remove every lock. Lock changes can be undone below.");
        unlockAllButton.onClick = [this]
        {
            processor.unlockAllFields();
            refresh();
        };
        addAndMakeVisible (unlockAllButton);

        undoLockButton.onClick = [this]
        {
            processor.undoLockChange();
            refresh();
        };
        addAndMakeVisible (undoLockButton);

        hintLabel.setText ("Locks protect a control from Randomize and Mutate only. Reset, "
                           "automation, MIDI, project restore and undo still change locked controls.",
                           juce::dontSendNotification);
        hintLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
        hintLabel.setColour (juce::Label::textColourId, colours::textMuted);
        hintLabel.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hintLabel);

        refresh();
    }

    EditToolsPanel::~EditToolsPanel() = default;

    void EditToolsPanel::refresh()
    {
        const auto options = processor.getMutateOptions();
        strengthSlider.setValue (static_cast<double> (options.strength) * 100.0, juce::dontSendNotification);

        for (std::size_t i = 0; i < mutateGroupToggles.size(); ++i)
            mutateGroupToggles[i].setToggleState (
                (options.groupMask & MutationPlanner::groupBit (mutateGroups[i])) != 0,
                juce::dontSendNotification);

        categoricalToggle.setToggleState (options.allowCategorical, juce::dontSendNotification);

        for (std::size_t i = 0; i < lockGroupButtons.size(); ++i)
            updateLockButtonText (static_cast<int> (i));

        undoLockButton.setEnabled (processor.canUndoLockChange());
        repaint();
    }

    void EditToolsPanel::updateLockButtonText (int index)
    {
        const auto group = lockGroups[static_cast<std::size_t> (index)];
        const auto state = processor.getGroupLockState (group);
        const auto total = LockPolicy::countFieldsIn (group);

        juce::String status = "Unlocked";

        if (state == LockPolicy::GroupState::all)
        {
            status = "Locked";
        }
        else if (state == LockPolicy::GroupState::some)
        {
            int lockedInGroup = 0;
            for (const auto& field : LockPolicy::fields())
                if (field.group == group && processor.isFieldLocked (juce::String (std::string (field.id))))
                    ++lockedInGroup;

            status = "Mixed: " + juce::String (lockedInGroup) + " of " + juce::String (total) + " locked";
        }

        lockGroupButtons[static_cast<std::size_t> (index)].setButtonText (
            juce::String (LockPolicy::groupName (group)) + "   " + status);
        lockGroupButtons[static_cast<std::size_t> (index)].setTooltip (
            state == LockPolicy::GroupState::all ? "Unlock every control in this group."
                                                 : "Lock every control in this group.");
    }

    void EditToolsPanel::lockGroupClicked (LockGroup group)
    {
        // A mixed group locks completely first, so one click never leaves it ambiguous.
        const auto shouldLock = processor.getGroupLockState (group) != LockPolicy::GroupState::all;
        processor.setGroupLocked (group, shouldLock);
        refresh();
    }

    void EditToolsPanel::pushMutateOptions()
    {
        TextureAudioProcessor::MutateOptions options;
        options.strength = static_cast<float> (strengthSlider.getValue() / 100.0);

        std::uint32_t mask = 0;
        for (std::size_t i = 0; i < mutateGroupToggles.size(); ++i)
            if (mutateGroupToggles[i].getToggleState())
                mask |= MutationPlanner::groupBit (mutateGroups[i]);

        options.groupMask = mask;
        options.allowCategorical = categoricalToggle.getToggleState();
        processor.setMutateOptions (options);
    }

    void EditToolsPanel::runMutate()
    {
        pushMutateOptions();
        const auto result = processor.mutateParameters();
        resultLabel.setText (result.message, juce::dontSendNotification);
        refresh();
    }

    void EditToolsPanel::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background.withAlpha (0.82f));

        const auto card = getLocalBounds().withSizeKeepingCentre (cardWidth, cardHeight);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (card.toFloat(), 6.0f);
        g.setColour (colours::textMuted.withAlpha (0.4f));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void EditToolsPanel::resized()
    {
        const auto card = getLocalBounds().withSizeKeepingCentre (cardWidth, cardHeight).reduced (pad);

        auto area = card;

        auto titleRow = area.removeFromTop (rowHeight);
        closeButton.setBounds (titleRow.removeFromRight (56));
        titleLabel.setBounds (titleRow);
        area.removeFromTop (gap * 2);

        auto mutateRow = area.removeFromTop (28);
        mutateButton.setBounds (mutateRow.removeFromLeft (84).reduced (0, 2));
        mutateRow.removeFromLeft (gap * 2);
        strengthLabel.setBounds (mutateRow.removeFromLeft (56));
        strengthSlider.setBounds (mutateRow);
        area.removeFromTop (gap);

        for (int row = 0; row < 2; ++row)
        {
            auto groupRow = area.removeFromTop (rowHeight);
            for (int column = 0; column < 2; ++column)
                mutateGroupToggles[static_cast<std::size_t> (row * 2 + column)]
                    .setBounds (groupRow.removeFromLeft (card.getWidth() / 2).reduced (0, 1));
        }

        categoricalToggle.setBounds (area.removeFromTop (rowHeight));
        resultLabel.setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (gap * 2);

        locksHeader.setBounds (area.removeFromTop (rowHeight));

        for (auto& button : lockGroupButtons)
            button.setBounds (area.removeFromTop (rowHeight).reduced (0, 1));

        area.removeFromTop (gap);
        auto buttonRow = area.removeFromTop (rowHeight);
        unlockAllButton.setBounds (buttonRow.removeFromLeft (buttonRow.getWidth() / 2).reduced (0, 1));
        undoLockButton.setBounds (buttonRow.reduced (0, 1));

        area.removeFromTop (gap);
        hintLabel.setBounds (area);
    }

    bool EditToolsPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onClose)
                onClose();
            return true;
        }

        return false;
    }
}
