#pragma once

#include "../Presets/LockPolicy.h"
#include "Widgets.h"

#include <array>
#include <functional>

namespace texture
{
    class TextureAudioProcessor;

    /** Overlay for Gentle Mutate (F03) and the editing-lock list (F02).

        Holds no sound state of its own: every control reads the processor's options
        and lock set when shown, and writes through the processor's message-thread API.
    */
    class EditToolsPanel final : public juce::Component
    {
    public:
        explicit EditToolsPanel (TextureAudioProcessor& processor);
        ~EditToolsPanel() override;

        /** Raised when the user closes the panel. */
        std::function<void()> onClose;

        /** Re-reads options and lock states. Call when shown and on state changes. */
        void refresh();

        void paint (juce::Graphics& g) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

    private:
        void runMutate();
        void pushMutateOptions();
        void lockGroupClicked (LockGroup group);
        void updateLockButtonText (int index);

        TextureAudioProcessor& processor;

        juce::Label titleLabel;
        juce::TextButton closeButton { "Close" };

        juce::TextButton mutateButton { "Mutate" };
        juce::Label strengthLabel;
        juce::Slider strengthSlider;

        static constexpr std::array<LockGroup, 4> mutateGroups {
            LockGroup::envelope, LockGroup::grain,
            LockGroup::motion, LockGroup::toneSpace };
        std::array<juce::ToggleButton, 4> mutateGroupToggles;
        juce::ToggleButton categoricalToggle { "Allow categorical changes" };
        juce::Label resultLabel;

        juce::Label locksHeader;
        static constexpr std::array<LockGroup, 7> lockGroups {
            LockGroup::pitchCalibration, LockGroup::envelope,
            LockGroup::grain, LockGroup::motion,
            LockGroup::toneSpace, LockGroup::layers,
            LockGroup::performance };
        std::array<juce::TextButton, 7> lockGroupButtons;
        juce::TextButton unlockAllButton { "Unlock all" };
        juce::TextButton undoLockButton { "Undo lock change" };
        juce::Label hintLabel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditToolsPanel)
    };
}
