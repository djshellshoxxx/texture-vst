#pragma once

#include "GUI/LookAndFeel.h"
#include "GUI/MainPanel.h"

#include <memory>

namespace texture
{
    class TextureAudioProcessor;

    /** The plugin window.

        Owns the look and feel and the tooltip window, keeps the 3:2 aspect ratio,
        and scales MainPanel rather than re-laying it out, so the design is pixel
        identical at every size (09_GUI_SPEC.md).
    */
    class TextureAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
    {
    public:
        explicit TextureAudioProcessorEditor (TextureAudioProcessor& processor);
        ~TextureAudioProcessorEditor() override;

        void paint (juce::Graphics& g) override;
        void resized() override;

    private:
        void timerCallback() override;
        void saveWindowSize();
        void showAudioSettings();

        TextureAudioProcessor& processorRef;
        TextureLookAndFeel lookAndFeel;

        std::unique_ptr<juce::TooltipWindow> tooltipWindow;
        std::unique_ptr<MainPanel> mainPanel;

        juce::ComponentBoundsConstrainer constrainer;

        /** Window size is persisted globally, not per project, with a debounce. */
        bool sizeChangePending { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TextureAudioProcessorEditor)
    };
}
