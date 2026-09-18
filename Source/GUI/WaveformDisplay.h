#pragma once

#include "../Engine/SampleBuffer.h"
#include "Colors.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace texture
{
    /** Draws the cached peak/RMS envelope of a sample plus a playhead cursor.

        The envelope is rendered once into an image and re-blitted, so the 30 Hz
        playhead repaint costs almost nothing (20_BEST_PRACTICES.md, "GUI code").
    */
    class WaveformDisplay : public juce::Component
    {
    public:
        WaveformDisplay();

        /** Swaps in a new sample. Pass nullptr to show the empty state. */
        void setSample (SampleBufferPtr newSample);

        /** Normalised playhead position in [0, 1). */
        void setPlayheadPosition (float normalisedPosition);

        /** Where the user last clicked, drawn as a start marker. */
        void setStartMarker (float normalisedPosition);
        float getStartMarker() const noexcept { return startMarker; }

        bool hasSample() const noexcept { return sample != nullptr && sample->isValid(); }

        void paint (juce::Graphics& g) override;
        void resized() override;

        /** Colour the envelope is drawn in; MainPanel tints it by MORPH. */
        void setEnvelopeColour (juce::Colour newColour);

    private:
        void rebuildImage();

        SampleBufferPtr sample;
        juce::Image cachedEnvelope;
        juce::Colour envelopeColour { colours::accentPrimary };
        float playhead { 0.0f };
        float startMarker { 0.0f };
        bool imageIsStale { true };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
    };
}
