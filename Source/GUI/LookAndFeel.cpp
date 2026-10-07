#include "LookAndFeel.h"

#if __has_include(<BinaryData.h>)
 #include <BinaryData.h>
 #define TEXTURE_HAS_BINARY_DATA 1
#else
 #define TEXTURE_HAS_BINARY_DATA 0
#endif

namespace texture
{
    namespace
    {
        constexpr float knobGapToArc = 4.0f;
        constexpr float valueArcThickness = 3.0f;
        constexpr float indicatorThickness = 2.0f;
    }

    juce::Typeface::Ptr TextureLookAndFeel::loadTypeface (bool bold)
    {
       #if TEXTURE_HAS_BINARY_DATA
        int size = 0;
        const void* data = bold ? BinaryData::getNamedResource ("InterBold_ttf", size)
                                : BinaryData::getNamedResource ("InterRegular_ttf", size);

        if (data != nullptr && size > 0)
            return juce::Typeface::createSystemTypefaceFor (data, static_cast<size_t> (size));
       #else
        juce::ignoreUnused (bold);
       #endif

        return {};
    }

    juce::Font TextureLookAndFeel::interfaceFont (float height, bool bold)
    {
        static juce::Typeface::Ptr regular = loadTypeface (false);
        static juce::Typeface::Ptr boldFace = loadTypeface (true);

        if (const auto& face = bold ? boldFace : regular)
            return juce::Font (juce::FontOptions (face).withHeight (height));

        // Inter was not bundled; fall back to the platform sans and say so once.
        static const bool warnedOnce = []
        {
            juce::Logger::writeToLog ("[warn] [gui] Inter typefaces unavailable; using the system sans.");
            return true;
        }();
        juce::ignoreUnused (warnedOnce);

        return juce::Font (juce::FontOptions()
                               .withHeight (height)
                               .withStyle (bold ? "Bold" : "Regular"));
    }

    juce::Font TextureLookAndFeel::monoFont (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                              juce::Font::plain));
    }

    juce::Font TextureLookAndFeel::labelFont()
    {
        return interfaceFont (11.0f, true).withExtraKerningFactor (0.08f);
    }

    void TextureLookAndFeel::drawSignatureNotch (juce::Graphics& g, juce::Rectangle<int> windowBounds)
    {
        // 12 px, 45 degrees, 2 px thick: the brand mark shared across the range.
        const auto origin = windowBounds.getTopLeft().toFloat() + juce::Point<float> (6.0f, 6.0f);

        g.setColour (colours::accentPrimary);
        g.drawLine (origin.x, origin.y + 12.0f, origin.x + 12.0f, origin.y, 2.0f);
    }

    TextureLookAndFeel::TextureLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::DocumentWindow::textColourId, colours::textPrimary);

        setColour (juce::Label::textColourId, colours::textPrimary);
        setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);

        setColour (juce::Slider::textBoxTextColourId, colours::textPrimary);
        setColour (juce::Slider::textBoxBackgroundColourId, colours::panel);
        setColour (juce::Slider::textBoxOutlineColourId, colours::panelEdge);
        setColour (juce::Slider::rotarySliderFillColourId, colours::accentPrimary);
        setColour (juce::Slider::rotarySliderOutlineColourId, colours::panelEdge);
        setColour (juce::Slider::thumbColourId, colours::accentPrimary);
        setColour (juce::Slider::trackColourId, colours::accentPrimary);
        setColour (juce::Slider::backgroundColourId, colours::panelEdge);

        setColour (juce::TextButton::buttonColourId, colours::panel);
        setColour (juce::TextButton::buttonOnColourId, colours::accentPrimary.withAlpha (0.15f));
        setColour (juce::TextButton::textColourOffId, colours::textMuted);
        setColour (juce::TextButton::textColourOnId, colours::accentPrimary);

        setColour (juce::ToggleButton::textColourId, colours::textPrimary);
        setColour (juce::ToggleButton::tickColourId, colours::accentPrimary);
        setColour (juce::ToggleButton::tickDisabledColourId, colours::panelEdge);

        setColour (juce::ComboBox::backgroundColourId, colours::panel);
        setColour (juce::ComboBox::textColourId, colours::textPrimary);
        setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
        setColour (juce::ComboBox::arrowColourId, colours::textMuted);
        setColour (juce::ComboBox::buttonColourId, colours::panel);

        setColour (juce::PopupMenu::backgroundColourId, colours::panel);
        setColour (juce::PopupMenu::textColourId, colours::textPrimary);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accentPrimary.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, colours::textPrimary);
        setColour (juce::PopupMenu::headerTextColourId, colours::textMuted);

        setColour (juce::TextEditor::backgroundColourId, colours::background);
        setColour (juce::TextEditor::textColourId, colours::textPrimary);
        setColour (juce::TextEditor::outlineColourId, colours::panelEdge);
        setColour (juce::TextEditor::focusedOutlineColourId, colours::accentPrimary);
        setColour (juce::TextEditor::highlightColourId, colours::accentPrimary.withAlpha (0.3f));
        setColour (juce::TextEditor::highlightedTextColourId, colours::textPrimary);
        setColour (juce::CaretComponent::caretColourId, colours::accentPrimary);

        setColour (juce::ListBox::backgroundColourId, colours::background);
        setColour (juce::ListBox::textColourId, colours::textPrimary);
        setColour (juce::ListBox::outlineColourId, colours::panelEdge);

        setColour (juce::ScrollBar::thumbColourId, colours::panelEdge.brighter (0.4f));
        setColour (juce::ScrollBar::trackColourId, colours::panel);

        setColour (juce::TooltipWindow::backgroundColourId, colours::panel);
        setColour (juce::TooltipWindow::textColourId, colours::textMuted);
        setColour (juce::TooltipWindow::outlineColourId, colours::panelEdge);

        setColour (juce::AlertWindow::backgroundColourId, colours::panel);
        setColour (juce::AlertWindow::textColourId, colours::textPrimary);
        setColour (juce::AlertWindow::outlineColourId, colours::panelEdge);
    }

    TextureLookAndFeel::~TextureLookAndFeel() = default;

    void TextureLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                               int x, int y, int width, int height,
                                               float sliderPosProportional,
                                               float rotaryStart,
                                               float rotaryEnd,
                                               juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();

        // The value arc lives outside the body with a 4 px gap (theme.md).
        const auto arcRadius = diameter * 0.5f - valueArcThickness * 0.5f;
        const auto bodyRadius = juce::jmax (4.0f, arcRadius - knobGapToArc - valueArcThickness * 0.5f);

        const auto isEnabled = slider.isEnabled();
        const auto hovered = isEnabled && slider.isMouseOverOrDragging();

        // Hover brightens the control by about 8% (theme.md, interaction feel).
        auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

        if (hovered)
            accent = accent.brighter (0.08f);

        const auto activeColour = isEnabled ? accent : accent.withAlpha (0.35f);
        const auto bodyTop = hovered ? colours::knobTop.brighter (0.08f) : colours::knobTop;
        const auto bodyBottom = hovered ? colours::knobBottom.brighter (0.08f) : colours::knobBottom;

        const auto angle = rotaryStart + sliderPosProportional * (rotaryEnd - rotaryStart);

        // --- Unfilled arc -----------------------------------------------------
        {
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 rotaryStart, rotaryEnd, true);

            g.setColour (colours::panelEdge.withAlpha (0.6f));
            g.strokePath (track, juce::PathStrokeType (valueArcThickness,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // --- Filled arc -------------------------------------------------------
        // A knob tagged "bipolar" (TUNE) fills outwards from twelve o'clock, so its
        // default reads as empty rather than half full.
        const auto bipolar = static_cast<bool> (slider.getProperties().getWithDefault ("bipolar", false));
        const auto arcFrom = bipolar ? rotaryStart + 0.5f * (rotaryEnd - rotaryStart) : rotaryStart;

        if (bipolar ? std::abs (sliderPosProportional - 0.5f) > 0.0001f : sliderPosProportional > 0.0001f)
        {
            juce::Path fill;
            fill.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                juce::jmin (arcFrom, angle), juce::jmax (arcFrom, angle), true);

            g.setColour (activeColour);
            g.strokePath (fill, juce::PathStrokeType (valueArcThickness,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }

        // --- Body -------------------------------------------------------------
        const juce::Rectangle<float> body (centre.x - bodyRadius, centre.y - bodyRadius,
                                           bodyRadius * 2.0f, bodyRadius * 2.0f);

        g.setGradientFill (juce::ColourGradient (bodyTop, body.getCentreX(), body.getY(),
                                                 bodyBottom, body.getCentreX(), body.getBottom(),
                                                 false));
        g.fillEllipse (body);

        g.setColour (colours::panelEdge);
        g.drawEllipse (body.reduced (0.5f), 1.0f);

        // --- Indicator ---------------------------------------------------------
        {
            const auto inner = bodyRadius * 0.32f;
            const auto outer = bodyRadius * 0.86f;

            const juce::Point<float> from (centre.x + std::sin (angle) * inner,
                                           centre.y - std::cos (angle) * inner);
            const juce::Point<float> to (centre.x + std::sin (angle) * outer,
                                         centre.y - std::cos (angle) * outer);

            g.setColour (activeColour);
            g.drawLine ({ from, to }, indicatorThickness);
        }

        // --- Centre dot: accent when moved off default, muted when at rest ------
        {
            const auto atDefault = juce::approximatelyEqual (
                slider.getValue(), slider.getDoubleClickReturnValue());

            g.setColour (atDefault ? colours::textMuted.withAlpha (0.6f) : activeColour);
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (centre));
        }
    }

    void TextureLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPos, float minSliderPos, float maxSliderPos,
                                               juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                    minSliderPos, maxSliderPos, style, slider);
            return;
        }

        juce::ignoreUnused (minSliderPos, maxSliderPos);

        // theme.md sliders: 4 px track, accent fill, 16 x 24 thumb, ticks outside the track.
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto centreY = bounds.getCentreY();
        const auto track = juce::Rectangle<float> (bounds.getX(), centreY - 2.0f, bounds.getWidth(), 4.0f);
        const auto accent = slider.isEnabled() ? colours::accentPrimary : colours::accentPrimary.withAlpha (0.35f);

        g.setColour (colours::textMuted.withAlpha (0.4f));

        for (int i = 0; i <= 5; ++i)
        {
            const auto tickX = track.getX() + track.getWidth() * static_cast<float> (i) / 5.0f;
            g.fillRect (tickX, track.getBottom() + 6.0f, 1.0f, 3.0f);
        }

        g.setColour (colours::panelEdge);
        g.fillRoundedRectangle (track, 2.0f);

        g.setColour (accent);
        g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

        const auto thumbHeight = juce::jmin (24.0f, bounds.getHeight());
        const auto thumb = juce::Rectangle<float> (16.0f, thumbHeight).withCentre ({ sliderPos, centreY });
        const auto hovered = slider.isMouseOverOrDragging();

        g.setGradientFill (juce::ColourGradient (hovered ? colours::knobTop.brighter (0.08f) : colours::knobTop,
                                                 thumb.getCentreX(), thumb.getY(),
                                                 colours::knobBottom, thumb.getCentreX(), thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, layout::controlCornerRadius);

        g.setColour (accent);
        g.drawRoundedRectangle (thumb.reduced (0.5f), layout::controlCornerRadius, 1.0f);
    }

    juce::Label* TextureLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (monoFont (13.0f));
        label->setJustificationType (juce::Justification::centred);
        return label;
    }

    void TextureLookAndFeel::drawToggleButton (juce::Graphics& g,
                                               juce::ToggleButton& button,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
    {
        juce::ignoreUnused (shouldDrawButtonAsDown);

        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto isOn = button.getToggleState();
        const auto isEnabled = button.isEnabled();

        auto accent = button.findColour (juce::ToggleButton::tickColourId);

        if (! isEnabled)
            accent = accent.withAlpha (0.35f);

        // Pill shape: rounded to half its height (09_GUI_SPEC.md, reverb toggle).
        const auto radius = juce::jmin (bounds.getHeight() * 0.5f, 14.0f);

        g.setColour (isOn ? accent.withAlpha (0.15f) : colours::panel);
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (isOn ? accent : colours::panelEdge);
        g.drawRoundedRectangle (bounds, radius, 1.0f);

        if (shouldDrawButtonAsHighlighted && isEnabled)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (bounds, radius);
        }

        g.setColour (isOn ? accent : (isEnabled ? colours::textMuted : colours::textMuted.withAlpha (0.5f)));
        g.setFont (labelFont());
        g.drawText (button.getButtonText().toUpperCase(), bounds, juce::Justification::centred, false);
    }

    void TextureLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                                   juce::Button& button,
                                                   const juce::Colour& backgroundColour,
                                                   bool shouldDrawButtonAsHighlighted,
                                                   bool shouldDrawButtonAsDown)
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto isOn = button.getToggleState();
        const auto accent = colours::accentPrimary;

        auto fill = isOn ? accent.withAlpha (0.15f) : backgroundColour;

        if (shouldDrawButtonAsDown)
            fill = accent.withAlpha (0.28f);   // 100 ms accent flash (theme.md)
        else if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter (0.08f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, layout::panelCornerRadius);

        g.setColour (isOn ? accent : colours::panelEdge);
        g.drawRoundedRectangle (bounds, layout::panelCornerRadius, 1.0f);
    }

    void TextureLookAndFeel::drawButtonText (juce::Graphics& g,
                                             juce::TextButton& button,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown)
    {
        juce::ignoreUnused (shouldDrawButtonAsDown);

        auto colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                                 : juce::TextButton::textColourOffId);

        if (! button.isEnabled())
            colour = colour.withAlpha (0.4f);
        else if (shouldDrawButtonAsHighlighted)
            colour = colour.brighter (0.2f);

        g.setColour (colour);
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, true);
    }

    juce::Font TextureLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
    {
        juce::ignoreUnused (button);
        return interfaceFont (juce::jlimit (10.0f, 14.0f, static_cast<float> (buttonHeight) * 0.45f), true);
    }

    void TextureLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        const juce::Rectangle<float> bounds (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));

        g.setColour (colours::panel);
        g.fillRoundedRectangle (bounds, layout::panelCornerRadius);

        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (bounds.reduced (0.5f), layout::panelCornerRadius, 1.0f);
    }

    void TextureLookAndFeel::drawPopupMenuItem (juce::Graphics& g,
                                                const juce::Rectangle<int>& area,
                                                bool isSeparator,
                                                bool isActive,
                                                bool isHighlighted,
                                                bool isTicked,
                                                bool hasSubMenu,
                                                const juce::String& text,
                                                const juce::String& shortcutKeyText,
                                                const juce::Drawable* icon,
                                                const juce::Colour* textColour)
    {
        juce::ignoreUnused (icon);

        if (isSeparator)
        {
            g.setColour (colours::panelEdge);
            g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
            return;
        }

        auto bounds = area.reduced (2, 1);

        if (isHighlighted && isActive)
        {
            g.setColour (colours::accentPrimary.withAlpha (0.18f));
            g.fillRoundedRectangle (bounds.toFloat(), layout::controlCornerRadius);
        }

        auto colour = textColour != nullptr ? *textColour : colours::textPrimary;

        if (! isActive)
            colour = colour.withAlpha (0.4f);

        g.setColour (colour);
        g.setFont (getPopupMenuFont());

        auto textArea = bounds.reduced (10, 0);

        if (isTicked)
        {
            g.setColour (colours::accentPrimary);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f)
                               .withCentre (textArea.removeFromLeft (12).toFloat().getCentre()));
            g.setColour (colour);
        }
        else
        {
            textArea.removeFromLeft (12);
        }

        if (hasSubMenu)
        {
            const auto arrow = textArea.removeFromRight (14).toFloat().reduced (3.0f);
            juce::Path path;
            path.addTriangle (arrow.getX(), arrow.getY(),
                              arrow.getX(), arrow.getBottom(),
                              arrow.getRight(), arrow.getCentreY());
            g.fillPath (path);
        }
        else if (shortcutKeyText.isNotEmpty())
        {
            const auto shortcutArea = textArea.removeFromRight (70);
            g.setColour (colours::textMuted);
            g.setFont (monoFont (11.0f));
            g.drawText (shortcutKeyText, shortcutArea, juce::Justification::centredRight, true);
            g.setColour (colour);
            g.setFont (getPopupMenuFont());
        }

        g.drawText (text, textArea, juce::Justification::centredLeft, true);
    }

    juce::Font TextureLookAndFeel::getPopupMenuFont()
    {
        return interfaceFont (13.0f);
    }

    void TextureLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                           int buttonX, int buttonY, int buttonW, int buttonH,
                                           juce::ComboBox& box)
    {
        juce::ignoreUnused (isButtonDown, buttonX, buttonY, buttonW, buttonH);

        const juce::Rectangle<float> bounds (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));

        g.setColour (box.isEnabled() ? colours::panel : colours::panel.withAlpha (0.5f));
        g.fillRoundedRectangle (bounds.reduced (0.5f), layout::panelCornerRadius);

        g.setColour (box.isEnabled() ? colours::panelEdge : colours::panelEdge.withAlpha (0.5f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), layout::panelCornerRadius, 1.0f);

        const auto arrowArea = juce::Rectangle<float> (static_cast<float> (width) - 18.0f,
                                                       static_cast<float> (height) * 0.5f - 3.0f,
                                                       10.0f, 6.0f);

        juce::Path arrow;
        arrow.addTriangle (arrowArea.getX(), arrowArea.getY(),
                           arrowArea.getRight(), arrowArea.getY(),
                           arrowArea.getCentreX(), arrowArea.getBottom());

        g.setColour (box.isEnabled() ? colours::textMuted : colours::textMuted.withAlpha (0.5f));
        g.fillPath (arrow);
    }

    juce::Font TextureLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        juce::ignoreUnused (box);
        return interfaceFont (13.0f);
    }

    juce::Font TextureLookAndFeel::getLabelFont (juce::Label& label)
    {
        return interfaceFont (juce::jmax (10.0f, label.getFont().getHeight()));
    }

    void TextureLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const juce::Rectangle<float> bounds (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));

        // Dark pill with muted text (theme.md).
        g.setColour (colours::panel);
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.35f);

        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.35f, 1.0f);

        juce::AttributedString attributed;
        attributed.setJustification (juce::Justification::centred);
        attributed.append (text, interfaceFont (12.0f), colours::textMuted);
        attributed.draw (g, bounds.reduced (8.0f, 4.0f));
    }

    juce::Rectangle<int> TextureLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                               juce::Point<int> screenPos,
                                                               juce::Rectangle<int> parentArea)
    {
        juce::AttributedString attributed;
        attributed.setJustification (juce::Justification::centred);
        attributed.append (tipText, interfaceFont (12.0f));

        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (attributed, 320.0f);

        const auto width = static_cast<int> (layout.getWidth()) + 20;
        const auto height = static_cast<int> (layout.getHeight()) + 12;

        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (width + 12)
                                                                           : screenPos.x + 12,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (height + 6)
                                                                           : screenPos.y + 6,
                                     width, height)
            .constrainedWithin (parentArea);
    }

    void TextureLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& scrollbar,
                                            int x, int y, int width, int height,
                                            bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                            bool isMouseOver, bool isMouseDown)
    {
        juce::ignoreUnused (scrollbar);

        juce::Rectangle<int> thumb;

        if (isScrollbarVertical)
            thumb = { x + width / 4, thumbStartPosition, juce::jmax (2, width / 2), thumbSize };
        else
            thumb = { thumbStartPosition, y + height / 4, thumbSize, juce::jmax (2, height / 2) };

        auto colour = colours::panelEdge.brighter (0.4f);

        if (isMouseDown)
            colour = colours::accentPrimary;
        else if (isMouseOver)
            colour = colour.brighter (0.25f);

        g.setColour (colour);
        g.fillRoundedRectangle (thumb.toFloat(), layout::controlCornerRadius);
    }
}
