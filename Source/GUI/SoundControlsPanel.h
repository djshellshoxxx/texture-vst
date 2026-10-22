#pragma once

#include "Widgets.h"

#include <functional>
#include <memory>

namespace texture
{
    class TextureAudioProcessor;

    /** Overlay for the advanced sound controls: independent tone and space (F07).

        Holds no sound state. Every control is an APVTS-bound widget, so changes go
        through the same parameter path, undo and automation as the main panel. The
        body scrolls, so the overlay stays usable at 1280x720 with OS scaling.
    */
    class SoundControlsPanel final : public juce::Component,
                                     private juce::Timer
    {
    public:
        explicit SoundControlsPanel (TextureAudioProcessor& processor);
        ~SoundControlsPanel() override;

        /** Raised when the user closes the panel. */
        std::function<void()> onClose;

        /** Raised when the user stars a variation: the owner previews it and opens the Save dialog. */
        std::function<void (std::size_t)> onStarVariation;

        /** Re-reads the mode and enables the controls that apply. Call when shown. */
        void refresh();

        void paint (juce::Graphics& g) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

    private:
        class Body;

        void timerCallback() override;

        juce::Label titleLabel;
        juce::TextButton closeButton { "Close" };

        juce::Viewport viewport;
        std::unique_ptr<Body> body;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundControlsPanel)
    };
}
