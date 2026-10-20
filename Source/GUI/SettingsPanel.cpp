#include "SettingsPanel.h"

#include "../PluginProcessor.h"
#include "../Utility/MotionPreferences.h"

#include <initializer_list>

namespace texture
{
    namespace
    {
        constexpr const char* circuitDriftUrl = "https://djshellshoxxx.github.io/circuitdriftlabs/";
        constexpr const char* buyUrl = "https://texture-synth.com/pro";
        constexpr const char* githubUrl = "https://github.com/djshellshoxxx/texture-vst";
        constexpr const char* cloudUrl = "https://texture-synth.com/community-presets";
        constexpr const char* supportEmail = "support@texture-synth.com";

        constexpr const char* tooltipsKey = "show_tooltips";
        constexpr const char* reducedMotionKey = "reduced_motion";
        constexpr const char* defaultGainKey = "default_gain_db";
        constexpr const char* sampleFolderKey = "sample_folder";

        constexpr int slideTickMs = 1000 / 60;
        constexpr float slideSpeed = 0.18f;

        juce::String versionString()
        {
           #if defined (JucePlugin_VersionString)
            return JucePlugin_VersionString;
           #else
            return "0.0.1";
           #endif
        }

        void styleFieldLabel (juce::Label& label)
        {
            label.setFont (TextureLookAndFeel::labelFont());
            label.setColour (juce::Label::textColourId, colours::textMuted);
        }

        void styleBodyLabel (juce::Label& label)
        {
            label.setFont (TextureLookAndFeel::interfaceFont (11.0f));
            label.setColour (juce::Label::textColourId, colours::textMuted);
            label.setJustificationType (juce::Justification::topLeft);
        }
    }

    SettingsPanel::SettingsPanel (TextureAudioProcessor& processorToUse)
        : processor (processorToUse)
    {
        setOpaque (false);
        setWantsKeyboardFocus (true);
        setVisible (false);

        // --- Tab bar ----------------------------------------------------------
        for (auto* button : { &licenseTabButton, &preferencesTabButton, &aboutTabButton })
        {
            button->setClickingTogglesState (false);
            addAndMakeVisible (button);
        }

        licenseTabButton.onClick     = [this] { setTab (Tab::license); };
        preferencesTabButton.onClick = [this] { setTab (Tab::preferences); };
        aboutTabButton.onClick       = [this] { setTab (Tab::about); };

        closeButton.setTooltip ("Close settings (Esc)");
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible (closeButton);

        // --- Licence tab -------------------------------------------------------
        styleBodyLabel (licenseBlurb);
        licenseBlurb.setText ("TEXTURE Pro unlocks the preset browser, dual-layer, MORPH LFO "
                              "and WAV export for $10 one-time. Buy at texture-synth.com/pro, "
                              "then enter the email you bought with and the key you were sent.",
                              juce::dontSendNotification);
        addAndMakeVisible (licenseBlurb);

        styleFieldLabel (emailLabel);
        addAndMakeVisible (emailLabel);

        emailField.setFont (TextureLookAndFeel::interfaceFont (12.0f));
        emailField.setTextToShowWhenEmpty ("you@example.com", colours::textMuted);
        emailField.setInputRestrictions (LicenseManager::maxFieldLength);
        emailField.setTitle ("Licence email");
        addAndMakeVisible (emailField);

        styleFieldLabel (keyLabel);
        addAndMakeVisible (keyLabel);

        keyField.setFont (TextureLookAndFeel::monoFont (12.0f));
        keyField.setTextToShowWhenEmpty ("XXXX-XXXX-XXXX-XXXX", colours::textMuted);
        keyField.setInputRestrictions (LicenseManager::maxFieldLength);
        keyField.setTitle ("Licence key");
        addAndMakeVisible (keyField);

        activateButton.setTooltip ("Validate the email and key offline");
        activateButton.onClick = [this] { attemptActivation(); };
        addAndMakeVisible (activateButton);

        restoreButton.setTooltip ("Re-check the key already stored on this machine");
        restoreButton.onClick = [this] { attemptRestore(); };
        addAndMakeVisible (restoreButton);

        deactivateButton.setTooltip ("Remove the stored licence from this machine");
        deactivateButton.onClick = [this]
        {
            processor.getLicenseManager().deactivate();
            processor.enforceTierLimits();
            refreshLicenseState();
            processor.postToast ("Licence removed from this machine.", SampleLoader::Severity::info);

            if (onTierChanged)
                onTierChanged();
        };
        addAndMakeVisible (deactivateButton);

        buyButton.setTooltip (buyUrl);
        buyButton.onClick = [] { juce::URL (buyUrl).launchInDefaultBrowser(); };
        addAndMakeVisible (buyButton);

        styleBodyLabel (licenseStatus);
        addAndMakeVisible (licenseStatus);

        // --- Preferences tab ---------------------------------------------------
        tooltipsToggle.setTooltip ("Turn the hover tooltips on or off");
        tooltipsToggle.onClick = [this]
        {
            const auto enabled = tooltipsToggle.getToggleState();

            if (auto* store = processor.getApplicationProperties().getUserSettings())
            {
                store->setValue (tooltipsKey, enabled);
                store->saveIfNeeded();
            }

            if (onTooltipsChanged)
                onTooltipsChanged (enabled);
        };
        addAndMakeVisible (tooltipsToggle);

        reducedMotionToggle.setTooltip ("Disable optional interface animation while keeping meters and state feedback active");
        reducedMotionToggle.onClick = [this]
        {
            const auto reduced = reducedMotionToggle.getToggleState();
            MotionPreferences::setReducedMotion (reduced);

            if (auto* store = processor.getApplicationProperties().getUserSettings())
            {
                store->setValue (reducedMotionKey, reduced);
                store->saveIfNeeded();
            }
        };
        addAndMakeVisible (reducedMotionToggle);

        styleBodyLabel (presetFolderLabel);
        addAndMakeVisible (presetFolderLabel);

        presetFolderButton.onClick = [this] { chooseFolder (false); };
        addAndMakeVisible (presetFolderButton);

        revealPresetFolderButton.setTooltip ("Open the preset folder in your file browser");
        revealPresetFolderButton.onClick = [this]
        {
            auto folder = processor.getPresetManager().getPresetSearchRoot();
            folder.createDirectory();
            folder.revealToUser();
        };
        addAndMakeVisible (revealPresetFolderButton);

        styleBodyLabel (sampleFolderLabel);
        addAndMakeVisible (sampleFolderLabel);

        sampleFolderButton.onClick = [this] { chooseFolder (true); };
        addAndMakeVisible (sampleFolderButton);

        revealSampleFolderButton.setTooltip ("Open the sample folder in your file browser");
        revealSampleFolderButton.onClick = [this]
        {
            auto folder = PresetManager::getFactorySampleDirectory();

            if (auto* store = processor.getApplicationProperties().getUserSettings())
            {
                const juce::File configured (store->getValue (sampleFolderKey));

                if (configured.isDirectory())
                    folder = configured;
            }

            folder.createDirectory();
            folder.revealToUser();
        };
        addAndMakeVisible (revealSampleFolderButton);

        styleFieldLabel (defaultGainLabel);
        addAndMakeVisible (defaultGainLabel);

        defaultGainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        defaultGainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        defaultGainSlider.setRange (params::gainMinDb, params::gainMaxDb, 0.1);
        defaultGainSlider.setTextValueSuffix (" dB");
        defaultGainSlider.setTooltip ("Output gain every new instance of TEXTURE starts with. "
                                      "Saved sessions keep their own gain.");
        defaultGainSlider.onValueChange = [this]
        {
            if (auto* store = processor.getApplicationProperties().getUserSettings())
            {
                store->setValue (defaultGainKey, defaultGainSlider.getValue());
                store->saveIfNeeded();
            }
        };
        addAndMakeVisible (defaultGainSlider);

        audioSettingsButton.setTooltip ("Choose the audio device and MIDI inputs "
                                        "(standalone only; in a plugin the host owns this)");
        audioSettingsButton.onClick = [this]
        {
            if (onAudioSettingsRequested)
                onAudioSettingsRequested();
        };
        addAndMakeVisible (audioSettingsButton);

        clearMappingsButton.setTooltip ("Forget every learned MIDI CC assignment");
        clearMappingsButton.onClick = [this]
        {
            processor.getMidiLearn().clearAll();
            processor.postToast ("All MIDI mappings cleared.", SampleLoader::Severity::info);
        };
        addAndMakeVisible (clearMappingsButton);

        styleFieldLabel (randomModeLabel);
        addAndMakeVisible (randomModeLabel);
        randomModeBox = std::make_unique<ParameterChoiceBox> (
            processor, params::randomMode, juce::StringArray { "Legacy", "Repeat", "Evolve" },
            "Legacy keeps the original sound. Repeat reproduces each note from the seed. Evolve varies repeated notes deterministically.");
        addAndMakeVisible (*randomModeBox);

        styleFieldLabel (randomSeedLabel);
        addAndMakeVisible (randomSeedLabel);
        randomSeedField.setFont (TextureLookAndFeel::monoFont (12.0f));
        randomSeedField.setInputRestrictions (16, "0123456789abcdefABCDEF");
        randomSeedField.setTitle ("16 digit hexadecimal random seed");
        randomSeedField.setTooltip ("Patch seed used by Repeat and Evolve modes. The seed is saved with the project.");
        randomSeedField.onReturnKey = [this] { commitRandomSeedField(); };
        randomSeedField.onFocusLost = [this] { commitRandomSeedField(); };
        addAndMakeVisible (randomSeedField);

        copySeedButton.setTooltip ("Copy the current 16 digit seed to the clipboard");
        copySeedButton.onClick = [this]
        {
            juce::SystemClipboard::copyTextToClipboard (VersionedRandomStreams::seedToHex (processor.getRandomSeed()));
            randomSeedStatus.setText ("Seed copied.", juce::dontSendNotification);
        };
        addAndMakeVisible (copySeedButton);

        newSeedButton.setTooltip ("Generate a new seed for Repeat and Evolve modes");
        newSeedButton.onClick = [this]
        {
            processor.generateNewRandomSeed();
            refreshRandomSeedField();
            randomSeedStatus.setText ("New seed saved with this patch.", juce::dontSendNotification);
        };
        addAndMakeVisible (newSeedButton);

        resetEvolutionButton.setTooltip ("Restart the Evolve note sequence at the next audio block");
        resetEvolutionButton.onClick = [this]
        {
            processor.resetRandomEvolution();
            randomSeedStatus.setText ("Evolution will restart at the next audio block.", juce::dontSendNotification);
        };
        addAndMakeVisible (resetEvolutionButton);

        styleBodyLabel (randomSeedStatus);
        addAndMakeVisible (randomSeedStatus);
        refreshRandomSeedField();

        // --- About tab ---------------------------------------------------------
        styleBodyLabel (aboutText);
        aboutText.setText (
            "TEXTURE " + versionString() + "\n"
            "A drag-and-drop granular pad synthesizer.\n\n"
            "Drop a sample. Play a chord. It sounds finished.\n\n"
            "Built with JUCE. Interface typeface: Inter (SIL Open Font License).\n"
            "TEXTURE source code and original project materials are proprietary. "
            "Third-party components remain under their own licences.\n\n"
            "TEXTURE never contacts a server. Licence validation is entirely offline and "
            "no analytics, telemetry or crash data leave your machine.\n\n"
            "Developer contact: " + juce::String (supportEmail),
            juce::dontSendNotification);
        addAndMakeVisible (aboutText);

        aboutHomepageButton.onClick = [] { juce::URL (circuitDriftUrl).launchInDefaultBrowser(); };
        addAndMakeVisible (aboutHomepageButton);

        aboutGithubButton.onClick = [] { juce::URL (githubUrl).launchInDefaultBrowser(); };
        addAndMakeVisible (aboutGithubButton);

        aboutSupportButton.onClick = []
        {
            juce::URL (juce::String ("mailto:") + supportEmail).launchInDefaultBrowser();
        };
        addAndMakeVisible (aboutSupportButton);

        aboutCloudButton.setTooltip ("Browse presets shared by other TEXTURE users (Pro)");
        aboutCloudButton.onClick = [this]
        {
            if (! processor.isPro())
            {
                setTab (Tab::license);
                processor.postToast ("Community presets are a Pro feature.", SampleLoader::Severity::info);
                return;
            }

            juce::URL (cloudUrl).launchInDefaultBrowser();
        };
        addAndMakeVisible (aboutCloudButton);

        // --- Restore persisted preferences ------------------------------------
        if (auto* store = processor.getApplicationProperties().getUserSettings())
        {
            tooltipsToggle.setToggleState (store->getBoolValue (tooltipsKey, true),
                                           juce::dontSendNotification);
            const auto reducedMotion = store->getBoolValue (reducedMotionKey, false);
            reducedMotionToggle.setToggleState (reducedMotion, juce::dontSendNotification);
            MotionPreferences::setReducedMotion (reducedMotion);
            defaultGainSlider.setValue (store->getDoubleValue (defaultGainKey, params::gainDefaultDb),
                                        juce::dontSendNotification);
        }
        else
        {
            tooltipsToggle.setToggleState (true, juce::dontSendNotification);
            reducedMotionToggle.setToggleState (false, juce::dontSendNotification);
            MotionPreferences::setReducedMotion (false);
            defaultGainSlider.setValue (params::gainDefaultDb, juce::dontSendNotification);
        }

        refreshLicenseState();
        setTab (Tab::license);
    }


    void SettingsPanel::refreshRandomSeedField()
    {
        randomSeedField.setText (VersionedRandomStreams::seedToHex (processor.getRandomSeed()),
                                 juce::dontSendNotification);
    }

    void SettingsPanel::commitRandomSeedField()
    {
        std::uint64_t seed = 0;
        const auto text = randomSeedField.getText().trim().toStdString();
        if (! VersionedRandomStreams::parseSeedHex (text, seed))
        {
            randomSeedStatus.setText ("Enter exactly 16 hexadecimal digits.", juce::dontSendNotification);
            refreshRandomSeedField();
            return;
        }

        processor.setRandomSeed (seed);
        refreshRandomSeedField();
        randomSeedStatus.setText ("Seed saved with this patch.", juce::dontSendNotification);
    }

    SettingsPanel::~SettingsPanel()
    {
        stopTimer();
    }

    void SettingsPanel::open (Tab tab)
    {
        setTab (tab);

        // Place it before it is shown, or it flashes at its old bounds for the
        // frame before the slide timer's first tick.
        updateSlideBounds();

        setVisible (true);
        toFront (true);
        targetOffset = 0.0f;

        if (MotionPreferences::isReducedMotion())
        {
            currentOffset = targetOffset;
            updateSlideBounds();
        }
        else
        {
            startTimer (slideTickMs);
        }

        grabKeyboardFocus();
    }

    void SettingsPanel::close()
    {
        targetOffset = 1.0f;

        if (MotionPreferences::isReducedMotion())
        {
            currentOffset = targetOffset;
            updateSlideBounds();
            setVisible (false);

            if (onClosed)
                onClosed();

            return;
        }

        startTimer (slideTickMs);
    }

    void SettingsPanel::timerCallback()
    {
        if (MotionPreferences::isReducedMotion())
        {
            currentOffset = targetOffset;
            stopTimer();
            updateSlideBounds();

            if (targetOffset >= 1.0f)
            {
                setVisible (false);

                if (onClosed)
                    onClosed();
            }

            return;
        }

        currentOffset += (targetOffset - currentOffset) * slideSpeed;

        if (std::abs (targetOffset - currentOffset) < 0.005f)
        {
            currentOffset = targetOffset;
            stopTimer();

            if (targetOffset >= 1.0f)
            {
                setVisible (false);

                if (onClosed)
                    onClosed();
            }
        }

        // The slide only moves the panel; its contents are laid out from the local
        // bounds and do not need rebuilding on every animation tick.
        updateSlideBounds();
        repaint();
    }

    void SettingsPanel::setTab (Tab tab)
    {
        currentTab = tab;

        licenseTabButton.setToggleState (tab == Tab::license, juce::dontSendNotification);
        preferencesTabButton.setToggleState (tab == Tab::preferences, juce::dontSendNotification);
        aboutTabButton.setToggleState (tab == Tab::about, juce::dontSendNotification);

        const auto isLicense = tab == Tab::license;
        const auto isPreferences = tab == Tab::preferences;
        const auto isAbout = tab == Tab::about;

        for (auto* component : std::initializer_list<juce::Component*> {
                 &licenseBlurb, &emailLabel, &emailField, &keyLabel, &keyField,
                 &activateButton, &restoreButton, &deactivateButton,
                 &buyButton, &licenseStatus })
            component->setVisible (isLicense);

        for (auto* component : std::initializer_list<juce::Component*> {
                 &tooltipsToggle, &reducedMotionToggle,
                 &presetFolderLabel, &presetFolderButton, &revealPresetFolderButton,
                 &sampleFolderLabel, &sampleFolderButton, &revealSampleFolderButton,
                 &defaultGainLabel, &defaultGainSlider,
                 &audioSettingsButton, &clearMappingsButton, &randomModeLabel,
                 randomModeBox.get(), &randomSeedLabel, &randomSeedField, &copySeedButton,
                 &newSeedButton, &resetEvolutionButton, &randomSeedStatus })
            component->setVisible (isPreferences);

        for (auto* component : std::initializer_list<juce::Component*> {
                 &aboutText, &aboutHomepageButton, &aboutGithubButton,
                 &aboutSupportButton, &aboutCloudButton })
            component->setVisible (isAbout);

        if (isLicense)
            refreshLicenseState();

        if (isPreferences)
        {
            refreshRandomSeedField();
            presetFolderLabel.setText ("Presets: " + processor.getPresetManager()
                                                          .getPresetSearchRoot().getFullPathName(),
                                       juce::dontSendNotification);

            auto sampleFolder = PresetManager::getFactorySampleDirectory().getFullPathName();

            if (auto* store = processor.getApplicationProperties().getUserSettings())
            {
                const auto configured = store->getValue (sampleFolderKey);

                if (configured.isNotEmpty())
                    sampleFolder = configured;
            }

            sampleFolderLabel.setText ("Samples: " + sampleFolder + (processor.isPro() ? "" : "   (Pro)"),
                                       juce::dontSendNotification);
            sampleFolderButton.setEnabled (processor.isPro());
        }

        resized();
        repaint();
    }

    void SettingsPanel::refreshLicenseState()
    {
        auto& license = processor.getLicenseManager();
        const auto pro = license.isPro();

        if (pro)
        {
            licenseStatus.setColour (juce::Label::textColourId, colours::success);
            licenseStatus.setText ("PRO is active for " + license.getStoredEmail()
                                       + "\nKey " + license.getMaskedKey(),
                                   juce::dontSendNotification);
        }
        else
        {
            licenseStatus.setColour (juce::Label::textColourId, colours::textMuted);
            licenseStatus.setText ("Free tier. Everything you can hear stays unlocked forever - "
                                   "Pro adds content and convenience, not sound quality.",
                                   juce::dontSendNotification);
        }

        if (license.getStoredEmail().isNotEmpty() && emailField.isEmpty())
            emailField.setText (license.getStoredEmail(), false);

        deactivateButton.setEnabled (pro);
        buyButton.setEnabled (! pro);
    }

    void SettingsPanel::attemptActivation()
    {
        auto& license = processor.getLicenseManager();
        const auto result = license.activate (emailField.getText(), keyField.getText());
        const auto message = LicenseManager::describe (result);

        if (result == LicenseManager::Result::activated)
        {
            keyField.setText (license.getStoredKey(), false);
            processor.enforceTierLimits();
            processor.postToast (message, SampleLoader::Severity::info);

            if (onTierChanged)
                onTierChanged();
        }
        else
        {
            licenseStatus.setColour (juce::Label::textColourId, colours::error);
            licenseStatus.setText (message, juce::dontSendNotification);
            logWarn ("license", "Activation failed: " + message);
            return;
        }

        refreshLicenseState();
    }

    void SettingsPanel::attemptRestore()
    {
        auto& license = processor.getLicenseManager();
        license.refreshFromStorage();
        const auto result = license.restore();

        if (result == LicenseManager::Result::activated)
        {
            processor.enforceTierLimits();
            processor.postToast ("Licence restored.", SampleLoader::Severity::info);

            if (onTierChanged)
                onTierChanged();
        }
        else
        {
            licenseStatus.setColour (juce::Label::textColourId, colours::error);
            licenseStatus.setText (result == LicenseManager::Result::emptyFields
                                       ? "No licence is stored on this machine yet."
                                       : LicenseManager::describe (result),
                                   juce::dontSendNotification);
            return;
        }

        refreshLicenseState();
    }

    void SettingsPanel::chooseFolder (bool isSampleFolder)
    {
        const auto start = isSampleFolder ? PresetManager::getFactorySampleDirectory()
                                          : processor.getPresetManager().getPresetSearchRoot();

        fileChooser = std::make_unique<juce::FileChooser> (isSampleFolder ? "Choose a sample folder"
                                                                          : "Choose a preset folder",
                                                            start);

        juce::Component::SafePointer<SettingsPanel> safeThis (this);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectDirectories,
                                  [safeThis, isSampleFolder] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto folder = chooser.getResult();

            if (! folder.isDirectory())
                return;

            if (isSampleFolder)
            {
                if (auto* store = safeThis->processor.getApplicationProperties().getUserSettings())
                {
                    store->setValue (sampleFolderKey, folder.getFullPathName());
                    store->saveIfNeeded();
                }
            }
            else
            {
                safeThis->processor.getPresetManager().setPresetSearchRoot (folder);
            }

            safeThis->setTab (Tab::preferences);
            safeThis->processor.postToast ("Folder set to " + folder.getFullPathName(),
                                           SampleLoader::Severity::info);
        });
    }

    bool SettingsPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }

        return false;
    }

    void SettingsPanel::mouseDown (const juce::MouseEvent&)
    {
        // Swallow clicks so they never reach the controls behind the overlay.
    }

    void SettingsPanel::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (colours::shadow);
        g.fillRect (bounds.removeFromLeft (6.0f));

        g.setColour (colours::panel);
        g.fillRect (bounds);

        g.setColour (colours::panelEdge);
        g.drawLine (bounds.getX(), bounds.getY(), bounds.getX(), bounds.getBottom(), 1.0f);

        auto header = bounds.removeFromTop (34.0f);

        g.setColour (colours::textPrimary);
        g.setFont (TextureLookAndFeel::interfaceFont (14.0f, true));
        g.drawText ("SETTINGS", header.reduced (layout::grid * 2.0f, 0.0f),
                    juce::Justification::centredLeft, false);

        g.setColour (colours::panelEdge);
        g.drawLine (bounds.getX() + 8.0f, header.getBottom(), bounds.getRight() - 8.0f,
                    header.getBottom(), 1.0f);
    }

    void SettingsPanel::updateSlideBounds()
    {
        // Deliberately not inside resized(): a component that assigns its own
        // bounds from resized() re-enters it, and juce::getParentWidth() falls back
        // to the monitor area when there is no parent, so the answer changed on
        // every pass and the recursion never terminated.
        auto* parent = getParentComponent();

        if (parent == nullptr)
            return;

        const auto width = juce::jmin (layout::settingsPanelWidth, parent->getWidth());
        const auto slideX = parent->getWidth() - width
                          + juce::roundToInt (currentOffset * static_cast<float> (width));

        setBounds (slideX, 0, width, parent->getHeight());
    }

    void SettingsPanel::resized()
    {

        auto bounds = getLocalBounds();
        bounds.removeFromLeft (6);   // shadow gutter
        bounds.removeFromTop (34);
        bounds = bounds.reduced (layout::grid * 2, layout::grid);

        auto tabRow = bounds.removeFromTop (26);
        const auto tabWidth = tabRow.getWidth() / 3;
        licenseTabButton.setBounds (tabRow.removeFromLeft (tabWidth).reduced (1, 0));
        preferencesTabButton.setBounds (tabRow.removeFromLeft (tabWidth).reduced (1, 0));
        aboutTabButton.setBounds (tabRow.reduced (1, 0));

        bounds.removeFromTop (layout::grid);

        auto footer = bounds.removeFromBottom (28);
        closeButton.setBounds (footer.removeFromRight (80));

        bounds.removeFromBottom (layout::grid);

        layoutCurrentTab();

        // Lay the content out inside the remaining area.
        auto content = bounds;

        switch (currentTab)
        {
            case Tab::license:
            {
                licenseBlurb.setBounds (content.removeFromTop (76));
                content.removeFromTop (layout::grid);

                emailLabel.setBounds (content.removeFromTop (14));
                emailField.setBounds (content.removeFromTop (26));
                content.removeFromTop (layout::grid);

                keyLabel.setBounds (content.removeFromTop (14));
                keyField.setBounds (content.removeFromTop (26));
                content.removeFromTop (layout::grid);

                auto buttonRow = content.removeFromTop (28);
                activateButton.setBounds (buttonRow.removeFromLeft (90));
                buttonRow.removeFromLeft (4);
                restoreButton.setBounds (buttonRow.removeFromLeft (80));
                buttonRow.removeFromLeft (4);
                deactivateButton.setBounds (buttonRow);

                content.removeFromTop (layout::grid);
                buyButton.setBounds (content.removeFromTop (28));
                content.removeFromTop (layout::grid);
                licenseStatus.setBounds (content);
                break;
            }

            case Tab::preferences:
            {
                tooltipsToggle.setBounds (content.removeFromTop (22));
                reducedMotionToggle.setBounds (content.removeFromTop (22));
                content.removeFromTop (4);

                presetFolderLabel.setBounds (content.removeFromTop (22));
                auto presetRow = content.removeFromTop (24);
                presetFolderButton.setBounds (presetRow.removeFromLeft (presetRow.getWidth() / 2 - 2));
                presetRow.removeFromLeft (4);
                revealPresetFolderButton.setBounds (presetRow);
                content.removeFromTop (4);

                sampleFolderLabel.setBounds (content.removeFromTop (22));
                auto sampleRow = content.removeFromTop (24);
                sampleFolderButton.setBounds (sampleRow.removeFromLeft (sampleRow.getWidth() / 2 - 2));
                sampleRow.removeFromLeft (4);
                revealSampleFolderButton.setBounds (sampleRow);
                content.removeFromTop (4);

                defaultGainLabel.setBounds (content.removeFromTop (14));
                defaultGainSlider.setBounds (content.removeFromTop (24));
                content.removeFromTop (4);

                auto modeRow = content.removeFromTop (24);
                randomModeLabel.setBounds (modeRow.removeFromLeft (92));
                randomModeBox->setBounds (modeRow);
                auto seedRow = content.removeFromTop (24);
                randomSeedLabel.setBounds (seedRow.removeFromLeft (40));
                randomSeedField.setBounds (seedRow.removeFromLeft (104));
                seedRow.removeFromLeft (3);
                copySeedButton.setBounds (seedRow.removeFromLeft (48));
                seedRow.removeFromLeft (3);
                newSeedButton.setBounds (seedRow.removeFromLeft (60));
                resetEvolutionButton.setBounds (content.removeFromTop (24));
                randomSeedStatus.setBounds (content.removeFromTop (16));
                content.removeFromTop (4);

                auto actionRow = content.removeFromTop (24);
                audioSettingsButton.setBounds (actionRow.removeFromLeft (actionRow.getWidth() / 2 - 2));
                actionRow.removeFromLeft (4);
                clearMappingsButton.setBounds (actionRow);
                break;
            }

            case Tab::about:
            {
                auto buttonRow = content.removeFromBottom (28);
                aboutCloudButton.setBounds (buttonRow);
                content.removeFromBottom (4);

                auto linkRow = content.removeFromBottom (28);
                const auto linkWidth = linkRow.getWidth() / 3;
                aboutHomepageButton.setBounds (linkRow.removeFromLeft (linkWidth).reduced (1, 0));
                aboutGithubButton.setBounds (linkRow.removeFromLeft (linkWidth).reduced (1, 0));
                aboutSupportButton.setBounds (linkRow.reduced (1, 0));

                content.removeFromBottom (layout::grid);
                aboutText.setBounds (content);
                break;
            }
        }
    }

    void SettingsPanel::layoutCurrentTab()
    {
        // The audio device dialog only exists in the standalone build; in a plugin
        // the host owns the device, so the button explains itself rather than lying.
        const auto standalone = processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone;

        audioSettingsButton.setEnabled (standalone);
        audioSettingsButton.setButtonText (standalone ? "Audio / MIDI setup..."
                                                      : "Audio / MIDI setup (host-controlled)");
    }
}
