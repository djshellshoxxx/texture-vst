#include "ExportDialog.h"

#include "../IO/ExportPlanner.h"
#include "../IO/WavExporter.h"

namespace texture
{
    namespace
    {
        constexpr int rowHeight = 28;
        constexpr int labelWidth = 120;
        constexpr int padding = 16;
        constexpr int pollIntervalMs = 100;   // at most 10 updates a second

        void configureSlider (juce::Slider& slider, double low, double high, double step, double value,
                              const juce::String& suffix)
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
            slider.setRange (low, high, step);
            slider.setValue (value, juce::dontSendNotification);
            slider.setTextValueSuffix (suffix);
        }
    }

    ExportDialog::ExportDialog (TextureAudioProcessor& audioProcessor)
        : processor (audioProcessor)
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
        addAndMakeVisible (durationLabel);
        addAndMakeVisible (statusLabel);
        addAndMakeVisible (progressBar);

        exportButton.onClick = [this] { chooseDestinationAndStart(); };
        cancelButton.onClick = [this] { requestCancel(); };
        showButton.onClick = [this] { lastFile.revealToUser(); };
        closeButton.onClick = [this]
        {
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->closeButtonPressed();
        };

        for (auto* button : { &exportButton, &cancelButton, &showButton, &closeButton })
            addAndMakeVisible (button);

        setSize (520, 430);
        updateDuration();
        refreshButtons();

        // Show what a running or finished render already holds, if any.
        const auto state = processor.getExportController().getState();

        if (state == ExportController::State::running)
            setStatus ("A render is running.");

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

        area.removeFromTop (8);
        durationLabel.setBounds (row());
        progressBar.setBounds (row (18));
        area.removeFromTop (6);
        statusLabel.setBounds (area.removeFromTop (100));

        auto buttons = area.removeFromBottom (rowHeight);
        exportButton.setBounds (buttons.removeFromLeft (130));
        buttons.removeFromLeft (8);
        cancelButton.setBounds (buttons.removeFromLeft (130));
        buttons.removeFromLeft (8);
        showButton.setBounds (buttons.removeFromLeft (100));
        closeButton.setBounds (buttons.removeFromRight (100));
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

    void ExportDialog::updateDuration()
    {
        const auto plan = ExportPlanner::plan (readRequest(),
                                               static_cast<double> (processor.captureRenderSettings().releaseMs) * 0.001);

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
        const auto releaseSeconds = static_cast<double> (processor.captureRenderSettings().releaseMs) * 0.001;
        const auto plan = ExportPlanner::plan (request, releaseSeconds);

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

        chooser = std::make_unique<juce::FileChooser> (
            "Export WAV",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (suggested),
            "*.wav");

        juce::Component::SafePointer<ExportDialog> safeThis (this);

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
            const auto depths = WavExporter::getSupportedBitDepths();
            job.bitDepth = depths[juce::jlimit (0, depths.size() - 1, depthBox.getSelectedId() - 1)];
            job.destination = destination;

            safeThis->startRender (std::move (job));
        });
    }

    void ExportDialog::startRender (ExportController::Request request)
    {
        auto& controller = processor.getExportController();

        if (controller.isBusy())
        {
            // The first render owns the controller. Offer to replace it with this one.
            juce::Component::SafePointer<ExportDialog> safeThis (this);

            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::QuestionIcon)
                    .withTitle ("Render already running")
                    .withMessage ("A render is already running. Cancel it and start this one?")
                    .withButton ("Restart")
                    .withButton ("Keep current"),
                [safeThis, request = std::move (request)] (int choice) mutable
                {
                    // AlertWindow maps the first of two buttons to 1.
                    if (safeThis == nullptr || choice != 1)
                        return;

                    safeThis->queuedRestart = std::move (request);
                    safeThis->requestCancel();
                });
            return;
        }

        if (! controller.start (std::move (request)))
        {
            setStatus ("A finished render is waiting to be collected. Try again in a moment.");
            return;
        }

        progress = 0.0;
        setStatus ("Rendering...");
        refreshButtons();
    }

    void ExportDialog::requestCancel()
    {
        processor.getExportController().cancel();
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

        if (controller.getState() == ExportController::State::finished)
        {
            WavExporter::Result result;

            if (controller.takeResult (result))
            {
                if (result.succeeded)
                {
                    lastFile = result.file;
                    progress = 1.0;
                    auto text = result.message;

                    if (result.clippedSamples > 0)
                        text << "\n" << juce::String (result.clippedSamples)
                             << " samples reached full scale. Lower the gain to avoid clipping; "
                                "nothing was limited.";

                    setStatus (text);
                }
                else
                {
                    setStatus (result.cancelled ? "Render cancelled. Nothing was written." : result.message);
                }

                processor.postToast (result.message, result.succeeded ? SampleLoader::Severity::info
                                                                      : SampleLoader::Severity::warning);
            }
        }

        if (queuedRestart.has_value() && ! controller.isBusy())
        {
            auto next = std::move (*queuedRestart);
            queuedRestart.reset();
            startRender (std::move (next));
            return;
        }

        refreshButtons();
    }

    void ExportDialog::refreshButtons()
    {
        const auto running = processor.getExportController().getState() == ExportController::State::running;
        exportButton.setEnabled (! running);
        cancelButton.setEnabled (running);
        showButton.setEnabled (lastFile != juce::File() && lastFile.existsAsFile() && ! running);
        progressBar.setVisible (running || progress > 0.0);
    }

    void ExportDialog::setStatus (const juce::String& text)
    {
        statusLabel.setText (text, juce::dontSendNotification);
    }
}
