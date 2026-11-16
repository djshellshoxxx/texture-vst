#pragma once

#include "Widgets.h"

#include <functional>

namespace texture
{
    class TextureAudioProcessor;

    /** The collapsed "Rhythm" section of the sound overlay (F19).

        Grid on or off, division, swing, probability, gate, burst and the first-grain toggle.
        Every control is an APVTS parameter, so each change is an ordinary undoable edit. The
        readout shows the tempo the grid is using. Holds no sound state. Starts collapsed.
    */
    class RhythmControlsPanel final : public juce::Component
    {
    public:
        RhythmControlsPanel (TextureAudioProcessor& processor, std::function<void()> layoutChanged);
        ~RhythmControlsPanel() override;

        /** Height the section needs at its current expansion state. */
        int preferredHeight() const;

        /** Re-reads the tempo readout and enables the controls that apply. Message thread. */
        void refresh();

        void resized() override;

    private:
        TextureAudioProcessor& processor;
        std::function<void()> onLayoutChanged;

        juce::TextButton header;
        juce::Label clockLabel;
        ParameterChoiceBox clockBox;
        juce::Label divisionLabel;
        ParameterChoiceBox divisionBox;
        ParameterKnob swingKnob;
        ParameterKnob probabilityKnob;
        ParameterKnob gateKnob;
        ParameterKnob burstKnob;
        ParameterToggle firstGrainToggle;
        juce::Label tempoLabel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhythmControlsPanel)
    };
}
