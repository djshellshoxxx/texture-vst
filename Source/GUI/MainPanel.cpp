#include "MainPanel.h"

#include "../PluginProcessor.h"

#include <initializer_list>

namespace texture
{
    namespace
    {
        constexpr int headerHeight = layout::headerHeight;
        constexpr int footerHeight = layout::footerHeight;

        juce::String versionString()
        {
           #if defined (JucePlugin_VersionString)
            return JucePlugin_VersionString;
           #else
            return "0.0.1";
           #endif
        }

        /** theme.md: plugin name in 14 px semibold. */
        juce::Font wordmarkFont()
        {
            return TextureLookAndFeel::interfaceFont (14.0f, true).withExtraKerningFactor (0.06f);
        }

        /** LED (12) + gap (8) after the edge padding. */
        constexpr int wordmarkX = layout::windowEdgePadding + 14 + layout::grid;

        ToastBar::Severity toToastSeverity (SampleLoader::Severity severity)
        {
            switch (severity)
            {
                case SampleLoader::Severity::info:    return ToastBar::Severity::info;
                case SampleLoader::Severity::warning: return ToastBar::Severity::warning;
                case SampleLoader::Severity::error:   return ToastBar::Severity::error;
            }

            return ToastBar::Severity::info;
        }
    }

    MainPanel::MainPanel (TextureAudioProcessor& processorToUse)
        : processor (processorToUse),
          undoGestureListener (processorToUse.getUndoManager())
    {
        setOpaque (true);
        addMouseListener (&undoGestureListener, true);
        setWantsKeyboardFocus (true);

        buildHeader();
        buildControls();
        buildOverlays();
        applyFocusOrder();

        // Only once every child exists: setSize() calls resized() synchronously when
        // the size actually changes, and the layout dereferences the controls.
        setSize (designWidth, designHeight);

        processor.addListener (this);

        updateProGating();
        pluginStateChanged();

        startTimerHz (layout::refreshHz);
    }

    MainPanel::~MainPanel()
    {
        auditionPressed = false;
        processor.stopAudition();
        stopTimer();
        removeMouseListener (&undoGestureListener);
        processor.removeListener (this);
        debugWindow.reset();
    }

    // =========================================================================
    //  Construction
    // =========================================================================

    void MainPanel::buildHeader()
    {
        addAndMakeVisible (levelLed);

        presetNameButton.setTooltip ("The current preset. Click for the preset list.");
        presetNameButton.setColour (juce::TextButton::buttonColourId, colours::panel);
        presetNameButton.onClick = [this] { showPresetMenu(); };
        addAndMakeVisible (presetNameButton);

        previousPresetButton.onClick = [this] { stepPreset (-1); };
        addAndMakeVisible (previousPresetButton);

        nextPresetButton.onClick = [this] { stepPreset (1); };
        addAndMakeVisible (nextPresetButton);

        compareAButton.setTooltip ("Snapshot A. Click to switch to it; the slot you leave is kept.");
        compareAButton.onClick = [this]
        {
            processor.recallCompareSlot (0);
            pluginStateChanged();
        };
        addAndMakeVisible (compareAButton);

        compareBButton.setTooltip ("Snapshot B. Click to switch to it; the slot you leave is kept.");
        compareBButton.onClick = [this]
        {
            processor.recallCompareSlot (1);
            pluginStateChanged();
        };
        addAndMakeVisible (compareBButton);

        midiLearnStatus.setFont (TextureLookAndFeel::labelFont());
        midiLearnStatus.setColour (juce::Label::textColourId, colours::accentPro);
        midiLearnStatus.setJustificationType (juce::Justification::centred);
        midiLearnStatus.setText ("MIDI LEARN", juce::dontSendNotification);
        midiLearnStatus.setTooltip ("MIDI Learn is armed. Right-click a control and choose Cancel MIDI Learn, or wait 15 seconds.");
        midiLearnStatus.setVisible (false);
        addAndMakeVisible (midiLearnStatus);

        fileMenuButton.onClick = [this] { showFileMenu(); };
        addAndMakeVisible (fileMenuButton);

        randomiseButton.onClick = [this] { processor.randomiseParameters(); };
        addAndMakeVisible (randomiseButton);

        lockButton.onClick = [this] { openEditTools(); };
        addAndMakeVisible (lockButton);

        soundButton.onClick = [this] { openSoundControls(); };
        addAndMakeVisible (soundButton);

        resetButton.onClick = [this] { processor.resetToDefaults(); };
        addAndMakeVisible (resetButton);

        helpButton.onClick = [this] { openHelp(); };
        addAndMakeVisible (helpButton);

        gearButton.onClick = [this]
        {
            // Shift-click is the documented back door to the debug view (19_DEBUGGING.md).
            if (juce::ModifierKeys::getCurrentModifiers().isShiftDown())
            {
                openDebugWindow();
                return;
            }

            closeOverlays();

            if (settingsPanel != nullptr)
                settingsPanel->open (SettingsPanel::Tab::preferences);
        };
        addAndMakeVisible (gearButton);

        tierBadge.onClick = [this]
        {
            closeOverlays();

            if (settingsPanel != nullptr)
                settingsPanel->open (SettingsPanel::Tab::license);
        };
        addAndMakeVisible (tierBadge);
    }

    void MainPanel::buildControls()
    {
        dropZoneA = std::make_unique<DropZone> (processor, 0);
        addAndMakeVisible (*dropZoneA);

        dropZoneB = std::make_unique<DropZone> (processor, 1);
        addAndMakeVisible (*dropZoneB);

        morphKnob = std::make_unique<MorphKnob> (processor);
        addAndMakeVisible (*morphKnob);

        attackKnob = std::make_unique<ParameterKnob> (processor, params::attack, "A",
                                                       "Attack: how long a note takes to reach full level.");
        decayKnob = std::make_unique<ParameterKnob> (processor, params::decay, "D",
                                                     "Decay: how long it takes to fall to the sustain level.");
        sustainKnob = std::make_unique<ParameterKnob> (processor, params::sustain, "S",
                                                       "Sustain: the level a held note settles at.");
        releaseKnob = std::make_unique<ParameterKnob> (processor, params::release, "R",
                                                       "Release: how long the note fades after key-up. "
                                                       "The long default is what makes it a pad.");

        for (auto* knob : { attackKnob.get(), decayKnob.get(), sustainKnob.get(), releaseKnob.get() })
            addAndMakeVisible (knob);

        gainKnob = std::make_unique<ParameterKnob> (processor, params::gainDb, "GAIN",
                                                    "Output gain, applied after the effects.");
        addAndMakeVisible (*gainKnob);

        reverbToggle = std::make_unique<ParameterToggle> (processor, params::reverbOn, "Reverb",
                                                           "Reverb on or off. When off it is fully bypassed, "
                                                           "so the dry signal is untouched.");
        addAndMakeVisible (*reverbToggle);

        lfoRateKnob = std::make_unique<ParameterKnob> (processor, params::lfoRate, "RATE",
                                                        "LFO rate in Hz, or the tempo division when sync is on. (Pro)");
        lfoDepthKnob = std::make_unique<ParameterKnob> (processor, params::lfoDepth, "DEPTH",
                                                         "How far the LFO swings MORPH. Depth 100% is "
                                                         "plus or minus 50% around the knob. (Pro)");

        for (auto* knob : { lfoRateKnob.get(), lfoDepthKnob.get() })
            addAndMakeVisible (knob);

        lfoSyncToggle = std::make_unique<ParameterToggle> (processor, params::lfoSync, "Sync",
                                                            "Lock the LFO to the host tempo. (Pro)");
        addAndMakeVisible (*lfoSyncToggle);

        juce::StringArray divisions;

        for (const auto* name : params::divisionNames)
            divisions.add (name);

        lfoDivisionBox = std::make_unique<ParameterChoiceBox> (processor, params::lfoDivision, divisions,
                                                               "Tempo division used while sync is on. (Pro)");
        addAndMakeVisible (*lfoDivisionBox);

        addAndMakeVisible (levelMeter);

        layerBlendKnob = std::make_unique<ParameterKnob> (processor, params::layerBlend, "BLEND",
                                                           "Blends the second sample layer in. Both layers "
                                                           "share one MORPH knob. (Pro)");
        addAndMakeVisible (*layerBlendKnob);

        tuneKnob = std::make_unique<ParameterKnob> (processor, params::tune, "TUNE",
                                                    "Tune in semitones. Samples are played as if recorded "
                                                    "at C3; use this when yours was not.");
        reverseKnob = std::make_unique<ParameterKnob> (processor, params::reverse, "REVERSE",
                                                       "Chance that each grain plays backwards. Higher MORPH "
                                                       "settings make it bloom.");
        shimmerKnob = std::make_unique<ParameterKnob> (processor, params::shimmer, "SHIMMER",
                                                       "Blends octave-up grains into the cloud for a bright, "
                                                       "glassy halo.");

        tuneKnob->getProperties().set ("bipolar", true);

        for (auto* knob : { tuneKnob.get(), reverseKnob.get(), shimmerKnob.get() })
            addAndMakeVisible (knob);

        addAndMakeVisible (envelopeHeader);
        addAndMakeVisible (outputHeader);
        addAndMakeVisible (grainHeader);
        addAndMakeVisible (lfoHeader);
        addAndMakeVisible (layerHeader);

        lfoProBadge.setTooltip ("The MORPH LFO is a Pro feature. Click to open the licence panel.");
        lfoProBadge.onClick = [this] { showProUpsell ("The MORPH LFO"); };
        addAndMakeVisible (lfoProBadge);

        layerProBadge.setTooltip ("Dual-layer is a Pro feature. Click to open the licence panel.");
        layerProBadge.onClick = [this] { showProUpsell ("Dual-layer"); };
        addAndMakeVisible (layerProBadge);

        addAndMakeVisible (dataStream);

        auditionHeader.setText ("AUDITION", juce::dontSendNotification);
        auditionHeader.setFont (TextureLookAndFeel::labelFont());
        auditionHeader.setColour (juce::Label::textColourId, colours::textMuted);
        addAndMakeVisible (auditionHeader);

        for (int note = 0; note < 128; ++note)
        {
            static constexpr std::array<const char*, 12> pitchNames {
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
            };
            const auto name = juce::String (note) + " "
                            + pitchNames[static_cast<size_t> (note % 12)]
                            + juce::String (note / 12 - 2);
            auditionNoteBox.addItem (name, note + 1);
        }
        auditionNoteBox.setSelectedId (61, juce::dontSendNotification); // MIDI 60, C3 in TEXTURE.
        auditionNoteBox.setTooltip ("Audition note. Each entry shows the MIDI number and note name.");
        auditionNoteBox.onChange = [this] { updateAuditionState(); };
        addAndMakeVisible (auditionNoteBox);

        auditionChordBox.addItemList ({ "Single", "Major", "Minor", "Fifth" }, 1);
        auditionChordBox.setSelectedId (1, juce::dontSendNotification);
        auditionChordBox.setTooltip ("Audition a single note or a simple chord.");
        auditionChordBox.onChange = [this] { updateAuditionState(); };
        addAndMakeVisible (auditionChordBox);

        auditionVelocitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
        auditionVelocitySlider.setRange (1.0, 127.0, 1.0);
        auditionVelocitySlider.setValue (96.0, juce::dontSendNotification);
        auditionVelocitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 18);
        auditionVelocitySlider.setTooltip ("Audition MIDI velocity (1–127).");
        auditionVelocitySlider.onValueChange = [this] { updateAuditionState(); };
        addAndMakeVisible (auditionVelocitySlider);

        auditionButton.setTooltip ("Press and hold to audition. Release to stop unless Hold is active.");
        auditionButton.onPressedStateChanged = [this] (bool down)
        {
            auditionPressed = down;
            updateAuditionState();
        };
        auditionButton.setWantsKeyboardFocus (true);
        auditionButton.setColour (juce::TextButton::buttonColourId, colours::accentPrimary);
        auditionButton.setColour (juce::TextButton::textColourOffId, colours::background);
        addAndMakeVisible (auditionButton);

        auditionHoldButton.setClickingTogglesState (true);
        auditionHoldButton.setTooltip ("Keep the audition note or chord playing after releasing the button.");
        auditionHoldButton.onClick = [this] { updateAuditionState(); };
        addAndMakeVisible (auditionHoldButton);

        auditionStopButton.setTooltip ("Stop the audition only. External MIDI notes keep playing.");
        auditionStopButton.onClick = [this]
        {
            auditionPressed = false;
            auditionHoldButton.setToggleState (false, juce::dontSendNotification);
            processor.stopAudition();
        };
        addAndMakeVisible (auditionStopButton);

        updateAuditionAvailability();
        addAndMakeVisible (toast);
    }

    void MainPanel::buildOverlays()
    {
        settingsPanel = std::make_unique<SettingsPanel> (processor);
        settingsPanel->onTierChanged = [this]
        {
            updateProGating();
            pluginStateChanged();
        };
        settingsPanel->onTooltipsChanged = [this] (bool enabled) { setTooltipsEnabled (enabled); };
        settingsPanel->onAudioSettingsRequested = [this]
        {
            if (onAudioSettingsRequested)
                onAudioSettingsRequested();
        };
        addChildComponent (*settingsPanel);

        presetBrowser = std::make_unique<PresetBrowser> (processor);
        presetBrowser->onClose = [this] { presetBrowser->setVisible (false); };
        addChildComponent (*presetBrowser);

        helpPanel = std::make_unique<HelpPanel> (processor);
        helpPanel->onClose = [this] { helpPanel->setVisible (false); };
        helpPanel->onOpenDebugWindow = [this] { openDebugWindow(); };
        addChildComponent (*helpPanel);

        editToolsPanel = std::make_unique<EditToolsPanel> (processor);
        editToolsPanel->onClose = [this] { editToolsPanel->setVisible (false); };
        addChildComponent (*editToolsPanel);

        soundPanel = std::make_unique<SoundControlsPanel> (processor);
        soundPanel->onClose = [this] { soundPanel->setVisible (false); };
        soundPanel->onStarVariation = [this] (std::size_t index)
        {
            // Star exports a normal preset: preview the variation, then use the Save dialog.
            processor.previewVariation (index);
            saveCurrentPreset (true);
        };
        addChildComponent (*soundPanel);

        envelopeHeader.onMenuRequested = [this] { showGroupLockMenu (LockGroup::envelope, envelopeHeader); };
        grainHeader.onMenuRequested = [this] { showGroupLockMenu (LockGroup::grain, grainHeader); };
        lfoHeader.onMenuRequested = [this] { showGroupLockMenu (LockGroup::motion, lfoHeader); };
        outputHeader.onMenuRequested = [this] { showGroupLockMenu (LockGroup::toneSpace, outputHeader); };
        layerHeader.onMenuRequested = [this] { showGroupLockMenu (LockGroup::layers, layerHeader); };
        updateLockIndicators();

        secretPanel = std::make_unique<SecretPanel> (processor);
        secretPanel->onClose = [this]
        {
            processor.setSecretRevealed (false);
            secretPanel->setVisible (false);
            resized();
        };
        addChildComponent (*secretPanel);

        if (processor.getSecretSettings().revealed)
            secretPanel->setVisible (true);
    }

    void MainPanel::applyFocusOrder()
    {
        // 09_GUI_SPEC.md fixes the tab order: MORPH -> ADSR -> Reverb -> Gain -> LFO.
        // Everything the spec does not name follows in reading order, so Tab never
        // dead-ends and never visits an overlay that is not on screen.
        int order = 0;

        const auto place = [&order] (juce::Component* component)
        {
            if (component != nullptr)
                component->setExplicitFocusOrder (++order);
        };

        place (morphKnob.get());
        place (attackKnob.get());
        place (decayKnob.get());
        place (sustainKnob.get());
        place (releaseKnob.get());
        place (reverbToggle.get());
        place (gainKnob.get());
        place (lfoRateKnob.get());
        place (lfoDepthKnob.get());
        place (lfoSyncToggle.get());
        place (lfoDivisionBox.get());
        place (tuneKnob.get());
        place (reverseKnob.get());
        place (shimmerKnob.get());

        place (dropZoneA.get());
        place (dropZoneB.get());
        place (layerBlendKnob.get());

        place (&previousPresetButton);
        place (&presetNameButton);
        place (&nextPresetButton);
        place (&compareAButton);
        place (&compareBButton);
        place (&fileMenuButton);
        place (&randomiseButton);
        place (&soundButton);
        place (&lockButton);
        place (&resetButton);
        place (&helpButton);
        place (&gearButton);
        place (&tierBadge);
    }

    // =========================================================================
    //  Layout
    // =========================================================================

    void MainPanel::resized()
    {
        // The layout below dereferences every control, and resized() can arrive
        // before they exist -- any setSize/setBounds during construction calls it.
        if (morphKnob == nullptr || lfoDivisionBox == nullptr)
            return;

        auto bounds = getLocalBounds();

        // --- Header (theme.md: 32 px strip) -----------------------------------
        auto header = bounds.removeFromTop (headerHeight);
        auto headerContent = header.reduced (layout::windowEdgePadding, 6);

        levelLed.setBounds (headerContent.removeFromLeft (14).withSizeKeepingCentre (12, 12));
        headerContent.removeFromLeft (layout::grid);

        // The hidden effect lives behind one pixel just after the wordmark, where
        // a full stop would sit.
        const auto wordmarkWidth = juce::roundToInt (
            juce::GlyphArrangement::getStringWidth (wordmarkFont(), "TEXTURE"));
        headerContent.removeFromLeft (juce::jmax (90, wordmarkWidth + 8));
        secretHotspot = juce::Rectangle<int> (wordmarkX + wordmarkWidth + 1, headerHeight / 2 + 3, 3, 3);

        headerContent.removeFromLeft (layout::grid);

        tierBadge.setBounds (headerContent.removeFromRight (46).withSizeKeepingCentre (46, 18));
        headerContent.removeFromRight (layout::grid);
        gearButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (4);
        helpButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (4);
        resetButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (4);
        soundButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (4);
        lockButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (4);
        randomiseButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (4);
        fileMenuButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (layout::grid);

        compareBButton.setBounds (headerContent.removeFromRight (26).withSizeKeepingCentre (26, 18));
        compareAButton.setBounds (headerContent.removeFromRight (26).withSizeKeepingCentre (26, 18));
        headerContent.removeFromRight (layout::grid);

        midiLearnStatus.setBounds (headerContent.removeFromRight (88).withSizeKeepingCentre (88, 18));
        headerContent.removeFromRight (layout::grid);

        previousPresetButton.setBounds (headerContent.removeFromLeft (20));
        nextPresetButton.setBounds (headerContent.removeFromRight (20));
        presetNameButton.setBounds (headerContent.reduced (2, 0).withSizeKeepingCentre (
            juce::jmax (80, headerContent.getWidth() - 4), 20));

        // --- Footer -----------------------------------------------------------
        auto footer = bounds.removeFromBottom (footerHeight);
        toast.setBounds (footer.reduced (layout::windowEdgePadding, 1));

        // --- Body -------------------------------------------------------------
        auto body = bounds.reduced (layout::windowEdgePadding, layout::grid);

        auto topRow = body.removeFromTop (196);
        auto sampleArea = topRow.removeFromLeft (348);
        topRow.removeFromLeft (layout::grid * 2);

        // Layer 2 gets a slim strip under layer 1 so the Pro feature is discoverable.
        auto layerStrip = sampleArea.removeFromBottom (84);
        dropZoneA->setBounds (sampleArea.withTrimmedBottom (layout::grid));

        auto layerHeaderRow = layerStrip.removeFromTop (14);
        layerProBadge.setBounds (layerHeaderRow.removeFromRight (44).withSizeKeepingCentre (44, 14));
        layerHeader.setBounds (layerHeaderRow.removeFromLeft (80));

        auto blendCell = layerStrip.removeFromRight (52);
        layerBlendKnob->setBounds (blendCell.reduced (2));
        dropZoneB->setBounds (layerStrip.withTrimmedRight (layout::grid));

        morphKnob->setBounds (topRow.withSizeKeepingCentre (
            juce::jmin (topRow.getWidth(), layout::morphKnobDiameter + 20),
            juce::jmin (topRow.getHeight(), layout::morphKnobDiameter + 34)));

        body.removeFromTop (layout::grid);

        // --- Envelope + output row --------------------------------------------
        auto controlRow = body.removeFromTop (88);

        auto envelopeArea = controlRow.removeFromLeft (280);
        envelopeHeader.setBounds (envelopeArea.removeFromTop (14));
        envelopeArea.removeFromTop (2);

        const auto adsrWidth = envelopeArea.getWidth() / 4;

        for (auto* knob : { attackKnob.get(), decayKnob.get(), sustainKnob.get(), releaseKnob.get() })
            knob->setBounds (envelopeArea.removeFromLeft (adsrWidth).reduced (6, 0));

        controlRow.removeFromLeft (layout::grid * 2);

        auto outputArea = controlRow;
        outputHeader.setBounds (outputArea.removeFromTop (14));
        outputArea.removeFromTop (2);

        reverbToggle->setBounds (outputArea.removeFromLeft (84).withSizeKeepingCentre (84, 28));
        outputArea.removeFromLeft (layout::grid);
        gainKnob->setBounds (outputArea.removeFromLeft (64).reduced (4, 0));
        outputArea.removeFromLeft (layout::grid * 2);
        levelMeter.setBounds (outputArea.withSizeKeepingCentre (outputArea.getWidth(), 44));

        body.removeFromTop (layout::grid);

        // --- LFO section + data stream ------------------------------------------
        auto lfoArea = body;

        if (secretPanel != nullptr && secretPanel->isVisible())
        {
            auto secretArea = lfoArea.removeFromRight (236);
            secretPanel->setBounds (secretArea);
            lfoArea.removeFromRight (layout::grid);
        }

        auto lfoControls = lfoArea.removeFromLeft (286);
        lfoArea.removeFromLeft (layout::grid * 2);

        // Grain section: three knobs, squeezed narrower when GRIT takes the right edge.
        auto grainArea = lfoArea.removeFromLeft (juce::jmin (lfoArea.getWidth(), 192));
        grainHeader.setBounds (grainArea.removeFromTop (14));
        grainArea.removeFromTop (2);
        grainArea = grainArea.withSizeKeepingCentre (grainArea.getWidth(), juce::jmin (76, grainArea.getHeight()));

        const auto grainKnobWidth = grainArea.getWidth() / 3;

        for (auto* knob : { tuneKnob.get(), reverseKnob.get(), shimmerKnob.get() })
            knob->setBounds (grainArea.removeFromLeft (grainKnobWidth).reduced (2, 0));

        lfoArea.removeFromLeft (layout::grid);

        // A compact audition strip uses the formerly empty right edge. Keep the
        // diagnostic stream at very narrow sizes where the audition strip cannot fit.
        if (lfoArea.getWidth() >= 170)
        {
            auditionHeader.setVisible (true);
            auditionNoteBox.setVisible (true);
            auditionChordBox.setVisible (true);
            auditionVelocitySlider.setVisible (true);
            auditionButton.setVisible (true);
            auditionHoldButton.setVisible (true);
            auditionStopButton.setVisible (true);
            auditionHeader.setBounds (lfoArea.removeFromTop (14));
            lfoArea.removeFromTop (1);
            auto noteRow = lfoArea.removeFromTop (22);
            auditionNoteBox.setBounds (noteRow.removeFromLeft (88));
            noteRow.removeFromLeft (4);
            auditionChordBox.setBounds (noteRow);
            lfoArea.removeFromTop (2);
            auditionVelocitySlider.setBounds (lfoArea.removeFromTop (20));
            lfoArea.removeFromTop (2);
            auto auditionButtons = lfoArea.removeFromTop (22);
            const auto buttonWidth = juce::jmax (42, (auditionButtons.getWidth() - 8) / 3);
            auditionButton.setBounds (auditionButtons.removeFromLeft (buttonWidth));
            auditionButtons.removeFromLeft (4);
            auditionHoldButton.setBounds (auditionButtons.removeFromLeft (buttonWidth));
            auditionButtons.removeFromLeft (4);
            auditionStopButton.setBounds (auditionButtons);
            dataStream.setBounds (0, 0, 0, 0);
            dataStream.setVisible (false);
        }
        else
        {
            auditionHeader.setVisible (false);
            auditionNoteBox.setVisible (false);
            auditionChordBox.setVisible (false);
            auditionVelocitySlider.setVisible (false);
            auditionButton.setVisible (false);
            auditionHoldButton.setVisible (false);
            auditionStopButton.setVisible (false);
            dataStream.setBounds (lfoArea);
            dataStream.setVisible (lfoArea.getWidth() >= 80);
        }

        auto lfoHeaderRow = lfoControls.removeFromTop (14);
        lfoHeader.setBounds (lfoHeaderRow.removeFromLeft (100));
        lfoProBadge.setBounds (lfoHeaderRow.removeFromLeft (150).withSizeKeepingCentre (150, 14));

        lfoControls.removeFromTop (2);
        lfoControls = lfoControls.withSizeKeepingCentre (lfoControls.getWidth(), juce::jmin (76, lfoControls.getHeight()));

        lfoRateKnob->setBounds (lfoControls.removeFromLeft (60).reduced (2, 0));
        lfoControls.removeFromLeft (4);
        lfoDepthKnob->setBounds (lfoControls.removeFromLeft (60).reduced (2, 0));
        lfoControls.removeFromLeft (layout::grid);
        lfoSyncToggle->setBounds (lfoControls.removeFromLeft (70).withSizeKeepingCentre (70, 28));
        lfoControls.removeFromLeft (layout::grid);
        lfoDivisionBox->setBounds (lfoControls.removeFromLeft (76).withSizeKeepingCentre (76, 24));

        // --- Overlays ----------------------------------------------------------
        if (presetBrowser != nullptr)
            presetBrowser->setBounds (getLocalBounds());

        if (helpPanel != nullptr)
            helpPanel->setBounds (getLocalBounds());

        if (settingsPanel != nullptr)
            settingsPanel->updateSlideBounds();
    }

    // =========================================================================
    //  Painting
    // =========================================================================

    void MainPanel::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background);

        auto bounds = getLocalBounds();

        // --- Header strip ------------------------------------------------------
        auto header = bounds.removeFromTop (headerHeight);

        g.setColour (colours::panel);
        g.fillRect (header);

        g.setColour (colours::panelEdge);
        g.fillRect (header.removeFromBottom (1));

        TextureLookAndFeel::drawSignatureNotch (g, getLocalBounds());

        // Wordmark. Its last few pixels double as the hidden effect's trigger.
        g.setColour (colours::textPrimary);
        g.setFont (wordmarkFont());
        g.drawText ("TEXTURE",
                    juce::Rectangle<int> (wordmarkX, 0, 120, headerHeight),
                    juce::Justification::centredLeft, false);

        // --- Footer separator ---------------------------------------------------
        auto footer = bounds.removeFromBottom (footerHeight);

        g.setColour (colours::panelEdge);
        g.fillRect (footer.removeFromTop (1));

        // Version, bottom right, in the small muted mono of the house style.
        if (! toast.isShowing())
        {
            g.setColour (colours::textMuted.withAlpha (0.7f));
            g.setFont (TextureLookAndFeel::monoFont (9.0f));
            g.drawText ("v" + versionString(),
                        footer.reduced (layout::windowEdgePadding, 0),
                        juce::Justification::centredRight, false);
        }

        // --- Section separators -------------------------------------------------
        g.setColour (colours::panelEdge);

        const auto separatorY = envelopeHeader.getY() - layout::grid / 2;
        g.fillRect (layout::windowEdgePadding, separatorY,
                    getWidth() - layout::windowEdgePadding * 2, 1);

        const auto lfoSeparatorY = lfoHeader.getY() - layout::grid / 2;
        g.fillRect (layout::windowEdgePadding, lfoSeparatorY,
                    getWidth() - layout::windowEdgePadding * 2, 1);

        // --- Pro gating veil ----------------------------------------------------
        // Same caveat as resized(): the veil is measured from the controls.
        if (! processor.isPro() && dropZoneB != nullptr && layerBlendKnob != nullptr && lfoDivisionBox != nullptr)
        {
            g.setColour (colours::background.withAlpha (0.6f));

            const auto lfoVeil = juce::Rectangle<int> (layout::windowEdgePadding,
                                                       lfoHeader.getY(),
                                                       juce::jmax (0, lfoDivisionBox->getRight() - layout::windowEdgePadding),
                                                       juce::jmax (0, lfoDivisionBox->getBottom() - lfoHeader.getY()));
            g.fillRect (lfoVeil);

            const auto layerVeil = juce::Rectangle<int> (dropZoneB->getX(),
                                                         layerHeader.getY(),
                                                         juce::jmax (0, layerBlendKnob->getRight() - dropZoneB->getX()),
                                                         juce::jmax (0, layerBlendKnob->getBottom() - layerHeader.getY()));
            g.fillRect (layerVeil);
        }
    }

    // =========================================================================
    //  Interaction
    // =========================================================================

    void MainPanel::mouseDown (const juce::MouseEvent& event)
    {
        if (secretHotspot.contains (event.getPosition()))
        {
            revealSecret();
            return;
        }

        if (event.mods.isPopupMenu())
            showFileMenu();
    }

    void MainPanel::revealSecret()
    {
        if (secretPanel == nullptr)
            return;

        if (secretPanel->isVisible())
            return;

        processor.setSecretRevealed (true);
        secretPanel->refreshFromProcessor();
        secretPanel->setVisible (true);
        secretPanel->toFront (false);
        resized();

        processor.postToast ("You found GRIT - the hidden effect.", SampleLoader::Severity::info);
    }

    bool MainPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0))
        {
            saveCurrentPreset (false);
            return true;
        }

        if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier
                                            | juce::ModifierKeys::shiftModifier, 0))
        {
            saveCurrentPreset (true);
            return true;
        }

        if (key == juce::KeyPress ('o', juce::ModifierKeys::commandModifier, 0))
        {
            openPresetFile();
            return true;
        }

        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        {
            performUndo (false);
            return true;
        }

        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier
                                            | juce::ModifierKeys::shiftModifier, 0)
            || key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0))
        {
            performUndo (true);
            return true;
        }

        if (key == juce::KeyPress ('r', juce::ModifierKeys::commandModifier, 0))
        {
            processor.randomiseParameters();
            return true;
        }

        if (key == juce::KeyPress ('m', juce::ModifierKeys::commandModifier, 0))
        {
            runGentleMutate();
            return true;
        }

        if (key == juce::KeyPress::F1Key)
        {
            openHelp();
            return true;
        }

        if (key == juce::KeyPress::escapeKey)
        {
            closeOverlays();
            return true;
        }

        return false;
    }

    void MainPanel::performUndo (bool redo)
    {
        auto& history = processor.getUndoManager();
        const auto description = redo ? history.getRedoDescription() : history.getUndoDescription();
        const auto succeeded = redo ? processor.redoLastEdit() : processor.undoLastEdit();

        if (! succeeded)
        {
            processor.postToast (redo ? "Nothing to redo." : "Nothing to undo.", SampleLoader::Severity::info);
            return;
        }

        processor.postToast ((redo ? "Redo" : "Undo")
                                 + (description.isNotEmpty() ? ": " + description : juce::String()),
                             SampleLoader::Severity::info);
    }

    void MainPanel::closeOverlays()
    {
        if (presetBrowser != nullptr)
            presetBrowser->setVisible (false);

        if (editToolsPanel != nullptr)
            editToolsPanel->setVisible (false);

        if (soundPanel != nullptr)
            soundPanel->setVisible (false);

        if (helpPanel != nullptr)
            helpPanel->setVisible (false);

        if (settingsPanel != nullptr && settingsPanel->isOpen())
            settingsPanel->close();
    }

    void MainPanel::openHelp()
    {
        closeOverlays();

        if (helpPanel == nullptr)
            return;

        helpPanel->setBounds (getLocalBounds());
        helpPanel->setVisible (true);
        helpPanel->toFront (true);
    }

    void MainPanel::openEditTools()
    {
        closeOverlays();

        if (editToolsPanel == nullptr)
            return;

        editToolsPanel->setBounds (getLocalBounds());
        editToolsPanel->refresh();
        editToolsPanel->setVisible (true);
        editToolsPanel->toFront (true);
        editToolsPanel->grabKeyboardFocus();
    }

    void MainPanel::openSoundControls()
    {
        closeOverlays();

        if (soundPanel == nullptr)
            return;

        soundPanel->setBounds (getLocalBounds());
        soundPanel->refresh();
        soundPanel->setVisible (true);
        soundPanel->toFront (true);
        soundPanel->grabKeyboardFocus();
    }

    void MainPanel::runGentleMutate()
    {
        // Mutate reports its own result as a toast, and writes nothing when it is refused.
        processor.mutateParameters();
        updateLockIndicators();
    }

    void MainPanel::showGroupLockMenu (LockGroup group, juce::Component& target)
    {
        const auto allLocked = processor.getGroupLockState (group) == LockPolicy::GroupState::all;

        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        menu.addItem (1, juce::String (allLocked ? "Unlock all " : "Lock all ")
                             + LockPolicy::groupName (group));
        menu.addItem (2, "Open edit locks and Mutate...");

        juce::Component::SafePointer<MainPanel> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                            [safeThis, group] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            if (result == 1)
            {
                const auto shouldLock = safeThis->processor.getGroupLockState (group)
                                        != LockPolicy::GroupState::all;
                safeThis->processor.setGroupLocked (group, shouldLock);
                safeThis->updateLockIndicators();
            }
            else if (result == 2)
            {
                safeThis->openEditTools();
            }
        });
    }

    void MainPanel::updateLockIndicators()
    {
        const auto refresh = [this] (SectionHeader& header, LockGroup group)
        {
            int locked = 0;

            for (const auto& field : LockPolicy::fields())
                if (field.group == group && processor.isFieldLocked (juce::String (std::string (field.id))))
                    ++locked;

            header.setLockCount (locked, LockPolicy::countFieldsIn (group));
        };

        refresh (envelopeHeader, LockGroup::envelope);
        refresh (grainHeader, LockGroup::grain);
        refresh (lfoHeader, LockGroup::motion);
        refresh (outputHeader, LockGroup::toneSpace);
        refresh (layerHeader, LockGroup::layers);

        if (auto* lockPanel = editToolsPanel.get(); lockPanel != nullptr && lockPanel->isVisible())
            lockPanel->refresh();
    }

    void MainPanel::openDebugWindow()
    {
        if (debugWindow != nullptr)
        {
            debugWindow->toFront (true);
            return;
        }

        debugWindow = std::make_unique<DebugWindow> (processor, getLookAndFeel());
        debugWindow->onClose = [this]
        {
            // Defer the destruction: we are inside the window's own callback.
            juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainPanel> (this)]
            {
                if (safeThis != nullptr)
                    safeThis->debugWindow.reset();
            });
        };
    }

    void MainPanel::openPresetBrowser()
    {
        if (! processor.isPro())
        {
            showProUpsell ("The preset browser");
            return;
        }

        closeOverlays();

        if (presetBrowser == nullptr)
            return;

        processor.getPresetManager().refresh();
        presetBrowser->refresh();
        presetBrowser->setBounds (getLocalBounds());
        presetBrowser->setVisible (true);
        presetBrowser->toFront (true);
    }

    void MainPanel::showProUpsell (const juce::String& featureName)
    {
        closeOverlays();

        if (settingsPanel != nullptr)
            settingsPanel->open (SettingsPanel::Tab::license);

        processor.postToast (featureName + " is a Pro feature. $10, one time.",
                             SampleLoader::Severity::info);
    }

    // =========================================================================
    //  Menus
    // =========================================================================

    void MainPanel::showFileMenu()
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());

        const auto addItem = [&menu] (int id, const juce::String& text, const juce::String& shortcut = {},
                                      bool enabled = true, bool ticked = false)
        {
            juce::PopupMenu::Item item (text);
            item.itemID = id;
            item.isEnabled = enabled;
            item.isTicked = ticked;
            item.shortcutKeyDescription = shortcut;
            menu.addItem (std::move (item));
        };

        const auto hasSample = processor.getSampleForLayer (0) != nullptr && processor.getSampleForLayer (0)->isValid();

        menu.addSectionHeader ("Presets");
        addItem (1, "Open preset...", "Ctrl+O");
        addItem (2, "Save", "Ctrl+S");
        addItem (3, "Save as...", "Ctrl+Shift+S");
        addItem (4, processor.isPro() ? "Preset browser..." : "Preset browser...  (PRO)");
        menu.addSeparator();

        menu.addSectionHeader ("Sample");
        addItem (13, "Load sample...");
        addItem (14, "Clear sample", {}, hasSample);
        menu.addSeparator();

        menu.addSectionHeader ("Audio");
        addItem (5, processor.isPro() ? "Export audio..." : "Export audio...  (PRO)");
        addItem (40, processor.isPro() ? "Export sound bundle..." : "Export sound bundle...  (PRO)");
        addItem (41, "Import sound bundle...");
        menu.addSeparator();

        menu.addSectionHeader ("Snapshots");
        addItem (6, "Copy A to B");
        addItem (7, "Copy B to A");
        menu.addSeparator();

        juce::PopupMenu sizeMenu;

        for (const auto percent : { 75, 100, 125, 150, 175, 200 })
            sizeMenu.addItem (100 + percent, juce::String (percent) + "%", true,
                              std::abs (currentScale * 100.0f - static_cast<float> (percent)) < 1.0f);

        menu.addSectionHeader ("General");
        addItem (15, "Undo", "Ctrl+Z", processor.getUndoManager().canUndo());
        addItem (16, "Redo", "Ctrl+Shift+Z", processor.getUndoManager().canRedo());
        addItem (8, "Randomize", "Ctrl+R");
        addItem (17, "Gentle Mutate", "Ctrl+M");
        addItem (9, "Reset all controls");
        addItem (10, "Options...");
        menu.addSubMenu ("Interface size", sizeMenu);
        addItem (11, "Open location in file browser");
        addItem (12, "Help", "F1");

        juce::Component::SafePointer<MainPanel> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileMenuButton),
                            [safeThis] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            if (result > 100)
            {
                if (safeThis->onScaleRequested)
                    safeThis->onScaleRequested (static_cast<float> (result - 100) / 100.0f);
                return;
            }

            switch (result)
            {
                case 1:  safeThis->openPresetFile(); break;
                case 2:  safeThis->saveCurrentPreset (false); break;
                case 3:  safeThis->saveCurrentPreset (true); break;
                case 4:  safeThis->openPresetBrowser(); break;
                case 5:  safeThis->exportAudio(); break;
                case 40:
                    if (safeThis->processor.isPro())
                        safeThis->portableMedia.exportBundle();
                    else
                        safeThis->showProUpsell ("Sound bundle export");
                    break;
                case 41: safeThis->portableMedia.importBundle(); break;

                case 6:
                    safeThis->processor.copyCompareSlot (0, 1);
                    safeThis->processor.postToast ("Copied A to B.", SampleLoader::Severity::info);
                    break;

                case 7:
                    safeThis->processor.copyCompareSlot (1, 0);
                    safeThis->processor.postToast ("Copied B to A.", SampleLoader::Severity::info);
                    break;

                case 8:  safeThis->processor.randomiseParameters(); break;
                case 17: safeThis->runGentleMutate(); break;
                case 9:  safeThis->processor.resetToDefaults(); break;

                case 10:
                    safeThis->closeOverlays();

                    if (safeThis->settingsPanel != nullptr)
                        safeThis->settingsPanel->open (SettingsPanel::Tab::preferences);
                    break;

                case 11:
                {
                    auto directory = PresetManager::getPluginDataDirectory();
                    directory.createDirectory();
                    directory.revealToUser();
                    break;
                }

                case 12: safeThis->openHelp(); break;
                case 15: safeThis->performUndo (false); break;
                case 16: safeThis->performUndo (true); break;

                case 13:
                    if (safeThis->dropZoneA != nullptr)
                        safeThis->dropZoneA->openFileChooser();
                    break;

                case 14: safeThis->processor.clearLayer (0); break;
                default: break;
            }
        });
    }

    void MainPanel::stepPreset (int delta)
    {
        auto& presets = processor.getPresetManager();
        const auto& all = presets.getAllPresets();

        if (all.empty())
        {
            processor.postToast ("No presets found. Save one, or rescan the preset folder.",
                                 SampleLoader::Severity::info);
            return;
        }

        const auto count = static_cast<int> (all.size());
        int current = -1;

        for (int i = 0; i < count; ++i)
            if (all[static_cast<size_t> (i)].name == processor.getCurrentPresetName())
                current = i;

        // From an unnamed patch, "next" is the first preset and "previous" the last.
        const auto next = current < 0 ? (delta > 0 ? 0 : count - 1)
                                       : ((current + delta) % count + count) % count;

        processor.loadPreset (all[static_cast<size_t> (next)].file);
    }

    void MainPanel::showPresetMenu()
    {
        auto& presets = processor.getPresetManager();
        const auto& all = presets.getAllPresets();

        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());

        juce::Array<juce::File> ordered;
        juce::String lastCategory;

        for (const auto& info : all)
        {
            if (info.category != lastCategory)
            {
                menu.addSectionHeader (info.category);
                lastCategory = info.category;
            }

            ordered.add (info.file);
            menu.addItem (ordered.size(), info.name, true,
                          info.name == processor.getCurrentPresetName());
        }

        if (ordered.isEmpty())
            menu.addItem (-1, "(no presets found)", false, false);

        menu.addSeparator();
        menu.addItem (-2, "Save as...");
        menu.addItem (-3, processor.isPro() ? "Open browser..." : "Open browser...  (PRO)");
        menu.addItem (-4, "Rescan preset folder");

        juce::Component::SafePointer<MainPanel> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetNameButton),
                            [safeThis, ordered] (int result)
        {
            if (safeThis == nullptr || result == 0 || result == -1)
                return;

            if (result == -2)
            {
                safeThis->saveCurrentPreset (true);
                return;
            }

            if (result == -3)
            {
                safeThis->openPresetBrowser();
                return;
            }

            if (result == -4)
            {
                safeThis->processor.getPresetManager().refresh();
                safeThis->processor.postToast ("Preset folder rescanned.", SampleLoader::Severity::info);
                return;
            }

            if (result >= 1 && result <= ordered.size())
                safeThis->processor.loadPreset (ordered[result - 1]);
        });
    }

    // =========================================================================
    //  File actions
    // =========================================================================

    void MainPanel::openPresetFile()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open a preset",
                                                            PresetManager::getUserPresetDirectory(),
                                                            "*.texture");

        juce::Component::SafePointer<MainPanel> safeThis (this);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto file = chooser.getResult();

            if (file != juce::File())
                safeThis->processor.loadPreset (file);
        });
    }

    void MainPanel::saveCurrentPreset (bool forceSaveAs)
    {
        const auto currentName = processor.getCurrentPresetName();
        const auto currentFile = processor.getCurrentPresetFile();

        juce::String currentCategory = processor.getCurrentPresetCategory();
        juce::StringArray currentTags;

        for (const auto& info : processor.getPresetManager().getAllPresets())
        {
            if (info.file == currentFile)
            {
                currentCategory = info.category;
                currentTags = info.tags;
                break;
            }
        }

        // Ctrl+S writes only to the exact user preset that was loaded/saved. Never
        // resolve by display name: duplicate names can exist in different categories.
        if (! forceSaveAs && currentFile.existsAsFile()
            && ! currentFile.isAChildOf (PresetManager::getFactoryPresetDirectory()))
        {
            processor.savePreset (currentFile, currentName, currentCategory, currentTags);
            return;
        }

        auto* window = new juce::AlertWindow ("Save preset",
                                              "Give this patch a name, a category and some tags.",
                                              juce::MessageBoxIconType::NoIcon);

        window->addTextEditor ("name", currentName == "Untitled" ? "My Pad" : currentName, "Name");
        window->addComboBox ("category", PresetManager::getCategories(), "Category");
        window->addTextEditor ("tags", currentTags.joinIntoString (", "), "Tags (comma separated)");

        if (auto* categoryBox = window->getComboBoxComponent ("category"))
        {
            const auto categories = PresetManager::getCategories();
            const auto selected = categories.indexOf (currentCategory);
            categoryBox->setSelectedItemIndex (selected >= 0 ? selected : 0, juce::dontSendNotification);
        }
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<MainPanel> safeThis (this);

        window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, window] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owned (window);

            if (safeThis == nullptr || result != 1)
                return;

            auto name = window->getTextEditorContents ("name").trim();

            if (name.isEmpty())
                name = "Untitled";

            juce::String category = "Cinematic";

            if (auto* box = window->getComboBoxComponent ("category"))
                category = box->getText();

            juce::StringArray tags;
            tags.addTokens (window->getTextEditorContents ("tags"), ",", "");
            tags.trim();
            tags.removeEmptyStrings();

            const auto destination = PresetManager::getUserPresetDirectory()
                                         .getChildFile (category)
                                         .getChildFile (juce::File::createLegalFileName (name)
                                                            + PresetManager::fileExtension);

            safeThis->processor.savePreset (destination, name, category, tags);
        }), false);
    }

    void MainPanel::exportAudio()
    {
        if (! processor.isPro())
        {
            showProUpsell ("WAV export");
            return;
        }

        const auto sample = processor.getSampleForLayer (0);

        if (sample == nullptr || ! sample->isValid())
        {
            processor.postToast ("Load a sample before exporting.", SampleLoader::Severity::warning);
            return;
        }

        auto* window = new juce::AlertWindow ("Export audio",
                                              "Renders a C3 hit, then a second C3 hit with a four "
                                              "second tail, into one stereo WAV file.",
                                              juce::MessageBoxIconType::NoIcon);

        juce::StringArray rates;

        for (const auto rate : WavExporter::getSupportedSampleRates())
            rates.add (juce::String (rate / 1000.0, 1) + " kHz");

        juce::StringArray depths;

        for (const auto depth : WavExporter::getSupportedBitDepths())
            depths.add (juce::String (depth) + "-bit");

        window->addComboBox ("rate", rates, "Sample rate");
        window->addComboBox ("depth", depths, "Bit depth");
        window->addButton ("Choose file...", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        if (auto* rateBox = window->getComboBoxComponent ("rate"))
            rateBox->setSelectedItemIndex (1, juce::dontSendNotification);   // 48 kHz

        if (auto* depthBox = window->getComboBoxComponent ("depth"))
            depthBox->setSelectedItemIndex (1, juce::dontSendNotification);  // 24-bit

        juce::Component::SafePointer<MainPanel> safeThis (this);

        window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, window] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owned (window);

            if (safeThis == nullptr || result != 1)
                return;

            WavExporter::Options options;

            if (auto* rateBox = window->getComboBoxComponent ("rate"))
                options.sampleRate = WavExporter::getSupportedSampleRates()[
                    juce::jmax (0, rateBox->getSelectedItemIndex())];

            if (auto* depthBox = window->getComboBoxComponent ("depth"))
                options.bitDepth = WavExporter::getSupportedBitDepths()[
                    juce::jmax (0, depthBox->getSelectedItemIndex())];

            const auto suggestedName = juce::File::createLegalFileName (
                safeThis->processor.getCurrentPresetName()) + ".wav";

            safeThis->fileChooser = std::make_unique<juce::FileChooser> (
                "Export WAV",
                juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (suggestedName),
                "*.wav");

            safeThis->fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                                    | juce::FileBrowserComponent::canSelectFiles
                                                    | juce::FileBrowserComponent::warnAboutOverwriting,
                                                [safeThis, options] (const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr)
                    return;

                const auto destination = chooser.getResult();

                if (destination == juce::File())
                    return;

                const std::array<SampleBufferPtr, 2> samples {
                    safeThis->processor.getSampleForLayer (0),
                    safeThis->processor.getSampleForLayer (1)
                };

                const auto renderResult = WavExporter::render (samples,
                                                               safeThis->processor.captureRenderSettings(),
                                                               options,
                                                               destination);

                if (! renderResult.succeeded)
                {
                    safeThis->processor.postToast (renderResult.message, SampleLoader::Severity::error);
                    return;
                }

                safeThis->processor.postToast (renderResult.message, SampleLoader::Severity::info);

                juce::AlertWindow::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::InfoIcon)
                        .withTitle ("Export complete")
                        .withMessage ("File:     " + renderResult.file.getFileName() + "\n"
                                      + "Location: " + renderResult.file.getParentDirectory().getFullPathName() + "\n"
                                      + "Length:   " + juce::String (renderResult.lengthSeconds, 2) + " seconds\n"
                                      + "Quality:  " + WavExporter::describeQuality (options) + "\n"
                                      + "Size:     " + juce::File::descriptionOfSizeInBytes (renderResult.fileSizeBytes))
                        .withButton ("Show file")
                        .withButton ("Done"),
                    [file = renderResult.file] (int choice)
                    {
                        if (choice == 1)
                            file.revealToUser();
                    });
            });
        }), false);
    }

    // =========================================================================
    //  Processor callbacks
    // =========================================================================

    void MainPanel::sampleChanged (int layerIndex)
    {
        if (layerIndex == 0 && dropZoneA != nullptr)
            dropZoneA->refreshFromProcessor();
        else if (layerIndex == 1 && dropZoneB != nullptr)
            dropZoneB->refreshFromProcessor();

        updateAuditionAvailability();
    }

    void MainPanel::sampleMissing (int layerIndex, const juce::String& expectedSha256, const juce::String& fileName)
    {
        portableMedia.offerRelink (layerIndex, expectedSha256, fileName);
    }

    void MainPanel::toastRequested (const juce::String& message, SampleLoader::Severity severity)
    {
        toast.show (message, toToastSeverity (severity));
    }

    void MainPanel::pluginStateChanged()
    {
        const auto name = processor.getCurrentPresetName()
                        + (processor.isPresetDirty() ? juce::String (" *") : juce::String());

        presetNameButton.setButtonText (name);

        compareAButton.setToggleState (processor.getActiveCompareSlot() == 0, juce::dontSendNotification);
        compareBButton.setToggleState (processor.getActiveCompareSlot() == 1, juce::dontSendNotification);

        tierBadge.setPro (processor.isPro());
        updateProGating();
        if (! processor.isAuditionRequested())
            auditionHoldButton.setToggleState (false, juce::dontSendNotification);
        updateAuditionAvailability();
        updateLockIndicators();
        repaint();
    }

    void MainPanel::updateAuditionAvailability()
    {
        auto hasSample = false;

        for (int layer = 0; layer < TextureAudioProcessor::numLayers; ++layer)
        {
            const auto sample = processor.getSampleForLayer (layer);
            hasSample = hasSample || (sample != nullptr && sample->isValid());
        }

        auditionNoteBox.setEnabled (hasSample);
        auditionChordBox.setEnabled (hasSample);
        auditionVelocitySlider.setEnabled (hasSample);
        auditionButton.setEnabled (hasSample);
        auditionHoldButton.setEnabled (hasSample);
        auditionStopButton.setEnabled (hasSample || processor.isAuditionRequested());
        auditionButton.setTooltip (hasSample
                                       ? "Press and hold to audition. Release to stop unless Hold is active."
                                       : "Load a sample into either layer to enable note audition.");

        if (! hasSample && (auditionPressed || auditionHoldButton.getToggleState()))
        {
            auditionPressed = false;
            auditionHoldButton.setToggleState (false, juce::dontSendNotification);
            processor.stopAudition();
        }
    }

    void MainPanel::updateAuditionState()
    {
        const auto note = juce::jmax (0, auditionNoteBox.getSelectedId() - 1);
        const auto velocity = juce::roundToInt (auditionVelocitySlider.getValue());
        const auto chord = juce::jmax (0, auditionChordBox.getSelectedId() - 1);
        const auto enabled = auditionPressed || auditionHoldButton.getToggleState();
        processor.setAuditionNoteSet (note, velocity, chord, enabled);
    }

    void MainPanel::updateProGating()
    {
        const auto pro = processor.isPro();

        // Pro parameters always exist; in the free tier their controls are disabled
        // and veiled, and the badge offers the upgrade (11_FREE_VS_PRO.md).
        for (auto* component : std::initializer_list<juce::Component*> { lfoRateKnob.get(),
                                                                         lfoDepthKnob.get(),
                                                                         lfoSyncToggle.get(),
                                                                         layerBlendKnob.get() })
            if (component != nullptr)
                component->setEnabled (pro);

        if (lfoDivisionBox != nullptr)
            lfoDivisionBox->setEnabled (pro);
        lfoProBadge.setVisible (! pro);
        layerProBadge.setVisible (! pro);

        if (dropZoneB != nullptr)
            dropZoneB->setEnabled (pro);

        repaint();
    }

    void MainPanel::setTooltipsEnabled (bool shouldBeEnabled)
    {
        // The editor owns the TooltipWindow; this only forwards the preference.
        if (onTooltipsChanged)
            onTooltipsChanged (shouldBeEnabled);
    }

    // =========================================================================
    //  Timer
    // =========================================================================

    void MainPanel::timerCallback()
    {
        if (! processor.isAuditionRequested() && auditionHoldButton.getToggleState())
            auditionHoldButton.setToggleState (false, juce::dontSendNotification);

        updateAuditionAvailability();
        levelLed.setLevel (processor.getSmoothedOutputPeak());
        levelMeter.pushLevels (processor.readChannelPeak (0), processor.readChannelPeak (1),
                               juce::Time::getMillisecondCounterHiRes());

        const auto morph = processor.getEffectiveMorph();
        const auto ringColour = colours::morphRing (morph);

        if (dropZoneA != nullptr)
        {
            dropZoneA->setPlayheadPosition (processor.getReadPosition());
            dropZoneA->setEnvelopeColour (ringColour);
        }

        if (dropZoneB != nullptr)
            dropZoneB->setEnvelopeColour (ringColour.withAlpha (0.8f));

        if (morphKnob != nullptr)
        {
            const auto* depth = processor.getValueTreeState().getRawParameterValue (params::lfoDepth);
            const auto lfoActive = processor.isPro() && depth != nullptr && depth->load() > 0.0f;

            morphKnob->setModulationVisible (lfoActive);
            morphKnob->setModulatedValue (morph);
        }

        if (processor.getMidiLearn().cancelLearningIfTimedOut())
            processor.postToast ("MIDI learn timed out after 15 seconds.", SampleLoader::Severity::info);

        const auto learningParameter = processor.getMidiLearn().getLearningParameterID();
        midiLearnStatus.setVisible (processor.getMidiLearn().isLearning());
        midiLearnStatus.setText (learningParameter.isNotEmpty()
                                     ? "LEARN " + params::displayNameFor (learningParameter).toUpperCase()
                                     : "MIDI LEARN",
                                 juce::dontSendNotification);

        // Pulse whichever knob is waiting for a MIDI CC.
        for (auto* knob : { static_cast<ParameterKnob*> (morphKnob.get()), attackKnob.get(), decayKnob.get(),
                            sustainKnob.get(), releaseKnob.get(), gainKnob.get(), lfoRateKnob.get(),
                            lfoDepthKnob.get(), layerBlendKnob.get() })
            if (knob != nullptr)
                knob->updateLearnIndicator();

        // The division dropdown only matters while sync is engaged (09_GUI_SPEC.md).
        if (const auto* sync = processor.getValueTreeState().getRawParameterValue (params::lfoSync))
            if (lfoDivisionBox != nullptr)
                lfoDivisionBox->setVisible (sync->load() > 0.5f);

        updateDataStream();
        dataStream.tick();
    }

    void MainPanel::updateDataStream()
    {
        // Only real changes produce a line, so the stream stops when nothing is
        // happening and runs while things are being played or tweaked (theme.md).
        juce::StringArray fresh;
        auto& state = processor.getValueTreeState();

        for (int i = 0; i < params::numParameters; ++i)
        {
            const auto* id = params::allIDs[i];
            const auto* raw = state.getRawParameterValue (id);

            if (raw == nullptr)
                continue;

            const auto value = raw->load();
            auto& last = streamParameterValues[static_cast<size_t> (i)];

            if (streamPrimed && std::abs (value - last) < 1.0e-4f)
                continue;

            last = value;

            if (const auto* parameter = state.getParameter (id))
                fresh.add ("param " + juce::String (id) + " = " + parameter->getCurrentValueAsText());
        }

        const auto note = processor.getLastMidiNote();
        const auto velocity = processor.getLastMidiVelocity();

        if (note != streamNote || velocity != streamVelocity)
        {
            streamNote = note;
            streamVelocity = velocity;

            if (note >= 0)
                fresh.add ("midi note " + juce::MidiMessage::getMidiNoteName (note, true, true, 3)
                           + " (" + juce::String (note) + ")  vel " + juce::String (velocity));
        }

        const auto controller = processor.getLastMidiController();
        const auto controllerValue = processor.getLastMidiControllerValue();

        if (controller != streamController || controllerValue != streamControllerValue)
        {
            streamController = controller;
            streamControllerValue = controllerValue;

            if (controller >= 0)
                fresh.add ("midi cc" + juce::String (controller) + " = " + juce::String (controllerValue));
        }

        const auto voices = processor.getActiveVoiceCount();

        if (voices != streamVoices)
        {
            streamVoices = voices;
            const auto outDb = juce::Decibels::gainToDecibels (processor.getSmoothedOutputPeak(), -100.0f);
            fresh.add ("voices " + juce::String (voices) + "  out "
                       + (outDb <= -100.0f ? juce::String ("-inf") : juce::String (outDb, 1))
                       + " dB  cpu " + juce::String (processor.getMeasuredCpuLoad() * 100.0, 1) + "%");
        }

        const auto targets = deriveMorphTargets (processor.getEffectiveMorph());

        if (targets.grainsPerVoice != streamGrains)
        {
            streamGrains = targets.grainsPerVoice;
            fresh.add ("grains " + juce::String (targets.grainsPerVoice)
                       + "  dur " + juce::String (juce::roundToInt (targets.grainDurationMs)) + "ms"
                       + "  verb " + juce::String (targets.reverbSend, 2)
                       + "  chor " + juce::String (targets.chorusDepth, 2));
        }

        if (! juce::approximatelyEqual (processor.getCurrentSampleRate(), streamSampleRate)
            || processor.getCurrentBlockSize() != streamBlockSize)
        {
            streamSampleRate = processor.getCurrentSampleRate();
            streamBlockSize = processor.getCurrentBlockSize();
            fresh.add ("audio " + juce::String (streamSampleRate / 1000.0, 1) + "kHz  block "
                       + juce::String (streamBlockSize) + "  " + (processor.isPro() ? "pro" : "free"));
        }

        if (processor.getCurrentPresetName() != streamPresetName)
        {
            streamPresetName = processor.getCurrentPresetName();
            fresh.add ("preset \"" + streamPresetName + "\"");
        }

        streamPrimed = true;

        for (const auto& line : fresh)
            dataStream.push (line);
    }
}
