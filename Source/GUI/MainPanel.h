#pragma once

#include "../PluginProcessor.h"
#include "../Presets/PresetBrowser.h"
#include "DebugWindow.h"
#include "DropZone.h"
#include "HelpPanel.h"
#include "MorphKnob.h"
#include "SecretPanel.h"
#include "SettingsPanel.h"
#include "Widgets.h"

#include <memory>

namespace texture
{
    class TextureAudioProcessor;

    /** Everything inside the plugin window.

        Laid out once at the design size of 720 x 480 and scaled by the editor, so
        the 3:2 aspect ratio guarantees the layout never distorts.
    */
    class MainPanel : public juce::Component,
                      public TextureAudioProcessor::Listener,
                      private juce::Timer
    {
    public:
        explicit MainPanel (TextureAudioProcessor& processor);
        ~MainPanel() override;

        /** The size everything is laid out against before the editor scales it. */
        static constexpr int designWidth = layout::defaultWidth;
        static constexpr int designHeight = layout::defaultHeight;

        /** Raised when the standalone wrapper should show its device dialog. */
        std::function<void()> onAudioSettingsRequested;

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& event) override;
        bool keyPressed (const juce::KeyPress& key) override;

        // --- TextureAudioProcessor::Listener ---------------------------------
        void sampleChanged (int layerIndex) override;
        void toastRequested (const juce::String& message, SampleLoader::Severity severity) override;
        void pluginStateChanged() override;

        /** Turns the hover tooltips on or off (Settings > Preferences). */
        void setTooltipsEnabled (bool shouldBeEnabled);

    private:
        void timerCallback() override;

        void buildHeader();
        void buildControls();
        void buildOverlays();
        void applyFocusOrder();

        void showFileMenu();
        void showPresetMenu();
        void openPresetBrowser();
        void closeOverlays();
        void openHelp();
        void openDebugWindow();
        void saveCurrentPreset (bool forceSaveAs);
        void openPresetFile();
        void exportAudio();
        void showProUpsell (const juce::String& featureName);
        void updateProGating();
        void updateDataStream();
        void revealSecret();

        TextureAudioProcessor& processor;

        // Header
        LevelLed levelLed;
        TierBadge tierBadge;
        IconButton gearButton { IconButton::Icon::gear, "Settings: licence, folders, preferences and about" };
        IconButton helpButton { IconButton::Icon::help, "Help: the full manual, troubleshooting and debug tools" };
        IconButton randomiseButton { IconButton::Icon::dice, "Randomize every control. Each press starts from defaults first." };
        IconButton resetButton { IconButton::Icon::reset, "Reset every control to its default value" };
        IconButton fileMenuButton { IconButton::Icon::folder, "File: open, save, export audio and options" };
        juce::TextButton presetNameButton;
        juce::TextButton compareAButton { "A" };
        juce::TextButton compareBButton { "B" };

        // Controls
        std::unique_ptr<DropZone> dropZoneA;
        std::unique_ptr<DropZone> dropZoneB;
        std::unique_ptr<MorphKnob> morphKnob;
        std::unique_ptr<ParameterKnob> attackKnob;
        std::unique_ptr<ParameterKnob> decayKnob;
        std::unique_ptr<ParameterKnob> sustainKnob;
        std::unique_ptr<ParameterKnob> releaseKnob;
        std::unique_ptr<ParameterKnob> gainKnob;
        std::unique_ptr<ParameterToggle> reverbToggle;
        std::unique_ptr<ParameterKnob> lfoRateKnob;
        std::unique_ptr<ParameterKnob> lfoDepthKnob;
        std::unique_ptr<ParameterToggle> lfoSyncToggle;
        std::unique_ptr<ParameterKnob> layerBlendKnob;
        juce::ComboBox lfoDivisionBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoDivisionAttachment;

        SectionHeader envelopeHeader { "Envelope" };
        SectionHeader outputHeader { "Output" };
        SectionHeader lfoHeader { "Morph LFO", colours::accentPro };
        SectionHeader layerHeader { "Layer 2", colours::accentPro };
        juce::TextButton lfoProBadge { "PRO - click to upgrade" };
        juce::TextButton layerProBadge { "PRO" };

        DataStreamView dataStream;
        ToastBar toast;

        // Overlays
        std::unique_ptr<SettingsPanel> settingsPanel;
        std::unique_ptr<PresetBrowser> presetBrowser;
        std::unique_ptr<HelpPanel> helpPanel;
        std::unique_ptr<SecretPanel> secretPanel;
        std::unique_ptr<DebugWindow> debugWindow;
        std::unique_ptr<juce::FileChooser> fileChooser;

        /** The pixel that reveals the hidden effect (include.md, "easter egg"). */
        juce::Rectangle<int> secretHotspot;

        int streamCounter { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
    };
}
