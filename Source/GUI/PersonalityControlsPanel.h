#pragma once

#include "Widgets.h"

#include <functional>

namespace texture
{
    class TextureAudioProcessor;

    /** The collapsed "Character" section of the sound overlay (F16).

        A compact selector picks one MORPH personality and an intensity knob sets how far it
        departs from the original mapping. The line under the selector gives one musical
        example for the chosen style. Both controls are APVTS parameters, so each change is an
        ordinary undoable edit. Starts collapsed.
    */
    class PersonalityControlsPanel final : public juce::Component
    {
    public:
        PersonalityControlsPanel (TextureAudioProcessor& processor, std::function<void()> layoutChanged);
        ~PersonalityControlsPanel() override;

        /** Height the section needs at its current expansion state. */
        int preferredHeight() const;

        /** Re-reads the example for the chosen style. Message thread. */
        void refresh();

        void resized() override;

    private:
        TextureAudioProcessor& processor;
        std::function<void()> onLayoutChanged;

        juce::TextButton header;
        ParameterChoiceBox styleBox;
        ParameterKnob intensityKnob;
        juce::Label example;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PersonalityControlsPanel)
    };
}
