#pragma once

#include "CurveEditor.h"
#include "Widgets.h"

#include <functional>

namespace texture
{
    class TextureAudioProcessor;

    /** The collapsed "Motion shapes" section of the sound overlay (F14).

        Shape and retrigger are choices, phase and slew are knobs, and the Curve shape is
        drawn by CurveEditor. Shape, retrigger, phase and slew go through the APVTS like every
        other control. Curve edits go through TextureAudioProcessor::setModulationCurve, so
        each edit is one undo step. Starts collapsed.
    */
    class ModulationPanel final : public juce::Component
    {
    public:
        ModulationPanel (TextureAudioProcessor& processor, std::function<void()> layoutChanged);
        ~ModulationPanel() override;

        int preferredHeight() const;

        /** Re-reads the curve and enables the curve controls that apply. Message thread. */
        void refresh();

        void resized() override;

    private:
        TextureAudioProcessor& processor;
        std::function<void()> onLayoutChanged;

        juce::TextButton header;
        juce::Label shapeLabel;
        ParameterChoiceBox shapeBox;
        juce::Label retriggerLabel;
        ParameterChoiceBox retriggerBox;
        ParameterKnob phaseKnob;
        ParameterKnob slewKnob;
        CurveEditor curveEditor;
        juce::TextButton resetCurve { "Reset curve" };
        juce::Label note;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulationPanel)
    };
}
