#pragma once

#include "../PluginProcessor.h"
#include "../Presets/PresetBrowser.h"
#include "DebugWindow.h"
#include "DropZone.h"
#include "EditToolsPanel.h"
#include "HelpPanel.h"
#include "MorphKnob.h"
#include "PortableMediaDialogs.h"
#include "SecretPanel.h"
#include "SettingsPanel.h"
#include "SoundControlsPanel.h"
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

        /** Raised when Preferences > Show tooltips changes. The editor owns the tooltip window. */
        std::function<void (bool)> onTooltipsChanged;

        /** Raised by File > Interface size with a scale relative to 720 x 480. */
        std::function<void (float)> onScaleRequested;

        /** The current scale, so the menu can tick it. Set by the editor. */
        float currentScale { 1.0f };

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& event) override;
        bool keyPressed (const juce::KeyPress& key) override;

        // --- TextureAudioProcessor::Listener ---------------------------------
        void sampleChanged (int layerIndex) override;
        void toastRequested (const juce::String& message, SampleLoader::Severity severity) override;
        void pluginStateChanged() override;
        void sampleMissing (int layerIndex, const juce::String& expectedSha256, const juce::String& fileName) override;

        /** Turns the hover tooltips on or off (Settings > Preferences). */
        void setTooltipsEnabled (bool shouldBeEnabled);

    private:
        class MomentaryAuditionButton final : public juce::TextButton
        {
        public:
            MomentaryAuditionButton() : juce::TextButton ("AUDITION") {}
            std::function<void (bool)> onPressedStateChanged;

        private:
            void buttonStateChanged() override
            {
                juce::TextButton::buttonStateChanged();
                if (onPressedStateChanged != nullptr)
                    onPressedStateChanged (isDown());
            }
        };

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
        void openEditTools();
        void openSoundControls();
        void runGentleMutate();
        void showGroupLockMenu (LockGroup group, juce::Component& target);
        void updateLockIndicators();
        void saveCurrentPreset (bool forceSaveAs);
        void openPresetFile();
        void exportAudio();
        void showProUpsell (const juce::String& featureName);
        void stepPreset (int delta);
        void updateProGating();
        void updateDataStream();
        void revealSecret();
        void updateAuditionState();
        void updateAuditionAvailability();

        TextureAudioProcessor& processor;

        // Header
        LevelLed levelLed;
        TierBadge tierBadge;
        IconButton gearButton { IconButton::Icon::gear, "Settings: licence, folders, preferences and about" };
        IconButton helpButton { IconButton::Icon::help, "Help: the full manual, troubleshooting and debug tools" };
        IconButton randomiseButton { IconButton::Icon::dice, "Randomize every unlocked control. Each press starts from defaults first; locked controls are kept." };
        IconButton lockButton { IconButton::Icon::lock, "Edit locks and Gentle Mutate: protect controls from Randomize and Mutate" };
        IconButton soundButton { IconButton::Icon::sliders, "Tone and space: independent tone filter, stereo width, reverb, chorus and drive" };
        IconButton resetButton { IconButton::Icon::reset, "Reset every control to its default value. Locks do not protect against Reset." };
        IconButton fileMenuButton { IconButton::Icon::folder, "File: open, save, export audio and options" };
        juce::TextButton presetNameButton;
        IconButton previousPresetButton { IconButton::Icon::chevronLeft, "Previous preset" };
        IconButton nextPresetButton { IconButton::Icon::chevronRight, "Next preset" };
        juce::TextButton compareAButton { "A" };
        juce::TextButton compareBButton { "B" };
        juce::Label midiLearnStatus;
        MomentaryAuditionButton auditionButton;
        juce::TextButton auditionHoldButton { "HOLD" };
        juce::TextButton auditionStopButton { "STOP" };
        juce::ComboBox auditionNoteBox;
        juce::ComboBox auditionChordBox;
        juce::Slider auditionVelocitySlider;
        juce::Label auditionHeader;
        bool auditionPressed { false };

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
        std::unique_ptr<ParameterKnob> tuneKnob;
        std::unique_ptr<ParameterKnob> reverseKnob;
        std::unique_ptr<ParameterKnob> shimmerKnob;
        std::unique_ptr<ParameterChoiceBox> lfoDivisionBox;
        LevelMeter levelMeter;

        SectionHeader envelopeHeader { "Envelope" };
        SectionHeader outputHeader { "Output" };
        SectionHeader grainHeader { "Grain" };
        SectionHeader lfoHeader { "Morph LFO", colours::accentPro };
        SectionHeader layerHeader { "Layer 2", colours::accentPro };
        juce::TextButton lfoProBadge { "PRO - click to upgrade" };
        juce::TextButton layerProBadge { "PRO" };

        DataStreamView dataStream;
        ToastBar toast;

        /** Starts a new undo step on every mouse press anywhere in the editor, so one
            drag or click is one Ctrl+Z. A separate listener rather than MainPanel's
            own mouseDown, which reads positions in its own coordinates. */
        struct UndoGestureListener final : juce::MouseListener
        {
            explicit UndoGestureListener (juce::UndoManager& manager) : undoManager (manager) {}
            void mouseDown (const juce::MouseEvent&) override { undoManager.beginNewTransaction(); }
            juce::UndoManager& undoManager;
        };

        UndoGestureListener undoGestureListener;
        void performUndo (bool redo);

        // Overlays
        std::unique_ptr<SettingsPanel> settingsPanel;
        std::unique_ptr<PresetBrowser> presetBrowser;
        std::unique_ptr<HelpPanel> helpPanel;
        std::unique_ptr<SecretPanel> secretPanel;
        std::unique_ptr<DebugWindow> debugWindow;
        std::unique_ptr<EditToolsPanel> editToolsPanel;
        std::unique_ptr<SoundControlsPanel> soundPanel;
        std::unique_ptr<juce::FileChooser> fileChooser;
        PortableMediaDialogs portableMedia { *this, processor };
        juce::Component::SafePointer<juce::DialogWindow> exportDialogWindow;

        /** The pixel that reveals the hidden effect (include.md, "easter egg"). */
        juce::Rectangle<int> secretHotspot;

        /** Last values the data stream reported, so it only speaks on change. */
        std::array<float, params::numParameters> streamParameterValues {};
        int streamNote { -2 }, streamVelocity { -1 }, streamController { -2 }, streamControllerValue { -1 };
        int streamVoices { -1 }, streamGrains { -1 }, streamBlockSize { -1 };
        double streamSampleRate { -1.0 };
        juce::String streamPresetName;
        bool streamPrimed { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
    };
}
