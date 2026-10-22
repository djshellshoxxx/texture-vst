#pragma once

#include "RenderedAudioTile.h"
#include "../PluginProcessor.h"

namespace texture
{
    /** Export window for offline WAV rendering (F06) and the rendered-audio drag tile (F15).

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
        double releaseSeconds() const;
        int bitDepthChoice() const;
        void updateDuration();
        void chooseDestinationAndStart();
        void renderForDrag();
        void chooseRenderFolder();
        void clearUnusedRenders();
        void startRender (ExportController::Request request, bool forDrag);
        void requestCancel();
        void refreshButtons();
        void refreshFolderLabel();
        void setStatus (const juce::String& text);

        TextureAudioProcessor& processor;

        juce::Label modeLabel, chordLabel, rateLabel, depthLabel, noteLabel, velocityLabel,
                    holdLabel, tailLabel, repeatsLabel, gapLabel;
        juce::ComboBox modeBox, chordBox, rateBox, depthBox;
        juce::Slider noteSlider, velocitySlider, holdSlider, tailSlider, repeatsSlider, gapSlider;

        juce::Label durationLabel, statusLabel, folderLabel;
        double progress { 0.0 };
        juce::ProgressBar progressBar { progress };

        juce::TextButton exportButton { "Export WAV..." };
        juce::TextButton dragButton { "Render for drag" };
        juce::TextButton cancelButton { "Cancel render" };
        juce::TextButton showButton { "Show file" };
        juce::TextButton folderButton { "Folder..." };
        juce::TextButton clearButton { "Clear renders..." };
        juce::TextButton closeButton { "Close" };

        RenderedAudioTile tile;

        std::unique_ptr<juce::FileChooser> chooser;
        std::optional<ExportController::Request> queuedRestart;
        bool queuedRestartForDrag { false };
        bool planValid { false };
        int seenExportSerial { 0 };
        juce::File lastFile;
    };
}
