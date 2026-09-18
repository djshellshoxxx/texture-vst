#include "SettingsPanel.h"

#include "../PluginProcessor.h"

namespace texture
{
    namespace
    {
        constexpr const char* homepageUrl = "https://texture-synth.com";
        constexpr const char* buyUrl = "https://texture-synth.com/pro";
        constexpr const char* githubUrl = "https://github.com/djshellshoxxx/texture-vst";
        constexpr const char* cloudUrl = "https://texture-synth.com/community-presets";
        constexpr const char* supportEmail = "support@texture-synth.com";

        constexpr const char* tooltipsKey = "show_tooltips";
        constexpr const char* defaultGainKey = "default_gain_db";
        constexpr const char* sampleFolderKey = "sample_folder";

        constexpr int slideTickMs = 1000 / 60;
        constexpr float slideSpeed = 0.18f;

        juce::String versionString()
        {
           #if defined (JucePlugin_VersionString)
            return JucePlugin_VersionString;
           #else
            return "1.0.0";
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
        licenseBlurb.setText ("TEXTURE Pro unlocks 500 samples, the preset browser, dual-layer "
                              "and the MORPH LFO for $10 one-time. Buy at texture-synth.com/pro, "
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
        addAndMakeVisible (tooltipsToggle);

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
        defaultGainSlider.setTooltip ("Gain applied to new instances of TEXTURE");
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

        // --- About tab ---------------------------------------------------------
        styleBodyLabel (aboutText);
        aboutText.setText (
            "TEXTURE " + versionString() + "\n"
            "A drag-and-drop granular pad synthesizer.\n\n"
            "Drop a sample. Play a chord. It sounds finished.\n\n"
            "Built with JUCE. Interface typeface: Inter (SIL Open Font License).\n"
            "Plugin code is MIT licensed; factory samples are licensed for use in your "
            "own music but not for redistribution as samples.\n\n"
            "TEXTURE never contacts a server. Licence validation is entirely offline and "
            "no analytics, telemetry or crash data leave your machine.\n\n"
            "Support: " + juce::String (supportEmail),
            juce::dontSendNotification);
        addAndMakeVisible (aboutText);

        aboutHomepageButton.onClick = [] { juce::URL (homepageUrl).launchInDefaultBrowser(); };
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
            defaultGainSlider.setValue (store->getDoubleValue (defaultGainKey, params::gainDefaultDb),
                                        juce::dontSendNotification);
        }
        else
        {
            tooltipsToggle.setToggleState (true, juce::dontSendNotification);
            defaultGainSlider.setValue (params::gainDefaultDb, juce::dontSendNotification);
        }

        refreshLicenseState();
        setTab (Tab::license);
    }

    SettingsPanel::~SettingsPanel()
    {
        stopTimer();
    }

    void SettingsPanel::open (Tab tab)
    {
        setTab (tab);
        setVisible (true);
        toFront (true);
        targetOffset = 0.0f;
        startTimer (slideTickMs);
        grabKeyboardFocus();
    }

    void SettingsPanel::close()
    {
        targetOffset = 1.0f;
        startTimer (slideTickMs);
    }

    void SettingsPanel::timerCallback()
    {
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

        resized();
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

        for (auto* component : { static_cast<juce::Component*> (&licenseBlurb),
                                 &emailLabel, &emailField, &keyLabel, &keyField,
                                 &activateButton, &restoreButton, &deactivateButton,
                                 &buyButton, &licenseStatus })
            component->setVisible (isLicense);

        for (auto* component : { static_cast<juce::Component*> (&tooltipsToggle),
                                 &presetFolderLabel, &presetFolderButton, &revealPresetFolderButton,
                                 &sampleFolderLabel, &sampleFolderButton, &revealSampleFolderButton,
                                 &defaultGainLabel, &defaultGainSlider,
                                 &audioSettingsButton, &clearMappingsButton })
            component->setVisible (isPreferences);

        for (auto* component : { static_cast<juce::Component*> (&aboutText),
                                 &aboutHomepageButton, &aboutGithubButton,
                                 &aboutSupportButton, &aboutCloudButton })
            component->setVisible (isAbout);

        if (isLicense)
            refreshLicenseState();

        if (isPreferences)
        {
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

    void SettingsPanel::resized()
    {
        const auto width = juce::jmin (layout::settingsPanelWidth, getParentWidth());
        const auto slideX = getParentWidth() - width + juce::roundToInt (currentOffset * width);

        setBounds (slideX, 0, width, getParentHeight());

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
                tooltipsToggle.setBounds (content.removeFromTop (26));
                content.removeFromTop (layout::grid);

                presetFolderLabel.setBounds (content.removeFromTop (30));
                auto presetRow = content.removeFromTop (26);
                presetFolderButton.setBounds (presetRow.removeFromLeft (presetRow.getWidth() / 2 - 2));
                presetRow.removeFromLeft (4);
                revealPresetFolderButton.setBounds (presetRow);
                content.removeFromTop (layout::grid);

                sampleFolderLabel.setBounds (content.removeFromTop (30));
                auto sampleRow = content.removeFromTop (26);
                sampleFolderButton.setBounds (sampleRow.removeFromLeft (sampleRow.getWidth() / 2 - 2));
                sampleRow.removeFromLeft (4);
                revealSampleFolderButton.setBounds (sampleRow);
                content.removeFromTop (layout::grid);

                defaultGainLabel.setBounds (content.removeFromTop (14));
                defaultGainSlider.setBounds (content.removeFromTop (26));
                content.removeFromTop (layout::grid);

                audioSettingsButton.setBounds (content.removeFromTop (28));
                content.removeFromTop (4);
                clearMappingsButton.setBounds (content.removeFromTop (28));
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
