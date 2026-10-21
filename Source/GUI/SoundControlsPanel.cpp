#include "SoundControlsPanel.h"

#include "../PluginProcessor.h"
#include "Colors.h"
#include "GrainControlsPanel.h"
#include "LookAndFeel.h"
#include "ModulationPanel.h"
#include "ScanPanel.h"

namespace texture
{
    namespace
    {
        /** One line of the variation list: a label, Star (save as preset) and Pin. */
        struct VariationRow final : public juce::Component
        {
            VariationRow()
            {
                label.setFont (TextureLookAndFeel::labelFont());
                label.setColour (juce::Label::textColourId, colours::textPrimary);
                label.setMinimumHorizontalScale (0.75f);
                addAndMakeVisible (label);

                star.setTooltip ("Save this variation as a preset. Opens the normal Save dialog.");
                addAndMakeVisible (star);

                pin.setClickingTogglesState (true);
                pin.setTooltip ("Keep this variation when the history is full. Up to eight.");
                addAndMakeVisible (pin);
            }

            void resized() override
            {
                auto area = getLocalBounds();
                pin.setBounds (area.removeFromRight (44).reduced (0, 1));
                area.removeFromRight (4);
                star.setBounds (area.removeFromRight (44).reduced (0, 1));
                area.removeFromRight (4);
                label.setBounds (area);
            }

            juce::Label label;
            juce::TextButton star { "Star" };
            juce::TextButton pin { "Pin" };
        };

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
        Body (TextureAudioProcessor& owner, std::function<void()> layoutChanged,
              std::function<void (std::size_t)> starVariation)
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
              chorusKnob (owner, params::spaceChorus, "Chorus", "Chorus depth."),
              voiceBox (owner, params::voiceMode, juce::StringArray { "Poly", "Mono retrigger", "Mono legato" },
                        "Poly: every key has its own voice. Mono retrigger: one voice that restarts on each key. "
                        "Mono legato: one voice that keeps playing through overlapping keys."),
              priorityBox (owner, params::notePriority, juce::StringArray { "Last", "High", "Low" },
                           "Which held key the mono line sounds: the most recent, the highest or the lowest."),
              glideKnob (owner, params::glideMs, "Glide", "Pitch slide time between notes in mono modes. Off at zero."),
              glideModeBox (owner, params::glideMode, juce::StringArray { "Always", "Overlap only" },
                            "Always: glide on every mono note. Overlap only: glide only when a key lands on a held one."),
              grainPanel (owner, [this] { if (onLayoutChanged) onLayoutChanged(); }),
              modulationPanel (owner, [this] { if (onLayoutChanged) onLayoutChanged(); }),
              scanPanel (owner, [this] { if (onLayoutChanged) onLayoutChanged(); })
        {
            addAndMakeVisible (grainPanel);
            addAndMakeVisible (modulationPanel);
            addAndMakeVisible (scanPanel);

            for (auto* header : { &toneHeader, &spaceHeader, &performanceHeader })
            {
                header->setClickingTogglesState (true);
                header->setToggleState (true, juce::dontSendNotification);
                header->onClick = [this] { if (onLayoutChanged) onLayoutChanged(); };
                addAndMakeVisible (*header);
            }

            onStarVariation = std::move (starVariation);

            toneHeader.setButtonText ("Tone");
            toneHeader.setTooltip ("Show or hide the tone controls.");
            spaceHeader.setButtonText ("Space");
            spaceHeader.setTooltip ("Show or hide the space controls.");
            performanceHeader.setButtonText ("Performance");
            performanceHeader.setTooltip ("Show or hide the voice and glide controls.");

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

            variationsHeader.setButtonText ("Variations");
            variationsHeader.setTooltip ("Show or hide the variation history: Randomize and Mutate results.");
            variationsHeader.setClickingTogglesState (true);
            variationsHeader.setToggleState (true, juce::dontSendNotification);
            variationsHeader.onClick = [this] { if (onLayoutChanged) onLayoutChanged(); };
            addAndMakeVisible (variationsHeader);

            previousButton.setTooltip ("Preview the next older variation.");
            previousButton.onClick = [this] { processor.previousVariation(); refreshVariations (true); };
            addAndMakeVisible (previousButton);

            nextButton.setTooltip ("Preview the next newer variation.");
            nextButton.onClick = [this] { processor.nextVariation(); refreshVariations (true); };
            addAndMakeVisible (nextButton);

            acceptButton.setTooltip ("Keep the previewed sound. One undo step.");
            acceptButton.onClick = [this] { processor.acceptVariation(); refreshVariations (true); };
            addAndMakeVisible (acceptButton);

            cancelButton.setTooltip ("Return to the sound from before the preview.");
            cancelButton.onClick = [this] { processor.cancelVariation(); refreshVariations (true); };
            addAndMakeVisible (cancelButton);

            clearButton.setTooltip ("Forget every variation. The sound you hear is kept.");
            clearButton.onClick = [this] { processor.clearVariations(); refreshVariations (true); };
            addAndMakeVisible (clearButton);

            variationStatus.setFont (TextureLookAndFeel::labelFont());
            variationStatus.setColour (juce::Label::textColourId, colours::textMuted);
            variationStatus.setMinimumHorizontalScale (0.7f);
            addAndMakeVisible (variationStatus);

            mediaLabel.setFont (TextureLookAndFeel::labelFont());
            mediaLabel.setColour (juce::Label::textColourId, colours::textMuted);
            mediaLabel.setMinimumHorizontalScale (0.7f);
            addAndMakeVisible (mediaLabel);

            listHeader.setButtonText ("History");
            listHeader.setTooltip ("Show or hide the list of variations.");
            listHeader.setClickingTogglesState (true);
            listHeader.setToggleState (false, juce::dontSendNotification);
            listHeader.onClick = [this] { refreshVariations (true); };
            addAndMakeVisible (listHeader);

            for (auto& row : rows)
            {
                row = std::make_unique<VariationRow>();
                row->setVisible (false);
                addChildComponent (*row);
            }

            refreshVariations (false);

            for (auto* label : { &voiceLabel, &priorityLabel, &glideModeLabel })
            {
                label->setFont (TextureLookAndFeel::labelFont());
                label->setColour (juce::Label::textColourId, colours::textMuted);
                addAndMakeVisible (*label);
            }

            voiceLabel.setText ("Voice", juce::dontSendNotification);
            priorityLabel.setText ("Priority", juce::dontSendNotification);
            glideModeLabel.setText ("Glide mode", juce::dontSendNotification);
            glideKnob.setShowsLabel (true);
            addAndMakeVisible (voiceBox);
            addAndMakeVisible (priorityBox);
            addAndMakeVisible (glideKnob);
            addAndMakeVisible (glideModeBox);
        }

        /** Enables the controls that apply and states which source is in charge. */
        void refresh()
        {
            grainPanel.refresh();
            modulationPanel.refresh();

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
            height += gap + grainPanel.preferredHeight();      // F13 grains, collapsed by default
            height += gap + modulationPanel.preferredHeight(); // F14 motion shapes, collapsed by default
            height += gap + scanPanel.preferredHeight();       // F10 scan and freeze, collapsed by default
            height += gap + rowHeight;                         // performance header
            if (performanceHeader.getToggleState())
                height += rowHeight + gap / 2 + knobHeight;    // mode and priority, glide row

            height += gap + rowHeight;                         // variations header
            if (variationsHeader.getToggleState())
            {
                height += rowHeight * 3;                       // navigation, actions, media
                height += gap + rowHeight;                     // history header
                if (listHeader.getToggleState())
                    height += rowHeight * static_cast<int> (juce::jmin (processor.getVariations().size(), rowCount));
            }
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

            area.removeFromTop (gap);
            grainPanel.setBounds (area.removeFromTop (grainPanel.preferredHeight()));

            area.removeFromTop (gap);
            modulationPanel.setBounds (area.removeFromTop (modulationPanel.preferredHeight()));

            area.removeFromTop (gap);
            scanPanel.setBounds (area.removeFromTop (scanPanel.preferredHeight()));

            area.removeFromTop (gap);
            const auto performanceOpen = performanceHeader.getToggleState();
            performanceHeader.setBounds (area.removeFromTop (rowHeight).removeFromLeft (120));

            auto modeRowArea = area.removeFromTop (performanceOpen ? rowHeight : 0);
            auto glideRowArea = area.removeFromTop (performanceOpen ? knobHeight : 0);

            for (auto* component : std::initializer_list<juce::Component*> { &voiceLabel, &voiceBox, &priorityLabel, &priorityBox })
                component->setVisible (performanceOpen);

            if (performanceOpen)
            {
                auto row = modeRowArea;
                voiceLabel.setBounds (row.removeFromLeft (44));
                voiceBox.setBounds (row.removeFromLeft (140));
                row.removeFromLeft (gap);
                priorityLabel.setBounds (row.removeFromLeft (54));
                priorityBox.setBounds (row.removeFromLeft (110));
            }

            glideKnob.setVisible (performanceOpen);
            glideModeLabel.setVisible (performanceOpen);
            glideModeBox.setVisible (performanceOpen);

            if (performanceOpen)
            {
                glideKnob.setBounds (glideRowArea.removeFromLeft (knobWidth));
                glideRowArea.removeFromLeft (gap);
                glideModeLabel.setBounds (glideRowArea.removeFromLeft (70));
                glideModeBox.setBounds (glideRowArea.removeFromLeft (140));
            }

            area.removeFromTop (gap);
            const auto variationsOpen = variationsHeader.getToggleState();
            variationsHeader.setBounds (area.removeFromTop (rowHeight).removeFromLeft (120));

            auto navigationRow = area.removeFromTop (variationsOpen ? rowHeight : 0);
            auto actionRow = area.removeFromTop (variationsOpen ? rowHeight : 0);
            auto mediaRow = area.removeFromTop (variationsOpen ? rowHeight : 0);

            previousButton.setVisible (variationsOpen);
            nextButton.setVisible (variationsOpen);
            variationStatus.setVisible (variationsOpen);
            acceptButton.setVisible (variationsOpen && processor.isPreviewingVariation());
            cancelButton.setVisible (variationsOpen && processor.isPreviewingVariation());
            clearButton.setVisible (variationsOpen);
            mediaLabel.setVisible (variationsOpen);

            if (variationsOpen)
            {
                previousButton.setBounds (navigationRow.removeFromLeft (76));
                navigationRow.removeFromLeft (4);
                nextButton.setBounds (navigationRow.removeFromLeft (76));
                navigationRow.removeFromLeft (8);
                variationStatus.setBounds (navigationRow);

                if (processor.isPreviewingVariation())
                {
                    acceptButton.setBounds (actionRow.removeFromLeft (76));
                    actionRow.removeFromLeft (4);
                    cancelButton.setBounds (actionRow.removeFromLeft (76));
                    actionRow.removeFromLeft (8);
                }

                clearButton.setBounds (actionRow.removeFromLeft (90));
                mediaLabel.setBounds (mediaRow);
            }

            area.removeFromTop (gap);
            listHeader.setVisible (variationsOpen);

            if (variationsOpen)
                listHeader.setBounds (area.removeFromTop (rowHeight).removeFromLeft (120));

            const auto showList = variationsOpen && listHeader.getToggleState();
            for (size_t i = 0; i < rows.size(); ++i)
            {
                auto& row = rows[i];
                const auto shown = showList && i < processor.getVariations().size();
                row->setVisible (shown);

                if (shown)
                    row->setBounds (area.removeFromTop (rowHeight));
            }
        }

        /** Re-reads the history: status line, media readout, and the list when it is open.
            Called by the panel timer when the revision changes, and after every button. */
        void refreshVariations (bool relayout)
        {
            const auto& list = processor.getVariations();
            const auto previewing = processor.isPreviewingVariation();
            const auto size = list.size();

            if (previewing)
                variationStatus.setText ("Previewing " + juce::String (static_cast<int> (processor.getPreviewingVariationIndex()) + 1)
                                             + " of " + juce::String (static_cast<int> (size)),
                                         juce::dontSendNotification);
            else if (size == 0)
                variationStatus.setText ("No variations yet. Randomize or Mutate adds one.", juce::dontSendNotification);
            else
                variationStatus.setText ("Variation " + juce::String (static_cast<int> (list.getCursor()) + 1)
                                             + " of " + juce::String (static_cast<int> (size)),
                                         juce::dontSendNotification);

            const auto megabytes = static_cast<double> (processor.getRetainedMediaBytes()) / (1024.0 * 1024.0);
            mediaLabel.setText ("Samples kept by history: " + juce::String (megabytes, 1) + " MB of "
                                    + juce::String (static_cast<int> (TextureAudioProcessor::variationMediaBudgetBytes / (1024u * 1024u)))
                                    + " MB",
                                juce::dontSendNotification);

            acceptButton.setVisible (previewing);
            cancelButton.setVisible (previewing);

            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (i >= size)
                    continue;

                const auto& entry = list.at (i);
                auto& row = *rows[i];
                const auto marker = i == list.getCursor() ? "> " : "  ";
                row.label.setText (marker + juce::Time (entry.createdAtMs).formatted ("%H:%M:%S") + "  "
                                       + juce::String (entry.operation) + "  #" + juce::String (static_cast<int> (entry.sequence)),
                                   juce::dontSendNotification);
                row.pin.setToggleState (entry.pinned, juce::dontSendNotification);
                row.pin.onClick = [this, i]
                {
                    const auto wanted = rows[i]->pin.getToggleState();
                    processor.setVariationPinned (i, wanted);
                    refreshVariations (false);
                };
                row.star.onClick = [this, i] { if (onStarVariation) onStarVariation (i); };
            }

            lastVariationRevision = processor.getVariationRevision();
            lastPreviewing = previewing;

            if (relayout && onLayoutChanged)
                onLayoutChanged();
        }

        bool variationsNeedRefresh() const
        {
            return processor.getVariationRevision() != lastVariationRevision
                || processor.isPreviewingVariation() != lastPreviewing;
        }

        void setStarHandler (std::function<void (std::size_t)> handler) { onStarVariation = std::move (handler); }

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

        static constexpr std::size_t rowCount = TextureAudioProcessor::VariationList::capacity;

        juce::TextButton variationsHeader;
        juce::TextButton previousButton { "Previous" };
        juce::TextButton nextButton { "Next" };
        juce::TextButton acceptButton { "Accept" };
        juce::TextButton cancelButton { "Cancel" };
        juce::TextButton clearButton { "Clear history" };
        juce::Label variationStatus;
        juce::Label mediaLabel;
        juce::TextButton listHeader;
        std::array<std::unique_ptr<VariationRow>, rowCount> rows;
        std::function<void (std::size_t)> onStarVariation;
        std::uint64_t lastVariationRevision { 0 };
        bool lastPreviewing { false };

        juce::TextButton performanceHeader;
        juce::Label voiceLabel;
        ParameterChoiceBox voiceBox;
        juce::Label priorityLabel;
        ParameterChoiceBox priorityBox;
        ParameterKnob glideKnob;
        juce::Label glideModeLabel;
        ParameterChoiceBox glideModeBox;

        GrainControlsPanel grainPanel;
        ModulationPanel modulationPanel;
        ScanPanel scanPanel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Body)
    };

    SoundControlsPanel::SoundControlsPanel (TextureAudioProcessor& owner)
    {
        setWantsKeyboardFocus (true);

        titleLabel.setText ("TONE, SPACE AND PERFORMANCE", juce::dontSendNotification);
        titleLabel.setFont (TextureLookAndFeel::labelFont());
        titleLabel.setColour (juce::Label::textColourId, colours::textPrimary);
        addAndMakeVisible (titleLabel);

        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        body = std::make_unique<Body> (owner, [this] { resized(); },
                                       [this] (std::size_t index) { if (onStarVariation) onStarVariation (index); });
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
        {
            body->refresh();
            body->refreshVariations (true);
        }
    }

    void SoundControlsPanel::timerCallback()
    {
        // Keeps the status lines honest when the mode changes from the host or a preset,
        // and when a variation is recorded or previewed from the keyboard.
        if (! isVisible() || body == nullptr)
            return;

        body->refresh();

        if (body->variationsNeedRefresh())
            body->refreshVariations (true);
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
