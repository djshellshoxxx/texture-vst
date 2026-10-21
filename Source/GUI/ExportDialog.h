#pragma once

#include "../PluginProcessor.h"

namespace texture
{
    /** Export window for offline WAV rendering (F06).

        Holds only controls and status. The render belongs to the processor's
        ExportController, so closing this window hides progress without stopping or
        invalidating the job. Reopening shows the job's current state.
    */
    class ExportDialog final : public juce::Component,
                               private juce::Timer
    {
    public:
        explicit ExportDialog (TextureAudioProcessor& processor);
        ~ExportDialog() override;

        void paint (juce::Graphics& g) override;
        void resized() override;

    private:
        void timerCallback() override;

        ExportPlanner::Request readRequest() const;
        void updateDuration();
        void chooseDestinationAndStart();
        void startRender (ExportController::Request request);
        void requestCancel();
        void refreshButtons();
        void setStatus (const juce::String& text);

        TextureAudioProcessor& processor;

        juce::Label modeLabel, chordLabel, rateLabel, depthLabel, noteLabel, velocityLabel,
                    holdLabel, tailLabel, repeatsLabel, gapLabel;
        juce::ComboBox modeBox, chordBox, rateBox, depthBox;
        juce::Slider noteSlider, velocitySlider, holdSlider, tailSlider, repeatsSlider, gapSlider;

        juce::Label durationLabel, statusLabel;
        double progress { 0.0 };
        juce::ProgressBar progressBar { progress };

        juce::TextButton exportButton { "Export WAV..." };
        juce::TextButton cancelButton { "Cancel render" };
        juce::TextButton showButton { "Show file" };
        juce::TextButton closeButton { "Close" };

        std::unique_ptr<juce::FileChooser> chooser;
        std::optional<ExportController::Request> queuedRestart;
        juce::File lastFile;
    };
}
