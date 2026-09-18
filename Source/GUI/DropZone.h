#pragma once

#include "WaveformDisplay.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace texture
{
    class TextureAudioProcessor;

    /** The left-hand panel: a drop target that becomes a waveform once loaded.

        Accepts audio files and `.texture` presets by drag and drop, opens a file
        chooser when clicked while empty, and hosts the WaveformDisplay in the
        loaded state (05_SAMPLE_LOADER.md, 09_GUI_SPEC.md).
    */
    class DropZone : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     public juce::SettableTooltipClient
    {
    public:
        DropZone (TextureAudioProcessor& processor, int layerIndex);
        ~DropZone() override;

        /** Re-reads the processor's buffer for this layer. */
        void refreshFromProcessor();

        /** Advances the playhead. Called from the editor timer. */
        void setPlayheadPosition (float normalisedPosition);

        /** Tints the waveform to match the MORPH ring. */
        void setEnvelopeColour (juce::Colour colour);

        int getLayerIndex() const noexcept { return layerIndex; }

        void paint (juce::Graphics& g) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent& event) override;
        void mouseEnter (const juce::MouseEvent& event) override;
        void mouseExit (const juce::MouseEvent& event) override;

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray& files, int x, int y) override;
        void fileDragExit (const juce::StringArray& files) override;
        void filesDropped (const juce::StringArray& files, int x, int y) override;

        /** Opens the browse dialog. Also reachable from the File menu. */
        void openFileChooser();

    private:
        void showContextMenu();

        TextureAudioProcessor& processor;
        int layerIndex { 0 };

        std::unique_ptr<WaveformDisplay> waveform;
        std::unique_ptr<juce::FileChooser> fileChooser;

        juce::String sampleName;
        juce::String sampleDetails;
        bool isDragOver { false };
        bool isHovered { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DropZone)
    };
}
