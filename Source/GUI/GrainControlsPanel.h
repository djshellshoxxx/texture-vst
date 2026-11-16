#pragma once

#include "Widgets.h"
#include "../Engine/GrainTargetResolver.h"

#include <array>
#include <functional>
#include <memory>

namespace texture
{
    class TextureAudioProcessor;

    /** The collapsed "Grains" section of the sound overlay (F13).

        Five manual controls, each with a Follow MORPH switch, the effective value the
        voices are using now, and a pool readout. Holds no sound state: the knobs are
        APVTS-bound, and Follow goes through TextureAudioProcessor::setGrainFollow so the
        value is copied before Follow is cleared. The section starts collapsed so the
        default view stays simple.
    */
    class GrainControlsPanel final : public juce::Component
    {
    public:
        GrainControlsPanel (TextureAudioProcessor& processor, std::function<void()> layoutChanged);
        ~GrainControlsPanel() override;

        /** Height the section needs at its current expansion state. */
        int preferredHeight() const;

        /** Re-reads Follow states, effective readouts and the pool status. Message thread. */
        void refresh();

        void resized() override;

    private:
        struct Column;

        TextureAudioProcessor& processor;
        std::function<void()> onLayoutChanged;

        juce::TextButton header;
        juce::Label poolLabel;
        std::array<std::unique_ptr<Column>, numGrainFields> columns;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainControlsPanel)
    };
}
