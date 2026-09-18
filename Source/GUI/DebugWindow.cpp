#include "DebugWindow.h"

#include "../PluginProcessor.h"

namespace texture
{
    namespace
    {
        constexpr int refreshIntervalMs = 200;

        constexpr float readoutFontHeight = 12.0f;
        constexpr int readoutLineHeight = 15;
        constexpr int readoutPadding = 8;
    }

    // =========================================================================
    //  DebugPanel::Readout
    // =========================================================================

    DebugPanel::Readout::Readout()
    {
        setInterceptsMouseClicks (false, false);
        setTitle ("Plugin internals");
        setAccessible (true);
    }

    void DebugPanel::Readout::setLines (juce::StringArray newLines, int availableWidth)
    {
        lines = std::move (newLines);

        // Grow to fit so the enclosing Viewport can scroll; the width is whatever
        // the viewport can show, because the content is never wrapped.
        setSize (juce::jmax (320, availableWidth),
                 juce::jmax (1, lines.size() * readoutLineHeight + readoutPadding * 2));

        repaint();
    }

    void DebugPanel::Readout::paint (juce::Graphics& g)
    {
        g.fillAll (colours::panel);

        g.setFont (TextureLookAndFeel::monoFont (readoutFontHeight));

        auto y = readoutPadding;

        for (const auto& line : lines)
        {
            // Section headers read as headers; everything else is plain data.
            g.setColour (line.startsWith ("===") ? colours::accentPrimary : colours::textPrimary);

            g.drawText (line,
                        readoutPadding, y, getWidth() - readoutPadding * 2, readoutLineHeight,
                        juce::Justification::centredLeft, false);

            y += readoutLineHeight;
        }
    }

    DebugPanel::DebugPanel (TextureAudioProcessor& processorToUse)
        : processor (processorToUse)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);

        readoutViewport.setViewedComponent (&readout, false);
        readoutViewport.setScrollBarsShown (true, false);
        readoutViewport.setTitle ("Live plugin internals");
        addAndMakeVisible (readoutViewport);

        explanation.setText (
            "Two different files. The troubleshooting file is a snapshot written the moment you ask "
            "for it - read it yourself, it solves most minor problems. The crash log is a running "
            "record that starts when you tick the box above and carries a copy of the troubleshooting "
            "file at its head. If TEXTURE is hard crashing, send both, with a description of what you "
            "were doing. Crash logging is off every time the plugin loads.",
            juce::dontSendNotification);
        explanation.setJustificationType (juce::Justification::topLeft);
        explanation.setFont (TextureLookAndFeel::interfaceFont (11.0f));
        explanation.setColour (juce::Label::textColourId, colours::textMuted);
        addAndMakeVisible (explanation);

        crashLogToggle.setToggleState (processor.isCrashLogEnabled(), juce::dontSendNotification);
        crashLogToggle.setTooltip ("Start a timestamped crash log in the data folder. "
                                   "Always off when the plugin loads.");
        crashLogToggle.onClick = [this]
        {
            processor.setCrashLogEnabled (crashLogToggle.getToggleState());

            if (crashLogToggle.getToggleState())
                processor.postToast ("Crash log started: " + processor.getCrashLogFile().getFileName(),
                                     SampleLoader::Severity::info);

            refreshNow();
        };
        addAndMakeVisible (crashLogToggle);

        exportButton.setTooltip ("Write a self-diagnostic with your settings, audio and MIDI "
                                 "configuration, licence state and host details");
        exportButton.onClick = [this] { exportTroubleshootingFile(); };
        addAndMakeVisible (exportButton);

        copyLogButton.setTooltip ("Copy the current session log to the clipboard");
        copyLogButton.onClick = [this]
        {
            juce::SystemClipboard::copyTextToClipboard (Logging::readLogText());
            processor.postToast ("Log copied to clipboard.", SampleLoader::Severity::info);
        };
        addAndMakeVisible (copyLogButton);

        revealLogButton.setTooltip ("Show log.txt in your file browser");
        revealLogButton.onClick = []
        {
            const auto file = Logging::getLogFile();

            if (file.existsAsFile())
                file.revealToUser();
            else
                Logging::getLogDirectory().revealToUser();
        };
        addAndMakeVisible (revealLogButton);

        resetPeakButton.setTooltip ("Clear the peak CPU reading");
        resetPeakButton.onClick = [this]
        {
            processor.resetPeakCpuLoad();
            refreshNow();
        };
        addAndMakeVisible (resetPeakButton);

        hardResetButton.setTooltip ("Destructive: clears preferences, MIDI mappings, the licence "
                                    "and cached logs");
        hardResetButton.setColour (juce::TextButton::textColourOffId, colours::error);
        hardResetButton.onClick = [this] { confirmHardReset(); };
        addAndMakeVisible (hardResetButton);

        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        refreshNow();
        startTimer (refreshIntervalMs);
    }

    DebugPanel::~DebugPanel()
    {
        stopTimer();
    }

    void DebugPanel::timerCallback()
    {
        refreshNow();
    }

    void DebugPanel::refreshNow()
    {
        juce::StringArray lines;

        lines.add ("=== LIVE STATE ===");
        lines.add ("sample rate      : " + juce::String (processor.getCurrentSampleRate(), 1) + " Hz");
        lines.add ("block size       : " + juce::String (processor.getCurrentBlockSize()));
        lines.add ("output channels  : " + juce::String (processor.getMainBusNumOutputChannels()));
        lines.add ("active voices    : " + juce::String (processor.getActiveVoiceCount())
                   + " / " + juce::String (GranularEngine::maxVoices * TextureAudioProcessor::numLayers));
        lines.add ("cpu (last block) : " + juce::String (processor.getMeasuredCpuLoad() * 100.0, 2) + " %");
        lines.add ("cpu (peak 1 s)   : " + juce::String (processor.getPeakCpuLoad() * 100.0, 2) + " %");
        lines.add ("output peak      : " + juce::String (juce::Decibels::gainToDecibels (
                                                juce::jmax (1.0e-6f, processor.getSmoothedOutputPeak())), 1) + " dBFS");
        lines.add ("read position    : " + juce::String (processor.getReadPosition(), 4));
        lines.add ("");

        lines.add ("=== MORPH ===");
        const auto morph = processor.getEffectiveMorph();
        const auto targets = deriveMorphTargets (morph);
        lines.add ("effective morph  : " + juce::String (morph, 4));
        lines.add ("grains / voice   : " + juce::String (targets.grainsPerVoice));
        lines.add ("grain duration   : " + juce::String (targets.grainDurationMs, 1) + " ms");
        lines.add ("grain overlap    : " + juce::String (targets.grainOverlap * 100.0f, 1) + " %");
        lines.add ("position jitter  : " + juce::String (targets.positionJitter, 4) + " of buffer");
        lines.add ("detune           : +/- " + juce::String (targets.detuneCents, 1) + " cents");
        lines.add ("pan spread       : " + juce::String (targets.panSpread, 3));
        lines.add ("reverb send      : " + juce::String (targets.reverbSend, 3));
        lines.add ("chorus depth     : " + juce::String (targets.chorusDepth, 3));
        lines.add ("saturator drive  : " + juce::String (targets.saturatorDrive, 3));
        lines.add ("high shelf tilt  : " + juce::String (targets.highShelfTiltDb, 2) + " dB");
        lines.add ("");

        lines.add ("=== PARAMETERS ===");

        for (const auto* id : params::allIDs)
        {
            const auto* parameter = processor.getValueTreeState().getParameter (id);
            const auto* raw = processor.getValueTreeState().getRawParameterValue (id);
            const auto mappedCc = processor.getMidiLearn().getControllerForParameter (id);

            lines.add (juce::String (id).paddedRight (' ', 17) + ": "
                       + (raw != nullptr ? juce::String (raw->load(), 4) : juce::String ("?"))
                       + (parameter != nullptr ? "  (" + parameter->getCurrentValueAsText() + ")" : juce::String())
                       + (mappedCc >= 0 ? "  [CC" + juce::String (mappedCc) + "]" : juce::String()));
        }

        lines.add ("");
        lines.add ("=== SAMPLES ===");

        for (int i = 0; i < TextureAudioProcessor::numLayers; ++i)
        {
            const auto buffer = processor.getSampleForLayer (i);

            if (buffer != nullptr && buffer->isValid())
            {
                lines.add ("layer " + juce::String (i + 1) + "          : " + buffer->getName()
                           + "  " + juce::String (buffer->getLengthSeconds(), 2) + " s"
                           + "  " + juce::String (buffer->getNumSamples()) + " frames"
                           + "  peak " + juce::String (buffer->getGlobalPeak(), 3));
                lines.add ("  path           : " + processor.getSampleFileForLayer (i).getFullPathName());
            }
            else
            {
                lines.add ("layer " + juce::String (i + 1) + "          : (empty)");
            }
        }

        lines.add ("");
        lines.add ("=== MIDI ===");
        lines.add ("last note        : " + juce::String (processor.getLastMidiNote())
                   + "  velocity " + juce::String (processor.getLastMidiVelocity()));
        lines.add ("last controller  : CC" + juce::String (processor.getLastMidiController())
                   + "  value " + juce::String (processor.getLastMidiControllerValue()));
        lines.add ("learning         : " + juce::String (processor.getMidiLearn().isLearning()
                                                              ? processor.getMidiLearn().getLearningParameterID()
                                                              : juce::String ("no")));
        lines.add ("");

        lines.add ("=== LICENCE ===");
        lines.add ("tier             : " + juce::String (processor.isPro() ? "PRO" : "FREE"));
        lines.add ("stored email     : " + (processor.getLicenseManager().getStoredEmail().isNotEmpty()
                                                ? processor.getLicenseManager().getStoredEmail()
                                                : juce::String ("(none)")));
        lines.add ("stored key       : " + processor.getLicenseManager().getMaskedKey());
        lines.add ("expected key     : " + processor.getLicenseManager().getExpectedKeyForStoredEmail());
        lines.add ("signature match  : " + juce::String (processor.isPro() ? "yes" : "no"));
        lines.add ("");

        lines.add ("=== CONFIGURATION WARNINGS ===");

        juce::StringArray warnings;

        if (processor.getSampleForLayer (0) == nullptr || ! processor.getSampleForLayer (0)->isValid())
            warnings.add ("No sample in layer 1 - the plugin will stay silent until one is loaded.");

        if (processor.getMainBusNumOutputChannels() < 2)
            warnings.add ("Output bus is mono; the stereo image is being folded down.");

        if (processor.getPeakCpuLoad() > 0.8)
            warnings.add ("Peak CPU above 80% of the block budget - raise your buffer size.");

        if (processor.getSmoothedOutputPeak() >= 1.0f)
            warnings.add ("Output is clipping - lower the Gain knob.");

        if (! processor.isPro()
            && processor.getValueTreeState().getRawParameterValue (params::lfoDepth) != nullptr
            && processor.getValueTreeState().getRawParameterValue (params::lfoDepth)->load() > 0.0f)
            warnings.add ("LFO depth is non-zero in the free tier and is being clamped.");

        if (processor.getCurrentBlockSize() > 2048)
            warnings.add ("Very large block size; note timing will feel loose.");

        lines.add (warnings.isEmpty() ? juce::String ("none") : warnings.joinIntoString ("\n"));
        lines.add ("");

        lines.add ("=== FILES ===");
        lines.add ("crash logging    : " + juce::String (processor.isCrashLogEnabled() ? "ON" : "off"));
        lines.add ("crash log        : " + (processor.getCrashLogFile() != juce::File()
                                                ? processor.getCrashLogFile().getFullPathName()
                                                : juce::String ("(not started)")));
        lines.add ("session log      : " + Logging::getLogFile().getFullPathName());
        lines.add ("data folder      : " + PresetManager::getPluginDataDirectory().getFullPathName());

        // Keep the scroll position so the view does not jump while it is being read.
        const auto scrollPosition = readoutViewport.getViewPositionY();

        readout.setLines (std::move (lines),
                          juce::jmax (320, readoutViewport.getMaximumVisibleWidth()));

        readoutViewport.setViewPosition (0, scrollPosition);
    }

    void DebugPanel::exportTroubleshootingFile()
    {
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
        const auto suggested = PresetManager::getPluginDataDirectory()
                                   .getChildFile ("troubleshooting_" + stamp + ".txt");

        fileChooser = std::make_unique<juce::FileChooser> ("Save troubleshooting file",
                                                            suggested, "*.txt");

        juce::Component::SafePointer<DebugPanel> safeThis (this);

        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto destination = chooser.getResult();

            if (destination == juce::File())
                return;

            const auto report = safeThis->processor.buildTroubleshootingReport()
                              + "\n\n===== RECENT LOG =====\n"
                              + Logging::readLogText (64 * 1024);

            if (destination.replaceWithText (report))
            {
                safeThis->processor.postToast ("Troubleshooting file written to "
                                                   + destination.getFullPathName(),
                                               SampleLoader::Severity::info);
                destination.revealToUser();
            }
            else
            {
                safeThis->processor.postToast ("Could not write the troubleshooting file.",
                                               SampleLoader::Severity::error);
            }
        });
    }

    void DebugPanel::confirmHardReset()
    {
        juce::Component::SafePointer<DebugPanel> safeThis (this);

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Reset all settings")
                .withMessage ("This clears your preferences, MIDI mappings, licence activation, "
                              "loaded samples and cached logs, and restores every control to its "
                              "default. It cannot be undone.\n\nContinue?")
                .withButton ("Reset everything")
                .withButton ("Cancel"),
            [safeThis] (int result)
            {
                if (safeThis == nullptr || result != 1)
                    return;

                safeThis->processor.hardResetAllSettings();
                safeThis->crashLogToggle.setToggleState (false, juce::dontSendNotification);
                safeThis->refreshNow();
            });
    }

    bool DebugPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onClose)
                onClose();

            return true;
        }

        return false;
    }

    void DebugPanel::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background);

        auto header = getLocalBounds().removeFromTop (30);

        g.setColour (colours::panel);
        g.fillRect (header);

        g.setColour (colours::panelEdge);
        g.fillRect (header.removeFromBottom (1));

        g.setColour (colours::textPrimary);
        g.setFont (TextureLookAndFeel::interfaceFont (13.0f, true));
        g.drawText ("DEBUG", header.reduced (layout::grid * 2, 0),
                    juce::Justification::centredLeft, false);
    }

    void DebugPanel::resized()
    {
        auto bounds = getLocalBounds();
        bounds.removeFromTop (30);
        bounds = bounds.reduced (layout::grid * 2, layout::grid);

        auto bottom = bounds.removeFromBottom (128);

        auto row = bottom.removeFromTop (28);
        crashLogToggle.setBounds (row.removeFromLeft (220));
        row.removeFromLeft (layout::grid);
        exportButton.setBounds (row.removeFromLeft (200));
        row.removeFromLeft (layout::grid);
        closeButton.setBounds (row.removeFromRight (70));

        bottom.removeFromTop (4);

        auto row2 = bottom.removeFromTop (26);
        copyLogButton.setBounds (row2.removeFromLeft (170));
        row2.removeFromLeft (layout::grid);
        revealLogButton.setBounds (row2.removeFromLeft (110));
        row2.removeFromLeft (layout::grid);
        resetPeakButton.setBounds (row2.removeFromLeft (120));
        hardResetButton.setBounds (row2.removeFromRight (220));

        bottom.removeFromTop (4);
        explanation.setBounds (bottom);

        bounds.removeFromBottom (layout::grid);
        readoutViewport.setBounds (bounds);
        readout.setSize (juce::jmax (320, readoutViewport.getMaximumVisibleWidth()), readout.getHeight());
    }

    // =========================================================================
    //  DebugWindow
    // =========================================================================

    DebugWindow::DebugWindow (TextureAudioProcessor& processor, juce::LookAndFeel& lookAndFeelToUse)
        : juce::DocumentWindow ("TEXTURE - Debug",
                                colours::background,
                                juce::DocumentWindow::closeButton)
    {
        setLookAndFeel (&lookAndFeelToUse);
        setUsingNativeTitleBar (true);
        setResizable (true, true);

        auto* panel = new DebugPanel (processor);
        panel->onClose = [this] { closeButtonPressed(); };
        panel->setSize (820, 640);

        setContentOwned (panel, true);
        centreWithSize (820, 640);
        setVisible (true);
    }

    DebugWindow::~DebugWindow()
    {
        clearContentComponent();
        setLookAndFeel (nullptr);
    }

    void DebugWindow::closeButtonPressed()
    {
        if (onClose)
            onClose();
    }
}
