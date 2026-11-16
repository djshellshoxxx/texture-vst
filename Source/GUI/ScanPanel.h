#pragma once

#include "Widgets.h"

#include <array>
#include <functional>

namespace texture
{
    class TextureAudioProcessor;

    /** The collapsed "Scan and freeze" section of the sound overlay (F10).

        One row pair per layer: a mode choice and Freeze, then the read position, scan speed
        and freeze spread. Every control is APVTS-bound, so automation and undo apply as for
        any other control. Starts collapsed. Position moves where grains read; it does not
        trim the sample, which the waveform does.
    */
    class ScanPanel final : public juce::Component
    {
    public:
        ScanPanel (TextureAudioProcessor& processor, std::function<void()> layoutChanged);
        ~ScanPanel() override;

        int preferredHeight() const;
        void resized() override;

    private:
        struct LayerRow;

        std::function<void()> onLayoutChanged;

        juce::TextButton header;
        std::array<std::unique_ptr<LayerRow>, 2> rows;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScanPanel)
    };
}
