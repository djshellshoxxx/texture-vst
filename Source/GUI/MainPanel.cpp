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
            return "1.0.0";
           #endif
        }

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
        : processor (processorToUse)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);
        setSize (designWidth, designHeight);

        buildHeader();
        buildControls();
        buildOverlays();
        applyFocusOrder();

        processor.addListener (this);

        updateProGating();
        pluginStateChanged();

        startTimerHz (layout::refreshHz);
    }

    MainPanel::~MainPanel()
    {
        stopTimer();
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

        fileMenuButton.onClick = [this] { showFileMenu(); };
        addAndMakeVisible (fileMenuButton);

        randomiseButton.onClick = [this] { processor.randomiseParameters(); };
        addAndMakeVisible (randomiseButton);

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

        for (const auto* name : params::divisionNames)
            lfoDivisionBox.addItem (name, lfoDivisionBox.getNumItems() + 1);

        lfoDivisionBox.setTooltip ("Tempo division used while sync is on. (Pro)");
        addAndMakeVisible (lfoDivisionBox);

        lfoDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.getValueTreeState(), params::lfoDivision, lfoDivisionBox);

        layerBlendKnob = std::make_unique<ParameterKnob> (processor, params::layerBlend, "BLEND",
                                                           "Blends the second sample layer in. Both layers "
                                                           "share one MORPH knob. (Pro)");
        addAndMakeVisible (*layerBlendKnob);

        addAndMakeVisible (envelopeHeader);
        addAndMakeVisible (outputHeader);
        addAndMakeVisible (lfoHeader);
        addAndMakeVisible (layerHeader);

        lfoProBadge.setTooltip ("The MORPH LFO is a Pro feature. Click to open the licence panel.");
        lfoProBadge.onClick = [this] { showProUpsell ("The MORPH LFO"); };
        addAndMakeVisible (lfoProBadge);

        layerProBadge.setTooltip ("Dual-layer is a Pro feature. Click to open the licence panel.");
        layerProBadge.onClick = [this] { showProUpsell ("Dual-layer"); };
        addAndMakeVisible (layerProBadge);

        addAndMakeVisible (dataStream);
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
        place (&lfoDivisionBox);

        place (dropZoneA.get());
        place (dropZoneB.get());
        place (layerBlendKnob.get());

        place (&presetNameButton);
        place (&compareAButton);
        place (&compareBButton);
        place (&fileMenuButton);
        place (&randomiseButton);
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
        auto bounds = getLocalBounds();

        // --- Header -----------------------------------------------------------
        auto header = bounds.removeFromTop (headerHeight);
        auto headerContent = header.reduced (layout::windowEdgePadding, layout::grid);

        levelLed.setBounds (headerContent.removeFromLeft (14).withSizeKeepingCentre (12, 12));
        headerContent.removeFromLeft (layout::grid);

        // The hidden effect lives behind one pixel at the foot of the wordmark.
        const auto wordmarkArea = headerContent.removeFromLeft (100);
        secretHotspot = juce::Rectangle<int> (wordmarkArea.getRight() - 4,
                                              wordmarkArea.getBottom() - 4, 3, 3);

        headerContent.removeFromLeft (layout::grid);

        tierBadge.setBounds (headerContent.removeFromRight (46).withSizeKeepingCentre (46, 18));
        headerContent.removeFromRight (layout::grid);
        gearButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (4);
        helpButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (4);
        resetButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (4);
        randomiseButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (4);
        fileMenuButton.setBounds (headerContent.removeFromRight (22));
        headerContent.removeFromRight (layout::grid);

        compareBButton.setBounds (headerContent.removeFromRight (26).withSizeKeepingCentre (26, 18));
        compareAButton.setBounds (headerContent.removeFromRight (26).withSizeKeepingCentre (26, 18));
        headerContent.removeFromRight (layout::grid);

        presetNameButton.setBounds (headerContent.withSizeKeepingCentre (
            juce::jmax (80, headerContent.getWidth()), 20));

        // --- Footer -----------------------------------------------------------
        auto footer = bounds.removeFromBottom (footerHeight);
        toast.setBounds (footer.reduced (layout::windowEdgePadding, 1));

        // --- Body -------------------------------------------------------------
        auto body = bounds.reduced (layout::windowEdgePadding, layout::grid);

        auto topRow = body.removeFromTop (196);
        auto sampleArea = topRow.removeFromLeft (348);
        topRow.removeFromLeft (layout::grid * 2);

        const auto layersVisible = secretPanel != nullptr && secretPanel->isVisible();
        juce::ignoreUnused (layersVisible);

        // Layer 2 gets a slim strip under layer 1 so the Pro feature is discoverable.
        auto layerStrip = sampleArea.removeFromBottom (62);
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
        auto controlRow = body.removeFromTop (78);

        auto envelopeArea = controlRow.removeFromLeft (232);
        envelopeHeader.setBounds (envelopeArea.removeFromTop (14));
        envelopeArea.removeFromTop (2);

        const auto adsrWidth = envelopeArea.getWidth() / 4;

        for (auto* knob : { attackKnob.get(), decayKnob.get(), sustainKnob.get(), releaseKnob.get() })
            knob->setBounds (envelopeArea.removeFromLeft (adsrWidth).reduced (4, 0));

        controlRow.removeFromLeft (layout::grid * 2);

        auto outputArea = controlRow.removeFromLeft (170);
        outputHeader.setBounds (outputArea.removeFromTop (14));
        outputArea.removeFromTop (2);

        reverbToggle->setBounds (outputArea.removeFromLeft (84).withSizeKeepingCentre (84, 26));
        outputArea.removeFromLeft (layout::grid);
        gainKnob->setBounds (outputArea.removeFromLeft (56).reduced (2, 0));

        controlRow.removeFromLeft (layout::grid);
        dataStream.setBounds (controlRow);

        body.removeFromTop (layout::grid);

        // --- LFO section -------------------------------------------------------
        auto lfoArea = body.removeFromTop (juce::jmax (52, body.getHeight()));

        if (secretPanel != nullptr && secretPanel->isVisible())
        {
            auto secretArea = lfoArea.removeFromRight (236);
            secretPanel->setBounds (secretArea);
            lfoArea.removeFromRight (layout::grid);
        }

        auto lfoHeaderRow = lfoArea.removeFromTop (14);
        lfoHeader.setBounds (lfoHeaderRow.removeFromLeft (100));
        lfoProBadge.setBounds (lfoHeaderRow.removeFromLeft (150).withSizeKeepingCentre (150, 14));

        lfoArea.removeFromTop (2);

        lfoRateKnob->setBounds (lfoArea.removeFromLeft (56).reduced (2, 0));
        lfoArea.removeFromLeft (4);
        lfoDepthKnob->setBounds (lfoArea.removeFromLeft (56).reduced (2, 0));
        lfoArea.removeFromLeft (layout::grid);
        lfoSyncToggle->setBounds (lfoArea.removeFromLeft (70).withSizeKeepingCentre (70, 24));
        lfoArea.removeFromLeft (layout::grid);
        lfoDivisionBox.setBounds (lfoArea.removeFromLeft (76).withSizeKeepingCentre (76, 24));

        // --- Overlays ----------------------------------------------------------
        if (presetBrowser != nullptr)
            presetBrowser->setBounds (getLocalBounds());

        if (helpPanel != nullptr)
            helpPanel->setBounds (getLocalBounds());

        if (settingsPanel != nullptr)
            settingsPanel->resized();
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
        g.setFont (TextureLookAndFeel::interfaceFont (22.0f, true));
        g.drawText ("TEXTURE",
                    juce::Rectangle<int> (layout::windowEdgePadding + 22, 0, 120, headerHeight),
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
        if (! processor.isPro())
        {
            g.setColour (colours::background.withAlpha (0.6f));

            const auto lfoVeil = juce::Rectangle<int> (layout::windowEdgePadding,
                                                       lfoHeader.getY(),
                                                       juce::jmax (0, lfoDivisionBox.getRight() - layout::windowEdgePadding),
                                                       juce::jmax (0, lfoDivisionBox.getBottom() - lfoHeader.getY()));
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

        if (key == juce::KeyPress ('r', juce::ModifierKeys::commandModifier, 0))
        {
            processor.randomiseParameters();
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

    void MainPanel::closeOverlays()
    {
        if (presetBrowser != nullptr)
            presetBrowser->setVisible (false);

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

        menu.addSectionHeader ("Presets");
        menu.addItem (1, "Open preset...", true, false);
        menu.addItem (2, "Save", true, false);
        menu.addItem (3, "Save as...", true, false);
        menu.addItem (4, processor.isPro() ? "Preset browser..." : "Preset browser...  (PRO)", true, false);
        menu.addSeparator();

        menu.addSectionHeader ("Audio");
        menu.addItem (5, processor.isPro() ? "Export audio..." : "Export audio...  (PRO)", true, false);
        menu.addSeparator();

        menu.addSectionHeader ("Snapshots");
        menu.addItem (6, "Copy A to B");
        menu.addItem (7, "Copy B to A");
        menu.addSeparator();

        menu.addSectionHeader ("General");
        menu.addItem (8, "Randomize");
        menu.addItem (9, "Reset all controls");
        menu.addItem (10, "Options...");
        menu.addItem (11, "Open location in file browser");
        menu.addItem (12, "Help");

        juce::Component::SafePointer<MainPanel> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileMenuButton),
                            [safeThis] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            switch (result)
            {
                case 1:  safeThis->openPresetFile(); break;
                case 2:  safeThis->saveCurrentPreset (false); break;
                case 3:  safeThis->saveCurrentPreset (true); break;
                case 4:  safeThis->openPresetBrowser(); break;
                case 5:  safeThis->exportAudio(); break;

                case 6:
                    safeThis->processor.copyCompareSlot (0, 1);
                    safeThis->processor.postToast ("Copied A to B.", SampleLoader::Severity::info);
                    break;

                case 7:
                    safeThis->processor.copyCompareSlot (1, 0);
                    safeThis->processor.postToast ("Copied B to A.", SampleLoader::Severity::info);
                    break;

                case 8:  safeThis->processor.randomiseParameters(); break;
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
                default: break;
            }
        });
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
        const auto existing = processor.getPresetManager().findByName (currentName);

        // Ctrl+S over an existing user preset writes straight through.
        if (! forceSaveAs && existing != nullptr && ! existing->isFactory)
        {
            processor.savePreset (existing->file, currentName, existing->category, existing->tags);
            return;
        }

        auto* window = new juce::AlertWindow ("Save preset",
                                              "Give this patch a name, a category and some tags.",
                                              juce::MessageBoxIconType::NoIcon);

        window->addTextEditor ("name", currentName == "Untitled" ? "My Pad" : currentName, "Name");
        window->addComboBox ("category", PresetManager::getCategories(), "Category");
        window->addTextEditor ("tags", processor.getCurrentPresetCategory().toLowerCase(), "Tags (comma separated)");
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

                const auto renderResult = WavExporter::render (safeThis->processor.getSampleForLayer (0),
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
        repaint();
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

        lfoDivisionBox.setEnabled (pro);
        lfoProBadge.setVisible (! pro);
        layerProBadge.setVisible (! pro);

        if (dropZoneB != nullptr)
            dropZoneB->setEnabled (pro);

        repaint();
    }

    void MainPanel::setTooltipsEnabled (bool shouldBeEnabled)
    {
        juce::ignoreUnused (shouldBeEnabled);
        // The editor owns the TooltipWindow; it reads the same preference.
    }

    // =========================================================================
    //  Timer
    // =========================================================================

    void MainPanel::timerCallback()
    {
        levelLed.setLevel (processor.getSmoothedOutputPeak());

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

        // The division dropdown only matters while sync is engaged (09_GUI_SPEC.md).
        if (const auto* sync = processor.getValueTreeState().getRawParameterValue (params::lfoSync))
            lfoDivisionBox.setVisible (sync->load() > 0.5f);

        updateDataStream();
    }

    void MainPanel::updateDataStream()
    {
        // One line per tick, cycling through the internals worth watching. The view
        // itself drops repeats, which is what makes the scroll stop when idle.
        const auto morph = processor.getEffectiveMorph();
        const auto targets = deriveMorphTargets (morph);

        juce::String line;

        switch (streamCounter % 6)
        {
            case 0:
                line = "morph " + juce::String (morph, 3)
                     + "  grains " + juce::String (targets.grainsPerVoice)
                     + "  dur " + juce::String (juce::roundToInt (targets.grainDurationMs)) + "ms";
                break;

            case 1:
                line = "voices " + juce::String (processor.getActiveVoiceCount())
                     + "/" + juce::String (GranularEngine::maxVoices)
                     + "  cpu " + juce::String (processor.getMeasuredCpuLoad() * 100.0, 1) + "%";
                break;

            case 2:
                line = "note " + juce::String (processor.getLastMidiNote())
                     + "  vel " + juce::String (processor.getLastMidiVelocity())
                     + "  cc" + juce::String (processor.getLastMidiController())
                     + " " + juce::String (processor.getLastMidiControllerValue());
                break;

            case 3:
                line = "out " + juce::String (juce::Decibels::gainToDecibels (
                           juce::jmax (1.0e-6f, processor.getSmoothedOutputPeak())), 1) + "dB"
                     + "  pos " + juce::String (processor.getReadPosition(), 3);
                break;

            case 4:
                line = "verb " + juce::String (targets.reverbSend, 2)
                     + "  chor " + juce::String (targets.chorusDepth, 2)
                     + "  sat " + juce::String (targets.saturatorDrive, 2);
                break;

            default:
                line = juce::String (processor.getCurrentSampleRate() / 1000.0, 1) + "kHz"
                     + "  blk " + juce::String (processor.getCurrentBlockSize())
                     + "  " + (processor.isPro() ? "pro" : "free");
                break;
        }

        if (dataStream.push (line))
            ++streamCounter;
        else
            ++streamCounter;
    }
}
