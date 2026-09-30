#pragma once

#include "Widgets.h"

namespace texture
{
    class TextureAudioProcessor;

    /** The hidden GRIT tab.

        Revealed by clicking one specific pixel in the interface, and closed again
        by its own close button. Its controls are deliberately not APVTS parameters,
        so finding the easter egg never changes the host's automation list.
    */
    class SecretPanel : public juce::Component
    {
    public:
        explicit SecretPanel (TextureAudioProcessor& processor);
        ~SecretPanel() override;

        /** Raised when the user closes the tab. */
        std::function<void()> onClose;

        /** Re-reads the processor's stored settings. */
        void refreshFromProcessor();

        void paint (juce::Graphics& g) override;
        void resized() override;

    private:
        void pushToProcessor();

        TextureAudioProcessor& processor;

        SectionHeader header { "GRIT - hidden", colours::accentSecondary };
        PlainKnob bitsKnob { "BITS", " bit" };
        PlainKnob rateKnob { "RATE", "x" };
        PlainKnob mixKnob { "MIX", "" };
        juce::TextButton closeButton { "Close" };
        juce::Label blurb;

        bool isUpdatingFromProcessor { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SecretPanel)
    };
}
