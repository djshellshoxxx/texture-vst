#pragma once

#include "Colors.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace texture
{
    /** The house look from theme.md: flat surfaces, one accent, depth from
        gradients and shadows rather than texture.
    */
    class TextureLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        TextureLookAndFeel();
        ~TextureLookAndFeel() override;

        // --- Rotary -----------------------------------------------------------
        void drawRotarySlider (juce::Graphics& g,
                               int x, int y, int width, int height,
                               float sliderPosProportional,
                               float rotaryStartAngle,
                               float rotaryEndAngle,
                               juce::Slider& slider) override;

        juce::Label* createSliderTextBox (juce::Slider& slider) override;

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle style, juce::Slider& slider) override;

        // --- Buttons ----------------------------------------------------------
        void drawToggleButton (juce::Graphics& g,
                               juce::ToggleButton& button,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

        void drawButtonBackground (juce::Graphics& g,
                                   juce::Button& button,
                                   const juce::Colour& backgroundColour,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown) override;

        void drawButtonText (juce::Graphics& g,
                             juce::TextButton& button,
                             bool shouldDrawButtonAsHighlighted,
                             bool shouldDrawButtonAsDown) override;

        juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override;

        // --- Menus ------------------------------------------------------------
        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override;

        void drawPopupMenuItem (juce::Graphics& g,
                                const juce::Rectangle<int>& area,
                                bool isSeparator,
                                bool isActive,
                                bool isHighlighted,
                                bool isTicked,
                                bool hasSubMenu,
                                const juce::String& text,
                                const juce::String& shortcutKeyText,
                                const juce::Drawable* icon,
                                const juce::Colour* textColour) override;

        juce::Font getPopupMenuFont() override;

        // --- Other widgets ----------------------------------------------------
        void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH,
                           juce::ComboBox& box) override;

        juce::Font getComboBoxFont (juce::ComboBox& box) override;
        juce::Font getLabelFont (juce::Label& label) override;

        void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                                               juce::Point<int> screenPos,
                                               juce::Rectangle<int> parentArea) override;

        void drawScrollbar (juce::Graphics& g, juce::ScrollBar& scrollbar,
                            int x, int y, int width, int height,
                            bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                            bool isMouseOver, bool isMouseDown) override;

        // --- Shared typography -------------------------------------------------
        /** Interface font at the requested height; falls back to the system sans if
            the bundled Inter faces are not available (22_FILE_MANIFEST.md).
        */
        static juce::Font interfaceFont (float height, bool bold = false);

        /** Tabular monospace for numeric readouts (theme.md). */
        static juce::Font monoFont (float height);

        /** Uppercase 11 px label style with the letter spacing theme.md asks for. */
        static juce::Font labelFont();

        /** Draws the brand notch: a 2 px accent diagonal in the top-left corner. */
        static void drawSignatureNotch (juce::Graphics& g, juce::Rectangle<int> windowBounds);

        /** Value arc geometry shared by every knob (7 o'clock to 5 o'clock). */
        static constexpr float rotaryStartAngle = juce::MathConstants<float>::pi * 1.25f;
        static constexpr float rotaryEndAngle = juce::MathConstants<float>::pi * 2.75f;

    private:
        static juce::Typeface::Ptr loadTypeface (bool bold);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TextureLookAndFeel)
    };
}
