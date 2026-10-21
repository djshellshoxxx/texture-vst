#pragma once

#include "Widgets.h"

#include <memory>

namespace texture
{
    class TextureAudioProcessor;

    /** The overlay that slides in from the right (09_GUI_SPEC.md).

        Four tabs: the licence, preferences, play (voicing and tone) and an about page. The hidden debug
        view is not a tab here - it is a separate window opened from Help or by
        shift-clicking the gear (19_DEBUGGING.md).
    */
    class SettingsPanel : public juce::Component,
                          private juce::Timer
    {
    public:
        enum class Tab { license, preferences, play, about };

        explicit SettingsPanel (TextureAudioProcessor& processor);
        ~SettingsPanel() override;

        /** Slides the panel in on the given tab. */
        void open (Tab tab);

        /** Slides the panel out. */
        void close();

        bool isOpen() const noexcept { return targetOffset <= 0.0f; }

        /** Raised once the panel has finished sliding out. */
        std::function<void()> onClosed;

        /** Raised when the user asks for the audio/MIDI device setup (standalone). */
        std::function<void()> onAudioSettingsRequested;

        /** Raised when the tooltip preference changes. */
        std::function<void (bool)> onTooltipsChanged;

        /** Raised whenever the licence tier changes so the header can update. */
        std::function<void()> onTierChanged;

        /** Positions the panel inside its parent for the current slide offset.
            The owner calls this from its own resized(); the panel must never set
            its own bounds from resized(). */
        void updateSlideBounds();

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& event) override;
        bool keyPressed (const juce::KeyPress& key) override;

    private:
        void timerCallback() override;
        void setTab (Tab tab);
        void layoutCurrentTab();
        void refreshLicenseState();
        void attemptActivation();
        void attemptRestore();
        void chooseFolder (bool isSampleFolder);

        TextureAudioProcessor& processor;

        // Tab bar
        juce::TextButton licenseTabButton { "Licence" };
        juce::TextButton preferencesTabButton { "Preferences" };
        juce::TextButton playTabButton { "Play" };
        juce::TextButton aboutTabButton { "About" };
        juce::TextButton closeButton { "Close" };
        Tab currentTab { Tab::license };

        // Licence tab
        juce::Label licenseBlurb;
        juce::Label emailLabel { {}, "Email" };
        juce::TextEditor emailField;
        juce::Label keyLabel { {}, "Licence key" };
        juce::TextEditor keyField;
        juce::TextButton activateButton { "Activate" };
        juce::TextButton restoreButton { "Restore" };
        juce::TextButton deactivateButton { "Deactivate" };
        juce::TextButton buyButton { "Buy Pro" };
        juce::Label licenseStatus;

        // Preferences tab
        juce::ToggleButton tooltipsToggle { "Show tooltips" };
        juce::ToggleButton reducedMotionToggle { "Reduce interface motion" };
        juce::Label presetFolderLabel;
        juce::TextButton presetFolderButton { "Choose preset folder..." };
        juce::TextButton revealPresetFolderButton { "Open in file browser" };
        juce::Label sampleFolderLabel;
        juce::TextButton sampleFolderButton { "Choose sample folder..." };
        juce::TextButton revealSampleFolderButton { "Open in file browser" };
        juce::Label defaultGainLabel { {}, "Default output gain" };
        juce::Slider defaultGainSlider;
        juce::TextButton audioSettingsButton { "Audio / MIDI setup..." };
        juce::TextButton clearMappingsButton { "Clear all MIDI mappings" };
        juce::Label randomModeLabel { {}, "Random variation" };
        std::unique_ptr<ParameterChoiceBox> randomModeBox;
        juce::Label randomSeedLabel { {}, "Seed" };
        juce::TextEditor randomSeedField;
        juce::TextButton copySeedButton { "Copy" };
        juce::TextButton newSeedButton { "New seed" };
        juce::TextButton resetEvolutionButton { "Reset evolution" };
        juce::Label randomSeedStatus;

        void refreshRandomSeedField();
        void commitRandomSeedField();

        // Play tab: how notes are voiced and what the output sounds like
        juce::Label voiceModeLabel { {}, "Voice mode" };
        std::unique_ptr<ParameterChoiceBox> voiceModeBox;
        juce::Label notePriorityLabel { {}, "Note priority" };
        std::unique_ptr<ParameterChoiceBox> notePriorityBox;
        juce::Label glideLabel { {}, "Glide" };
        std::unique_ptr<ParameterSlider> glideSlider;
        std::unique_ptr<ParameterToggle> toneFilterToggle;
        juce::Label highPassLabel { {}, "High-pass" };
        std::unique_ptr<ParameterSlider> highPassSlider;
        juce::Label lowPassLabel { {}, "Low-pass" };
        std::unique_ptr<ParameterSlider> lowPassSlider;
        juce::Label stereoWidthLabel { {}, "Stereo width" };
        std::unique_ptr<ParameterSlider> stereoWidthSlider;

        // About tab
        juce::Label aboutText;
        juce::TextButton aboutHomepageButton { "Circuit Drift Labs" };
        juce::TextButton aboutGithubButton { "Source" };
        juce::TextButton aboutSupportButton { "Contact" };
        juce::TextButton aboutCloudButton { "Community presets" };

        std::unique_ptr<juce::FileChooser> fileChooser;

        float currentOffset { 1.0f };   // 1 = fully off-screen right, 0 = open
        float targetOffset { 1.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
    };
}
