#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>

/** The palette.

    The neutrals, spacing and control shapes come from `theme.md`, which is the
    house style shared by every plugin in the range. TEXTURE re-tints the single
    accent it is allowed to claim: the MORPH ring sweeps from the cool blue of
    `09_GUI_SPEC.md` to its warm magenta, and those two also serve as the primary
    and secondary accents everywhere else.
*/
namespace texture::colours
{
    // --- Neutrals (theme.md) -------------------------------------------------
    inline const juce::Colour background   { 0xff0e1116 };
    inline const juce::Colour panel        { 0xff171b22 };
    inline const juce::Colour panelEdge    { 0xff2a303a };
    inline const juce::Colour textPrimary  { 0xffe6e8ec };
    inline const juce::Colour textMuted    { 0xff8a929e };
    inline const juce::Colour shadow       { 0x8c000000 };   // rgba(0,0,0,0.55)

    // --- Accents -------------------------------------------------------------
    /** Cool blue: MORPH at 0, primary accent, active values and selections. */
    inline const juce::Colour accentPrimary   { 0xff7bd1ff };

    /** Warm magenta: MORPH at 1, secondary accent, modulation and links. */
    inline const juce::Colour accentSecondary { 0xffff7bc8 };

    /** Teal, reserved for Pro-only affordances so they read as a different class. */
    inline const juce::Colour accentPro       { 0xff4fb6c4 };

    // --- Status --------------------------------------------------------------
    inline const juce::Colour success { 0xff7bc96f };
    inline const juce::Colour warning { 0xfff2c14e };
    inline const juce::Colour error   { 0xffff6b6b };

    /** The rolling internals readout behind empty panel space. */
    inline const juce::Colour dataStream { 0xff7bf59a };

    // --- Knob body gradient (theme.md) --------------------------------------
    inline const juce::Colour knobTop    { 0xff232833 };
    inline const juce::Colour knobBottom { 0xff14181f };

    /** MORPH ring colour for a value in [0, 1]: cool blue to warm magenta. */
    inline juce::Colour morphRing (float normalisedValue)
    {
        const auto t = juce::jlimit (0.0f, 1.0f, normalisedValue);
        return accentPrimary.interpolatedWith (accentSecondary, t);
    }

    /** Output LED colour: dark grey at silence, white near 0 dBFS, red above it. */
    inline juce::Colour levelLed (float peak)
    {
        if (peak >= 1.0f)
            return error;

        // -60 dBFS reads as unlit; the top 60 dB maps onto grey -> white.
        const auto db = peak > 0.0f ? juce::Decibels::gainToDecibels (peak, -60.0f) : -60.0f;
        const auto t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

        return juce::Colour (0xff3a3f48).interpolatedWith (juce::Colours::white, t);
    }
}

/** Layout constants shared by every panel. theme.md specifies an 8 px base grid. */
namespace texture::layout
{
    inline constexpr int grid = 8;
    inline constexpr int windowEdgePadding = 16;
    inline constexpr int headerHeight = 40;
    inline constexpr int footerHeight = 24;

    inline constexpr float windowCornerRadius = 6.0f;
    inline constexpr float panelCornerRadius = 4.0f;
    inline constexpr float controlCornerRadius = 2.0f;

    inline constexpr int defaultWidth = 720;
    inline constexpr int defaultHeight = 480;
    inline constexpr int minimumWidth = 540;
    inline constexpr int minimumHeight = 360;
    inline constexpr int maximumWidth = 1440;
    inline constexpr int maximumHeight = 960;

    inline constexpr int settingsPanelWidth = 320;

    inline constexpr int morphKnobDiameter = 180;
    inline constexpr int smallKnobDiameter = 44;

    /** Repaint rate for the meters, waveform playhead and data stream. */
    inline constexpr int refreshHz = 30;
}
