#pragma once

#include "Widgets.h"

namespace texture
{
    /** The knob the whole product is built around.

        A large custom rotary whose ring sweeps from cool blue at 0 to warm magenta
        at 1, with "MORPH" at the centre and the value as a percentage beneath.
        Everything else about its interaction comes from ParameterKnob.
    */
    class MorphKnob : public ParameterKnob
    {
    public:
        explicit MorphKnob (TextureAudioProcessor& processor);

        /** The LFO-modulated value, drawn as a ghost ring behind the real one. */
        void setModulatedValue (float value);

        /** True when the LFO is actually moving the parameter. */
        void setModulationVisible (bool shouldBeVisible);

        void paint (juce::Graphics& g) override;

    private:
        float modulatedValue { 0.0f };
        bool modulationVisible { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphKnob)
    };
}
