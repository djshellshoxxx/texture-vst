#include "PluginEditor.h"

#include "PluginProcessor.h"

// juce_StandaloneFilterWindow.h assumes the audio/MIDI device classes are already
// visible; it includes only the plugin-filter factory itself.
#include <juce_audio_utils/juce_audio_utils.h>

#if __has_include(<juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>)
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
 #define TEXTURE_HAS_STANDALONE_WINDOW 1
#else
 #define TEXTURE_HAS_STANDALONE_WINDOW 0
#endif

namespace texture
{
    namespace
    {
        constexpr const char* windowWidthKey = "window_width";
        constexpr const char* windowHeightKey = "window_height";
        constexpr const char* tooltipsKey = "show_tooltips";

        /** Window size is written this long after the last resize (13_STATE_SAVING.md). */
        constexpr int sizeSaveDebounceMs = 500;
    }

    TextureAudioProcessorEditor::TextureAudioProcessorEditor (TextureAudioProcessor& owner)
        : juce::AudioProcessorEditor (&owner),
          processorRef (owner)
    {
        setLookAndFeel (&lookAndFeel);
        setOpaque (true);

        mainPanel = std::make_unique<MainPanel> (processorRef);
        mainPanel->onAudioSettingsRequested = [this] { showAudioSettings(); };

        mainPanel->onTooltipsChanged = [this] (bool enabled)
        {
            if (! enabled)
                tooltipWindow.reset();
            else if (tooltipWindow == nullptr)
                tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 400);
        };

        mainPanel->onScaleRequested = [this] (float scale)
        {
            const auto width = juce::jlimit (layout::minimumWidth, layout::maximumWidth,
                                             juce::roundToInt (static_cast<float> (layout::defaultWidth) * scale));
            setSize (width, juce::roundToInt (width * static_cast<double> (layout::defaultHeight)
                                                    / static_cast<double> (layout::defaultWidth)));
        };
        addAndMakeVisible (*mainPanel);

        // Tooltips are a preference, so only create the window when they are wanted.
        auto tooltipsEnabled = true;

        if (auto* store = processorRef.getApplicationProperties().getUserSettings())
            tooltipsEnabled = store->getBoolValue (tooltipsKey, true);

        if (tooltipsEnabled)
            tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 400);

        // 3:2 for ever: the layout is scaled, never reflowed.
        constrainer.setSizeLimits (layout::minimumWidth, layout::minimumHeight,
                                   layout::maximumWidth, layout::maximumHeight);
        constrainer.setFixedAspectRatio (static_cast<double> (layout::defaultWidth)
                                             / static_cast<double> (layout::defaultHeight));
        setConstrainer (&constrainer);
        setResizable (true, true);

        auto width = layout::defaultWidth;
        auto height = layout::defaultHeight;

        if (auto* store = processorRef.getApplicationProperties().getUserSettings())
        {
            width = store->getIntValue (windowWidthKey, layout::defaultWidth);
            height = store->getIntValue (windowHeightKey, layout::defaultHeight);
        }

        width = juce::jlimit (layout::minimumWidth, layout::maximumWidth, width);
        height = juce::roundToInt (width * static_cast<double> (layout::defaultHeight)
                                         / static_cast<double> (layout::defaultWidth));

        setSize (width, height);
    }

    TextureAudioProcessorEditor::~TextureAudioProcessorEditor()
    {
        stopTimer();

        if (sizeChangePending)
            saveWindowSize();

        setConstrainer (nullptr);
        tooltipWindow.reset();
        mainPanel.reset();
        setLookAndFeel (nullptr);
    }

    void TextureAudioProcessorEditor::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background);
    }

    void TextureAudioProcessorEditor::resized()
    {
        if (mainPanel == nullptr)
            return;

        const auto scale = static_cast<float> (getWidth())
                         / static_cast<float> (MainPanel::designWidth);

        mainPanel->currentScale = scale;
        mainPanel->setTransform (juce::AffineTransform::scale (scale));
        mainPanel->setBounds (0, 0, MainPanel::designWidth, MainPanel::designHeight);

        sizeChangePending = true;
        startTimer (sizeSaveDebounceMs);
    }

    void TextureAudioProcessorEditor::timerCallback()
    {
        stopTimer();
        saveWindowSize();
    }

    void TextureAudioProcessorEditor::saveWindowSize()
    {
        sizeChangePending = false;

        if (auto* store = processorRef.getApplicationProperties().getUserSettings())
        {
            store->setValue (windowWidthKey, getWidth());
            store->setValue (windowHeightKey, getHeight());
            store->saveIfNeeded();
        }
    }

    void TextureAudioProcessorEditor::showAudioSettings()
    {
       #if TEXTURE_HAS_STANDALONE_WINDOW
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            holder->showAudioSettingsDialog();
            return;
        }
       #endif

        processorRef.postToast ("Your host owns the audio and MIDI devices. "
                                "Use its own preferences to change them.",
                                SampleLoader::Severity::info);
    }
}
