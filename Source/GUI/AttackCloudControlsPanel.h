#pragma once

#include "Widgets.h"

#include <functional>

namespace texture
{
    class TextureAudioProcessor;

    /** The collapsed "Attack to cloud" section of the sound overlay (F17).

        An on switch, the hold and transition times, and the level of the direct attack. Each
        control is an APVTS parameter, so each change is an ordinary undoable edit. The note
        says that times apply to the next note. Starts collapsed.
    */
    class AttackCloudControlsPanel final : public juce::Component
    {
    public:
        AttackCloudControlsPanel (TextureAudioProcessor& processor, std::function<void()> layoutChanged);
        ~AttackCloudControlsPanel() override;

        /** Height the section needs at its current expansion state. */
        int preferredHeight() const;

        void resized() override;

    private:
        std::function<void()> onLayoutChanged;

        juce::TextButton header;
        ParameterToggle onToggle;
        ParameterKnob holdKnob;
        ParameterKnob transitionKnob;
        ParameterKnob originalKnob;
        juce::Label note;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AttackCloudControlsPanel)
    };
}
