#include "ExportDialog.h"

#include "../IO/ExportPlanner.h"
#include "../IO/RenderedAudioCache.h"
#include "../IO/WavExporter.h"

namespace texture
{
    namespace
    {
        constexpr int rowHeight = 28;
        constexpr int labelWidth = 120;
        constexpr int padding = 16;
        constexpr int pollIntervalMs = 100;   // at most 10 updates a second
        constexpr int tileHeight = 120;

        void configureSlider (juce::Slider& slider, double low, double high, double step, double value,
                              const juce::String& suffix)
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
            slider.setRange (low, high, step);
            slider.setValue (value, juce::dontSendNotification);
            slider.setTextValueSuffix (suffix);
        }

        int bytesPerSample (int depth) noexcept
        {
            return depth == 16 ? 2 : (depth == 32 ? 4 : 3);
        }
    }

    ExportDialog::ExportDialog (TextureAudioProcessor& audioProcessor)
        : processor (audioProcessor),
          tile (audioProcessor)
    {
        const std::array<juce::Label*, 10> labels {
            &modeLabel, &chordLabel, &rateLabel, &depthLabel, &noteLabel,
            &velocityLabel, &holdLabel, &tailLabel, &repeatsLabel, &gapLabel
        };
        const std::array<const char*, 10> names {
            "Mode", "Chord", "Sample rate", "Bit depth", "Note", "Velocity", "Hold (s)", "Tail (s)", "Repeats", "Gap (s)"
        };

        for (size_t i = 0; i < labels.size(); ++i)
        {
            labels[i]->setText (names[i], juce::dontSendNotification);
            labels[i]->setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (labels[i]);
        }

        modeBox.addItem ("Legacy two hits", 1);
        modeBox.addItem ("Single note", 2);
        modeBox.addItem ("Chord", 3);
        modeBox.addItem ("Repeated phrase", 4);
        modeBox.setSelectedId (1, juce::dontSendNotification);
        modeBox.setTooltip ("Legacy keeps the original two-hit layout. The others use the controls below.");
        modeBox.onChange = [this] { updateDuration(); };

        chordBox.addItem ("Single", 1);
        chordBox.addItem ("Major", 2);
        chordBox.addItem ("Minor", 3);
        chordBox.addItem ("Fifth", 4);
        chordBox.setSelectedId (2, juce::dontSendNotification);
        chordBox.onChange = [this] { updateDuration(); };

        for (const auto rate : WavExporter::getSupportedSampleRates())
            rateBox.addItem (juce::String (rate / 1000.0, 1) + " kHz", static_cast<int> (rateBox.getNumItems() + 1));
        rateBox.setSelectedId (2, juce::dontSendNotification);   // 48 kHz
        rateBox.onChange = [this] { updateDuration(); };

        for (const auto depth : WavExporter::getSupportedBitDepths())
            depthBox.addItem (juce::String (depth) + "-bit", static_cast<int> (depthBox.getNumItems() + 1));
        depthBox.setSelectedId (2, juce::dontSendNotification);  // 24-bit

        configureSlider (noteSlider, 0, 127, 1, 60, "");
        configureSlider (velocitySlider, 1, 127, 1, 96, "");
        configureSlider (holdSlider, 0.1, 30.0, 0.1, 2.0, " s");
        configureSlider (tailSlider, 0.0, 30.0, 0.1, 4.0, " s");
        configureSlider (repeatsSlider, 1, 16, 1, 1, "");
        configureSlider (gapSlider, 0.0, 5.0, 0.1, 0.5, " s");

        for (auto* slider : { &noteSlider, &velocitySlider, &holdSlider, &tailSlider, &repeatsSlider, &gapSlider })
        {
            slider->onValueChange = [this] { updateDuration(); };
            addAndMakeVisible (slider);
        }

        for (auto* box : { &modeBox, &chordBox, &rateBox, &depthBox })
            addAndMakeVisible (box);

        durationLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setJustificationType (juce::Justification::topLeft);
        statusLabel.setFont (juce::FontOptions (13.0f));
        folderLabel.setJustificationType (juce::Justification::centredLeft);
        folderLabel.setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (durationLabel);
        addAndMakeVisible (statusLabel);
        addAndMakeVisible (folderLabel);
        addAndMakeVisible (progressBar);
        addAndMakeVisible (tile);

        exportButton.onClick = [this] { chooseDestinationAndStart(); };
        dragButton.onClick = [this] { renderForDrag(); };
        cancelButton.onClick = [this] { requestCancel(); };
        showButton.onClick = [this] { lastFile.revealToUser(); };
        folderButton.onClick = [this] { chooseRenderFolder(); };
        clearButton.onClick = [this] { clearUnusedRenders(); };
        closeButton.onClick = [this]
        {
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->closeButtonPressed();
        };

        for (auto* button : { &exportButton, &dragButton, &cancelButton, &showButton, &folderButton, &clearButton, &closeButton })
            addAndMakeVisible (button);

        dragButton.setTooltip ("Render with these settings into the rendered-audio folder, then drag the tile into your DAW.");
        folderButton.setTooltip ("Choose where drag renders are kept. Existing renders stay where they are.");
        clearButton.setTooltip ("Delete rendered WAVs you are not dragging right now. Nothing else is touched.");

        setSize (540, 700);
        updateDuration();
        refreshFolderLabel();
        refreshButtons();

        seenExportSerial = processor.getExportSerial();

        if (processor.getExportController().getState() == ExportController::State::running)
            setStatus ("A render is running.");
        else if (seenExportSerial > 0)
            setStatus (processor.getLastExportResult().message);

        if (processor.getLastExportResult().succeeded)
            lastFile = processor.getLastExportResult().file;

        startTimer (pollIntervalMs);
    }

    ExportDialog::~ExportDialog()
    {
        stopTimer();
    }

    void ExportDialog::paint (juce::Graphics& g)
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    }

    void ExportDialog::resized()
    {
        auto area = getLocalBounds().reduced (padding);

        const auto row = [&area] (int height = rowHeight)
        {
            return area.removeFromTop (height);
        };

        const auto placeRow = [&] (juce::Label& label, juce::Component& control)
        {
            auto bounds = row();
            label.setBounds (bounds.removeFromLeft (labelWidth));
            control.setBounds (bounds.reduced (0, 2));
        };

        placeRow (modeLabel, modeBox);
        placeRow (chordLabel, chordBox);
        placeRow (rateLabel, rateBox);
        placeRow (depthLabel, depthBox);
        placeRow (noteLabel, noteSlider);
        placeRow (velocityLabel, velocitySlider);
        placeRow (holdLabel, holdSlider);
        placeRow (tailLabel, tailSlider);
        placeRow (repeatsLabel, repeatsSlider);
        placeRow (gapLabel, gapSlider);

        area.removeFromTop (6);
        durationLabel.setBounds (row());
        progressBar.setBounds (row (18));
        area.removeFromTop (4);
        statusLabel.setBounds (area.removeFromTop (72));

        auto actions = row();
        exportButton.setBounds (actions.removeFromLeft (150));
        actions.removeFromLeft (8);
        dragButton.setBounds (actions.removeFromLeft (150));
        actions.removeFromLeft (8);
        cancelButton.setBounds (actions.removeFromLeft (120));

        area.removeFromTop (6);
        tile.setBounds (area.removeFromTop (tileHeight));
        area.removeFromTop (6);

        folderLabel.setBounds (row (20));

        auto footer = row();
        showButton.setBounds (footer.removeFromLeft (100));
        footer.removeFromLeft (8);
        folderButton.setBounds (footer.removeFromLeft (100));
        footer.removeFromLeft (8);
        clearButton.setBounds (footer.removeFromLeft (140));
        closeButton.setBounds (footer.removeFromRight (100));
    }

    ExportPlanner::Request ExportDialog::readRequest() const
    {
        ExportPlanner::Request request;
        request.mode = static_cast<ExportPlanner::Mode> (juce::jmax (0, modeBox.getSelectedId() - 1));
        request.chordShape = juce::jmax (0, chordBox.getSelectedId() - 1);

        const auto rates = WavExporter::getSupportedSampleRates();
        request.sampleRate = rates[juce::jlimit (0, rates.size() - 1, rateBox.getSelectedId() - 1)];
        request.note = juce::roundToInt (noteSlider.getValue());
        request.velocity = juce::roundToInt (velocitySlider.getValue());
        request.holdSeconds = holdSlider.getValue();
        request.tailSeconds = tailSlider.getValue();
        request.repeats = juce::roundToInt (repeatsSlider.getValue());
        request.gapSeconds = gapSlider.getValue();

        if (request.mode == ExportPlanner::Mode::singleNote)
            request.chordShape = 0;

        return request;
    }

    double ExportDialog::releaseSeconds() const
    {
        return static_cast<double> (processor.captureRenderSettings().releaseMs) * 0.001;
    }

    int ExportDialog::bitDepthChoice() const
    {
        const auto depths = WavExporter::getSupportedBitDepths();
        return depths[juce::jlimit (0, depths.size() - 1, depthBox.getSelectedId() - 1)];
    }

    void ExportDialog::updateDuration()
    {
        const auto plan = ExportPlanner::plan (readRequest(), releaseSeconds());
        planValid = plan.valid;

        if (plan.valid)
            durationLabel.setText ("Length: " + juce::String (plan.durationSeconds, 2) + " s  (limit "
                                       + juce::String (ExportPlanner::maxDurationSeconds, 0) + " s)",
                                   juce::dontSendNotification);
        else
            durationLabel.setText (plan.message, juce::dontSendNotification);

        refreshButtons();
    }

    void ExportDialog::chooseDestinationAndStart()
    {
        const auto request = readRequest();
        const auto plan = ExportPlanner::plan (request, releaseSeconds());

        if (! plan.valid)
        {
            setStatus (plan.message);
            return;
        }

        if (processor.getSampleForLayer (0) == nullptr && processor.getSampleForLayer (1) == nullptr)
        {
            setStatus ("Load a sample before exporting.");
            return;
        }

        const auto suggested = juce::File::createLegalFileName (processor.getCurrentPresetName()) + ".wav";

        juce::Component::SafePointer<ExportDialog> safeThis (this);

        chooser = std::make_unique<juce::FileChooser> (
            "Export WAV",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (suggested),
            "*.wav");

        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, safeThis, request] (const juce::FileChooser& picked)
        {
            if (safeThis == nullptr)
                return;

            auto destination = picked.getResult();

            if (destination == juce::File())
                return;

            if (! destination.hasFileExtension ("wav"))
                destination = destination.withFileExtension ("wav");

            // The snapshot and plan are taken now, so later edits cannot change this render.
            const auto snapshot = processor.captureExportSnapshot();
            ExportController::Request job;
            job.snapshot = snapshot;
            job.plan = ExportPlanner::plan (request, snapshot->settings.releaseMs * 0.001);
            job.sampleRate = request.sampleRate;
            job.bitDepth = bitDepthChoice();
            job.destination = destination;

            safeThis->startRender (std::move (job), false);
        });
    }

    void ExportDialog::renderForDrag()
    {
        const auto request = readRequest();
        const auto plan = ExportPlanner::plan (request, releaseSeconds());

        if (! plan.valid)
        {
            setStatus (plan.message);
            return;
        }

        if (processor.getSampleForLayer (0) == nullptr && processor.getSampleForLayer (1) == nullptr)
        {
            setStatus ("Load a sample before exporting.");
            return;
        }

        const auto depth = bitDepthChoice();
        const auto estimate = plan.totalSamples * 2 * bytesPerSample (depth) + 64 * 1024;
        auto& cache = processor.getRenderedAudioCache();
        const auto reservation = cache.reserve (processor.getCurrentPresetName(), estimate);

        if (! reservation.succeeded)
        {
            setStatus (reservation.message);
            return;
        }

        // The worker finalises the name and content ID; the message thread only displays it.
        const auto snapshot = processor.captureExportSnapshot();
        ExportController::Request job;
        job.snapshot = snapshot;
        job.plan = ExportPlanner::plan (request, snapshot->settings.releaseMs * 0.001);
        job.sampleRate = request.sampleRate;
        job.bitDepth = depth;
        job.destination = reservation.file;
        job.afterSuccess = [renderCache = &cache] (WavExporter::Result& result)
        {
            const auto finalised = renderCache->finalise (result.file);

            if (! finalised.succeeded)
            {
                result.succeeded = false;
                result.message = finalised.message;
                return;
            }

            result.file = finalised.file;
            result.fileSizeBytes = finalised.file.getSize();
        };

        startRender (std::move (job), true);
    }

    void ExportDialog::chooseRenderFolder()
    {
        if (processor.getExportController().isBusy())
        {
            setStatus ("Wait for the current render to finish before changing the folder.");
            return;
        }

        juce::Component::SafePointer<ExportDialog> safeThis (this);

        chooser = std::make_unique<juce::FileChooser> (
            "Choose the rendered audio folder",
            processor.getRenderedAudioDirectory());

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis] (const juce::FileChooser& picked)
        {
            if (safeThis == nullptr || picked.getResult() == juce::File())
                return;

            if (safeThis->processor.setRenderedAudioDirectory (picked.getResult()))
                safeThis->setStatus ("Renders will be kept in " + picked.getResult().getFullPathName() + ".");
            else
                safeThis->setStatus ("That folder could not be used.");

            safeThis->refreshFolderLabel();
        });
    }

    void ExportDialog::clearUnusedRenders()
    {
        if (processor.getExportController().isBusy())
        {
            setStatus ("Wait for the render to finish before clearing.");
            return;
        }

        juce::Component::SafePointer<ExportDialog> safeThis (this);

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Clear rendered audio")
                .withMessage ("Delete the rendered WAVs in " + processor.getRenderedAudioCache().getDirectory().getFullPathName()
                              + " that are not being dragged right now? DAW projects that reference them will lose their audio.")
                .withButton ("Delete")
                .withButton ("Keep"),
            [safeThis] (int choice)
            {
                // AlertWindow maps the first of two buttons to 1.
                if (safeThis == nullptr || choice != 1)
                    return;

                const auto result = safeThis->processor.getRenderedAudioCache().clearUnleased();
                safeThis->setStatus ("Removed " + juce::String (result.removed) + " render(s), freeing "
                                     + juce::File::descriptionOfSizeInBytes (result.freedBytes) + "."
                                     + (result.skippedLeased > 0
                                            ? " " + juce::String (result.skippedLeased) + " in use were kept."
                                            : juce::String()));
                safeThis->tile.repaint();
                safeThis->refreshFolderLabel();
            });
    }

    void ExportDialog::startRender (ExportController::Request request, bool forDrag)
    {
        if (processor.getExportController().isBusy())
        {
            juce::Component::SafePointer<ExportDialog> safeThis (this);

            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::QuestionIcon)
                    .withTitle ("Render already running")
                    .withMessage ("A render is already running. Cancel it and start this one?")
                    .withButton ("Restart")
                    .withButton ("Keep current"),
                [safeThis, request = std::move (request), forDrag] (int choice) mutable
                {
                    // AlertWindow maps the first of two buttons to 1.
                    if (safeThis == nullptr || choice != 1)
                        return;

                    safeThis->queuedRestart = std::move (request);
                    safeThis->queuedRestartForDrag = forDrag;
                    safeThis->requestCancel();
                });
            return;
        }

        if (! processor.startExportJob (std::move (request), forDrag))
        {
            setStatus ("A finished render is waiting to be collected. Try again in a moment.");
            return;
        }

        progress = 0.0;
        setStatus (forDrag ? "Rendering for drag..." : "Rendering...");
        refreshButtons();
    }

    void ExportDialog::requestCancel()
    {
        processor.getExportController().cancel();
        queuedRestart.reset();
        refreshButtons();
    }

    void ExportDialog::timerCallback()
    {
        auto& controller = processor.getExportController();

        if (controller.getState() == ExportController::State::running)
        {
            progress = controller.getProgress();
            progressBar.repaint();
            refreshButtons();
            return;
        }

        // The processor collects every finished render, with or without this window open.
        // This window only shows the latest one, once.
        if (processor.getExportSerial() != seenExportSerial)
        {
            seenExportSerial = processor.getExportSerial();
            const auto& result = processor.getLastExportResult();

            if (result.succeeded)
            {
                lastFile = result.file;
                progress = 1.0;
            }

            setStatus (result.cancelled ? juce::String ("Render cancelled. Nothing was written.") : result.message);
            tile.repaint();
            refreshFolderLabel();
        }

        if (queuedRestart.has_value() && ! controller.isBusy())
        {
            auto next = std::move (*queuedRestart);
            const auto forDrag = queuedRestartForDrag;
            queuedRestart.reset();
            queuedRestartForDrag = false;
            startRender (std::move (next), forDrag);
            return;
        }

        refreshButtons();
    }

    void ExportDialog::refreshButtons()
    {
        const auto running = processor.getExportController().getState() == ExportController::State::running;
        const auto idle = ! processor.getExportController().isBusy();
        const auto ready = idle && planValid;

        exportButton.setEnabled (ready);
        dragButton.setEnabled (ready);
        folderButton.setEnabled (idle);
        clearButton.setEnabled (idle);
        cancelButton.setEnabled (running);
        showButton.setEnabled (lastFile != juce::File() && lastFile.existsAsFile() && idle);
        progressBar.setVisible (running || progress > 0.0);
    }

    void ExportDialog::refreshFolderLabel()
    {
        const auto folder = processor.getRenderedAudioDirectory();
        const auto& cache = processor.getRenderedAudioCache();
        folderLabel.setText ("Renders: " + folder.getFullPathName() + "  ("
                                 + juce::File::descriptionOfSizeInBytes (cache.totalBytes()) + " of "
                                 + juce::File::descriptionOfSizeInBytes (cache.getCapBytes()) + ")",
                             juce::dontSendNotification);
    }

    void ExportDialog::setStatus (const juce::String& text)
    {
        statusLabel.setText (text, juce::dontSendNotification);
    }
}
