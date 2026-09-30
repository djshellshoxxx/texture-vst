#pragma once

#include "Colors.h"
#include "LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <deque>
#include <functional>

namespace texture
{
    class TextureAudioProcessor;

    /** Shared right-click behaviour for every control in the plugin.

        include.md requires that right-clicking any control offers MIDI mapping, a
        reset to default and direct numeric entry; this puts that in one place so no
        control can accidentally ship without it.
    */
    class ControlContextMenu
    {
    public:
        /** Shows the menu for a slider bound to `parameterID`. */
        static void showFor (juce::Slider& slider,
                             TextureAudioProcessor& processor,
                             const juce::String& parameterID,
                             std::function<void()> onChanged);

        /** Shows the menu for a choice box bound to `parameterID`. */
        static void showFor (juce::ComboBox& box,
                             TextureAudioProcessor& processor,
                             const juce::String& parameterID);

        /** Shows the menu for a button bound to `parameterID`. */
        static void showFor (juce::Button& button,
                             TextureAudioProcessor& processor,
                             const juce::String& parameterID,
                             std::function<void()> onChanged);

        /** The clipboard shared by copy/paste value across all controls. */
        static void setClipboardValue (double value);
        static double getClipboardValue();
        static bool hasClipboardValue();
    };

    /** A rotary bound to one APVTS parameter, with the house drag behaviour.

        Vertical drag adjusts, Shift is 10x finer, Ctrl/Cmd is 100x finer, double
        click resets, right click opens the shared context menu (09_GUI_SPEC.md).
    */
    class ParameterKnob : public juce::Slider,
                          private juce::Timer
    {
    public:
        ParameterKnob (TextureAudioProcessor& processor,
                       const juce::String& parameterID,
                       const juce::String& labelText,
                       const juce::String& tooltipText);

        ~ParameterKnob() override;

        void mouseDown (const juce::MouseEvent& event) override;
        void mouseUp (const juce::MouseEvent& event) override;
        void paint (juce::Graphics& g) override;
        void resized() override;

        const juce::String& getParameterID() const noexcept { return parameterID; }

        /** Draws the label beneath the knob. Off for the big MORPH knob. */
        void setShowsLabel (bool shouldShow);

        /** Ctrl/Cmd+click steps through these values instead of dragging. */
        void setCoarseSteps (juce::Array<double> steps);

        /** Called by the owner's timer: pulses the ring while MIDI learn waits on this knob. */
        void updateLearnIndicator();

        void valueChanged() override;

    protected:
        /** The proportion to draw: follows the value, easing external jumps over 80 ms. */
        float getDisplayedProportion() const noexcept { return displayedProportion; }

        /** Draws the pulsing MIDI-learn ring inside `area` when armed. */
        void paintLearnRing (juce::Graphics& g, juce::Rectangle<float> area) const;

    private:
        void updateSensitivity (const juce::MouseEvent& event);
        void timerCallback() override;

        float displayedProportion { 0.0f };
        float animationFrom { 0.0f };
        float animationTo { 0.0f };
        double animationStartMs { 0.0 };
        bool isLearning { false };

        TextureAudioProcessor& processor;
        juce::String parameterID;
        juce::String labelText;
        juce::Array<double> coarseSteps;
        bool showsLabel { true };

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterKnob)
    };

    /** A dropdown bound to one APVTS choice parameter, with the shared right-click menu. */
    class ParameterChoiceBox : public juce::ComboBox
    {
    public:
        ParameterChoiceBox (TextureAudioProcessor& processor,
                            const juce::String& parameterID,
                            const juce::StringArray& items,
                            const juce::String& tooltipText);
        ~ParameterChoiceBox() override;

        void mouseDown (const juce::MouseEvent& event) override;

    private:
        TextureAudioProcessor& processor;
        juce::String parameterID;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterChoiceBox)
    };

    /** A knob that is not an APVTS parameter (the hidden GRIT controls).
        Same look and drag feel as ParameterKnob, hover readout, and a right-click
        menu with reset and value entry; it has no MIDI learn because MIDI learn
        maps host parameters and GRIT deliberately is not one.
    */
    class PlainKnob : public juce::Slider
    {
    public:
        PlainKnob (juce::String labelText, juce::String suffix);

        void mouseDown (const juce::MouseEvent& event) override;
        void mouseUp (const juce::MouseEvent& event) override;
        void paint (juce::Graphics& g) override;

    private:
        juce::String labelText;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlainKnob)
    };

    /** Stereo output meter (theme.md, "Meters").

        Smooth teal -> orange -> yellow -> red gradient, a 1 px peak hold that waits
        1.5 s and then falls at 20 dB/s, and the peak in mono at the top right.
    */
    class LevelMeter : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        LevelMeter();

        /** Feeds the latest block peaks (linear). Called at the GUI refresh rate. */
        void pushLevels (float left, float right, double nowMs);

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& event) override;

        static constexpr float minDb = -60.0f;
        static constexpr float maxDb = 6.0f;

    private:
        struct Channel
        {
            float levelDb { minDb };
            float holdDb { minDb };
            double holdSinceMs { 0.0 };
        };

        float dbToX (float db, float width) const noexcept;

        std::array<Channel, 2> channels;
        float peakReadoutDb { minDb };
        double lastUpdateMs { 0.0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
    };

    /** A pill toggle bound to one APVTS parameter. */
    class ParameterToggle : public juce::ToggleButton
    {
    public:
        ParameterToggle (TextureAudioProcessor& processor,
                         const juce::String& parameterID,
                         const juce::String& buttonText,
                         const juce::String& tooltipText);

        ~ParameterToggle() override;

        void mouseDown (const juce::MouseEvent& event) override;

        const juce::String& getParameterID() const noexcept { return parameterID; }

    private:
        TextureAudioProcessor& processor;
        juce::String parameterID;

        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterToggle)
    };

    /** The output LED in the top-left corner (theme.md, "Animations").

        Dark grey at silence, brightening to white as the output approaches 0 dBFS,
        latching red for as long as the signal is over.
    */
    class LevelLed : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        LevelLed();

        /** Feeds the latest peak. Called from the editor's 30 Hz timer. */
        void setLevel (float peak);

        void paint (juce::Graphics& g) override;

    private:
        float level { 0.0f };
        bool isOver { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelLed)
    };

    /** The rolling internals readout that fills otherwise empty panel space.

        Real data, presented quietly: the top and bottom lines fade to nothing and
        the scroll stops when the plugin has nothing new to say (theme.md).
    */
    class DataStreamView : public juce::Component
    {
    public:
        static constexpr int visibleLines = 10;

        DataStreamView();

        /** Adds a line if it differs from the last one. Returns true if it scrolled. */
        bool push (const juce::String& line);

        /** Advances the glide. Called at the GUI refresh rate. */
        void tick();

        void clear();

        void paint (juce::Graphics& g) override;

    private:
        std::deque<juce::String> lines;
        juce::String lastLine;

        /** 0..1 of a line still to scroll; eases to 0 so new lines glide in. */
        float scrollOffset { 0.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DataStreamView)
    };

    /** The transient status banner along the bottom of the window.

        Fades in over 150 ms, holds for 3 seconds, fades out over 300 ms, and never
        stacks: a new message replaces the current one immediately (09_GUI_SPEC.md).
    */
    class ToastBar : public juce::Component,
                     private juce::Timer
    {
    public:
        enum class Severity { info, warning, error };

        ToastBar();
        ~ToastBar() override;

        void show (const juce::String& message, Severity severity);
        void dismiss();

        bool isShowing() const noexcept { return phase != Phase::idle; }

        void paint (juce::Graphics& g) override;

    private:
        enum class Phase { idle, fadingIn, holding, fadingOut };

        void timerCallback() override;

        static constexpr int fadeInMs = 150;
        static constexpr int holdMs = 3000;
        static constexpr int fadeOutMs = 300;
        static constexpr int tickMs = 1000 / 60;

        Phase phase { Phase::idle };
        juce::String text;
        Severity severity { Severity::info };
        float opacity { 0.0f };
        int elapsedMs { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToastBar)
    };

    /** A small section header: uppercase text with the 2 px accent bar (theme.md). */
    class SectionHeader : public juce::Component
    {
    public:
        explicit SectionHeader (juce::String titleText, juce::Colour accentColour = colours::accentPrimary);

        void setTitle (const juce::String& newTitle);
        void paint (juce::Graphics& g) override;

    private:
        juce::String title;
        juce::Colour accent;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionHeader)
    };

    /** The FREE/PRO badge in the header. Clicking opens the upgrade view. */
    class TierBadge : public juce::Button
    {
    public:
        TierBadge();

        void setPro (bool shouldShowPro);
        bool isPro() const noexcept { return pro; }

        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    private:
        bool pro { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TierBadge)
    };

    /** A borderless icon button that paints a supplied drawing routine. */
    class IconButton : public juce::Button
    {
    public:
        enum class Icon { gear, help, dice, reset, chevronDown, folder, save, compare, chevronLeft, chevronRight };

        IconButton (Icon iconToDraw, const juce::String& tooltipText);

        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

        void clicked() override;

    private:
        static void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds, juce::Colour colour);

        Icon icon;

        /** Time of the last click, for the 100 ms accent flash (theme.md). */
        juce::uint32 flashStartMs { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
    };
}
