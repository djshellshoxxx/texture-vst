#pragma once

#include "../PluginProcessor.h"

namespace texture
{
    /** The most recent finished render, shown as a draggable WAV tile (F15).

        An operating-system file drag starts only from a complete, existing file, and a
        lease keeps it on disk until the drag ends. Hosts that do not accept file drops
        still have Show file in the dialog. This is audio export: no MIDI, no project
        insertion.
    */
    class RenderedAudioTile final : public juce::Component,
                                    public juce::SettableTooltipClient
    {
    public:
        explicit RenderedAudioTile (TextureAudioProcessor& audioProcessor);

        void paint (juce::Graphics& g) override;
        void mouseDrag (const juce::MouseEvent& event) override;

    private:
        TextureAudioProcessor& processor;
        bool dragging { false };
    };
}
