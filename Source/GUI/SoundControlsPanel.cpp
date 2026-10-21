#include "SoundControlsPanel.h"

#include "../PluginProcessor.h"
#include "Colors.h"
#include "LookAndFeel.h"

namespace texture
{
    namespace
    {
        constexpr int cardWidth = 392;
        constexpr int cardMaxHeight = 452;
        constexpr int rowHeight = 22;
        constexpr int gap = 6;
        constexpr int pad = 14;
        constexpr int knobWidth = 64;
        constexpr int knobHeight = 84;
        constexpr int scrollbarAllowance = 12;
    }

    /** The scrolling content: a mode selector, then collapsible Tone and Space sections.
        Every height below comes from the same arithmetic as resized(), so the viewport
        always knows how tall the content is. */
    class SoundControlsPanel::Body final : public juce::Component
    {
    public:
        Body (TextureAudioProcessor& owner, std::function<void()> layoutChanged)
            : processor (owner),
              onLayoutChanged (std::move (layoutChanged)),
              modeBox (owner, params::toneSpaceMode,
                       juce::StringArray { "Follow MORPH", "Independent" },
                       "Follow MORPH: the MORPH knob sets the tone and space effects. "
                       "Independent: these controls set them, and MORPH still moves the grains."),
              startButton ("Start from current sound"),
              hpKnob (owner, params::toneHpHz, "High-pass", "Removes content below this frequency. Needs the filter on."),
              lpKnob (owner, params::toneLpHz, "Low-pass", "Removes content above this frequency. Needs the filter on."),
              tiltKnob (owner, params::toneTiltDb, "Tilt", "High-shelf tilt in dB. Positive brightens, negative darkens."),
              driveKnob (owner, params::toneDrive, "Drive", "Saturation amount. 10% matches the MORPH default."),
              filterToggle (owner, params::toneFilterOn, "Tone filter on",
                            "Enables the high-pass and low-pass before the saturator."),
              widthKnob (owner, params::spaceWidth, "Width", "Stereo width. 0% sums to mono, 100% leaves the image alone."),
              reverbKnob (owner, params::spaceReverb, "Reverb", "Reverb send. The Reverb switch still gates it."),
              chorusKnob (owner, params::spaceChorus, "Chorus", "Chorus depth.")
        {
            for (auto* header : { &toneHeader, &spaceHeader })
            {
                header->setClickingTogglesState (true);
                header->setToggleState (true, juce::dontSendNotification);
                header->onClick = [this] { if (onLayoutChanged) onLayoutChanged(); };
                addAndMakeVisible (*header);
            }

            toneHeader.setButtonText ("Tone");
            toneHeader.setTooltip ("Show or hide the tone controls.");
            spaceHeader.setButtonText ("Space");
            spaceHeader.setTooltip ("Show or hide the space controls.");

            modeLabel.setText ("Mode", juce::dontSendNotification);
            modeLabel.setFont (TextureLookAndFeel::labelFont());
            modeLabel.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (modeLabel);
            addAndMakeVisible (modeBox);

            statusLabel.setFont (TextureLookAndFeel::labelFont());
            statusLabel.setMinimumHorizontalScale (0.8f);
            addAndMakeVisible (statusLabel);

            startButton.setTooltip ("Switch to Independent without a jump: copies the current MORPH-derived "
                                    "drive, chorus, reverb and tilt into these controls. One undo step.");
            startButton.onClick = [this] { processor.startIndependentFromCurrentSound(); };
            addAndMakeVisible (startButton);

            for (auto* knob : { &hpKnob, &lpKnob, &tiltKnob, &driveKnob, &widthKnob, &reverbKnob, &chorusKnob })
            {
                knob->setShowsLabel (true);
                addAndMakeVisible (*knob);
            }

            addAndMakeVisible (filterToggle);
        }

        /** Enables the controls that apply and states which source is in charge. */
        void refresh()
        {
            const auto* raw = processor.getValueTreeState().getRawParameterValue (params::toneSpaceMode);
            const auto independent = raw != nullptr && raw->load() > 0.5f;

            startButton.setEnabled (! independent);
            statusLabel.setText (independent
                                     ? "Independent: these controls set tone and space. MORPH still moves the grains."
                                     : "Follow MORPH: MORPH sets tone and space. Values below are kept for Independent.",
                                 juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId,
                                   independent ? colours::accentPrimary : colours::textMuted);
        }

        int preferredHeight() const
        {
            int height = rowHeight + gap                       // mode
                       + rowHeight + gap / 2                   // status
                       + rowHeight + gap;                      // start button
            height += rowHeight;                               // tone header
            if (toneHeader.getToggleState())
                height += knobHeight + rowHeight;              // knobs and filter switch
            height += gap + rowHeight;                         // space header
            if (spaceHeader.getToggleState())
                height += knobHeight;
            return height;
        }

        void resized() override
        {
            auto area = getLocalBounds();

            auto modeRow = area.removeFromTop (rowHeight);
            modeLabel.setBounds (modeRow.removeFromLeft (44));
            modeBox.setBounds (modeRow.removeFromLeft (150));
            area.removeFromTop (gap);

            statusLabel.setBounds (area.removeFromTop (rowHeight));
            area.removeFromTop (gap / 2);
            startButton.setBounds (area.removeFromTop (rowHeight).removeFromLeft (180));
            area.removeFromTop (gap);

            const auto toneOpen = toneHeader.getToggleState();
            toneHeader.setBounds (area.removeFromTop (rowHeight).removeFromLeft (120));
            layoutKnobs (area.removeFromTop (toneOpen ? knobHeight : 0),
                         { &hpKnob, &lpKnob, &tiltKnob, &driveKnob }, toneOpen);

            filterToggle.setVisible (toneOpen);
            if (toneOpen)
                filterToggle.setBounds (area.removeFromTop (rowHeight).removeFromLeft (180));

            area.removeFromTop (gap);
            const auto spaceOpen = spaceHeader.getToggleState();
            spaceHeader.setBounds (area.removeFromTop (rowHeight).removeFromLeft (120));
            layoutKnobs (area.removeFromTop (spaceOpen ? knobHeight : 0),
                         { &widthKnob, &reverbKnob, &chorusKnob }, spaceOpen);
        }

    private:
        static void layoutKnobs (juce::Rectangle<int> row, std::initializer_list<ParameterKnob*> knobs, bool visible)
        {
            for (auto* knob : knobs)
            {
                knob->setVisible (visible);

                if (visible)
                    knob->setBounds (row.removeFromLeft (knobWidth));
            }
        }

        TextureAudioProcessor& processor;
        std::function<void()> onLayoutChanged;

        juce::TextButton toneHeader;
        juce::TextButton spaceHeader;
        juce::Label modeLabel;
        ParameterChoiceBox modeBox;
        juce::Label statusLabel;
        juce::TextButton startButton;

        ParameterKnob hpKnob;
        ParameterKnob lpKnob;
        ParameterKnob tiltKnob;
        ParameterKnob driveKnob;
        ParameterToggle filterToggle;
        ParameterKnob widthKnob;
        ParameterKnob reverbKnob;
        ParameterKnob chorusKnob;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Body)
    };

    SoundControlsPanel::SoundControlsPanel (TextureAudioProcessor& owner)
        : processor (owner)
    {
        setWantsKeyboardFocus (true);

        titleLabel.setText ("TONE AND SPACE", juce::dontSendNotification);
        titleLabel.setFont (TextureLookAndFeel::labelFont());
        titleLabel.setColour (juce::Label::textColourId, colours::textPrimary);
        addAndMakeVisible (titleLabel);

        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        body = std::make_unique<Body> (owner, [this] { resized(); });
        viewport.setViewedComponent (body.get(), false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        startTimerHz (8);
    }

    SoundControlsPanel::~SoundControlsPanel()
    {
        stopTimer();
    }

    void SoundControlsPanel::refresh()
    {
        if (body != nullptr)
            body->refresh();
    }

    void SoundControlsPanel::timerCallback()
    {
        // Keeps the status line honest when the mode changes from the host or a preset.
        if (isVisible() && body != nullptr)
            body->refresh();
    }

    void SoundControlsPanel::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background.withAlpha (0.82f));

        const auto card = getLocalBounds().withSizeKeepingCentre (cardWidth, juce::jmin (cardMaxHeight, getHeight() - 16));
        g.setColour (colours::panel);
        g.fillRoundedRectangle (card.toFloat(), 6.0f);
        g.setColour (colours::textMuted.withAlpha (0.4f));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void SoundControlsPanel::resized()
    {
        const auto card = getLocalBounds().withSizeKeepingCentre (cardWidth, juce::jmin (cardMaxHeight, getHeight() - 16))
                                          .reduced (pad);
        auto area = card;

        auto titleRow = area.removeFromTop (rowHeight);
        closeButton.setBounds (titleRow.removeFromRight (56));
        titleLabel.setBounds (titleRow);
        area.removeFromTop (gap * 2);

        viewport.setBounds (area);

        if (body != nullptr)
            body->setSize (area.getWidth() - scrollbarAllowance, body->preferredHeight());
    }

    bool SoundControlsPanel::keyPressed (const juce::KeyPress& key)
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
