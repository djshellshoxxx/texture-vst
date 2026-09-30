#pragma once

#include "Widgets.h"
#include "../Utility/ParameterIDs.h"

#include <array>
#include <memory>

namespace texture
{
    class TextureAudioProcessor;

    /** The live internals view, reached from the help page.

        Shows the raw state of the plugin updating as controls move, owns the crash
        logging switch and the troubleshooting export, and carries the destructive
        reset (include.md, "extra features"; 19_DEBUGGING.md).
    */
    class DebugPanel : public juce::Component,
                       private juce::Timer
    {
    public:
        explicit DebugPanel (TextureAudioProcessor& processor);
        ~DebugPanel() override;

        std::function<void()> onClose;

        void paint (juce::Graphics& g) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

    private:
        /** Monospaced text pane. A plain component inside a Viewport rather than a
            TextEditor, because setting a TextEditor's text resets its scroll and
            this view refreshes five times a second.
        */
        class Readout : public juce::Component
        {
        public:
            Readout();

            /** Replaces the content and resizes to fit. Keeps the caller's scroll. */
            void setLines (juce::StringArray newLines, int availableWidth);

            void paint (juce::Graphics& g) override;

        private:
            juce::StringArray lines;
        };

        void timerCallback() override;
        void refreshNow();

        /** Compares against the last poll and records what changed (include.md:
            "show the data as features are invoked and settings are changed"). */
        void recordEvents();
        void addEvent (const juce::String& text);

        static constexpr int maxEvents = 60;
        juce::StringArray events;
        std::array<float, params::numParameters> lastParameterValues {};
        int lastNote { -2 }, lastVelocity { -1 }, lastController { -2 }, lastControllerValue { -1 };
        juce::String lastPresetName, lastSampleName;
        bool eventsPrimed { false };
        void exportTroubleshootingFile();
        void confirmHardReset();

        TextureAudioProcessor& processor;

        juce::Viewport readoutViewport;
        Readout readout;
        juce::ToggleButton crashLogToggle { "Create log file on crash" };
        juce::TextButton exportButton { "Export troubleshooting file" };
        juce::TextButton copyLogButton { "Copy log to clipboard" };
        juce::TextButton revealLogButton { "Reveal log" };
        juce::TextButton resetPeakButton { "Reset peak CPU" };
        juce::TextButton hardResetButton { "Reset all settings to default" };
        juce::TextButton closeButton { "Close" };

        juce::Label explanation;
        std::unique_ptr<juce::FileChooser> fileChooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DebugPanel)
    };

    /** A resizable top-level window hosting a DebugPanel. */
    class DebugWindow : public juce::DocumentWindow
    {
    public:
        DebugWindow (TextureAudioProcessor& processor, juce::LookAndFeel& lookAndFeelToUse);
        ~DebugWindow() override;

        void closeButtonPressed() override;

        /** Raised when the window closes itself so the owner can drop it. */
        std::function<void()> onClose;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DebugWindow)
    };
}
