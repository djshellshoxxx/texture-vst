#pragma once

#include "Widgets.h"

#include <memory>

namespace texture
{
    class TextureAudioProcessor;

    /** The in-plugin manual.

        Covers the workflow, every control, installation and preset troubleshooting,
        the version, the licence and the support links, and is the only route to the
        debug window (include.md, "Help File").
    */
    class HelpPanel : public juce::Component
    {
    public:
        explicit HelpPanel (TextureAudioProcessor& processor);
        ~HelpPanel() override;

        /** Raised when the user asks to close the panel. */
        std::function<void()> onClose;

        /** Raised when the user opens the debug window from the help page. */
        std::function<void()> onOpenDebugWindow;

        /** Jumps the view to a named section, e.g. "Troubleshooting". */
        void scrollToSection (const juce::String& sectionTitle);

        void paint (juce::Graphics& g) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

        /** The manual text, also used by the exported troubleshooting file. */
        static juce::String buildManualText();

    private:
        TextureAudioProcessor& processor;

        juce::TextEditor body;
        juce::TextButton closeButton { "Close" };
        juce::TextButton debugButton { "Open debug window" };
        juce::TextButton githubButton { "GitHub" };
        juce::TextButton homepageButton { "Circuit Drift Labs" };
        juce::TextButton supportButton { "Contact" };
        juce::TextButton dataFolderButton { "Open data folder" };

        /** One button per manual section; clicking jumps the text there. */
        juce::OwnedArray<juce::TextButton> sectionButtons;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpPanel)
    };
}
