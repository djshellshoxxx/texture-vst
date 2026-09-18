#include "Widgets.h"

#include "../PluginProcessor.h"

#include <cmath>

namespace texture
{
    namespace
    {
        /** Pixels of vertical travel for the full range of a knob. */
        constexpr int normalDragSensitivity = 220;
        constexpr int fineDragSensitivity = 2200;        // Shift: 10x finer
        constexpr int ultraFineDragSensitivity = 22000;  // Ctrl/Cmd: 100x finer

        double& clipboardSlot()
        {
            static double value = 0.0;
            return value;
        }

        bool& clipboardHasValue()
        {
            static bool hasValue = false;
            return hasValue;
        }

        /** Builds the shared right-click menu entries for a parameter. */
        void addCommonItems (juce::PopupMenu& menu,
                             TextureAudioProcessor& processor,
                             const juce::String& parameterID,
                             bool includeValueItems)
        {
            if (includeValueItems)
            {
                menu.addItem (1, "Reset to default");
                menu.addItem (2, "Enter value...");
                menu.addItem (3, "Copy value");
                menu.addItem (4, "Paste value", ControlContextMenu::hasClipboardValue());
                menu.addSeparator();
            }
            else
            {
                menu.addItem (1, "Reset to default");
                menu.addSeparator();
            }

            const auto mappedCc = processor.getMidiLearn().getControllerForParameter (parameterID);

            if (mappedCc >= 0)
            {
                menu.addItem (5, "Re-learn MIDI CC (now CC" + juce::String (mappedCc) + ")");
                menu.addItem (6, "Clear MIDI mapping");
            }
            else
            {
                menu.addItem (5, "Learn MIDI CC...");
            }
        }
    }

    // =========================================================================
    //  ControlContextMenu
    // =========================================================================

    void ControlContextMenu::setClipboardValue (double value)
    {
        clipboardSlot() = value;
        clipboardHasValue() = true;
    }

    double ControlContextMenu::getClipboardValue()
    {
        return clipboardSlot();
    }

    bool ControlContextMenu::hasClipboardValue()
    {
        return clipboardHasValue();
    }

    void ControlContextMenu::showFor (juce::Slider& slider,
                                      TextureAudioProcessor& processor,
                                      const juce::String& parameterID,
                                      std::function<void()> onChanged)
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&slider.getLookAndFeel());
        addCommonItems (menu, processor, parameterID, true);

        auto* sliderPointer = &slider;
        juce::Component::SafePointer<juce::Slider> safeSlider (sliderPointer);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slider),
                            [safeSlider, &processor, parameterID, onChanged] (int result)
        {
            if (safeSlider == nullptr || result == 0)
                return;

            switch (result)
            {
                case 1:
                    safeSlider->setValue (safeSlider->getDoubleClickReturnValue(),
                                          juce::sendNotificationSync);
                    break;

                case 2:
                {
                    auto* editorWindow = new juce::AlertWindow ("Enter value",
                                                                "Value for " + params::displayNameFor (parameterID)
                                                                    + " (" + juce::String (safeSlider->getMinimum(), 3)
                                                                    + " to " + juce::String (safeSlider->getMaximum(), 3) + ")",
                                                                juce::MessageBoxIconType::NoIcon);

                    editorWindow->addTextEditor ("value", juce::String (safeSlider->getValue(), 4));
                    editorWindow->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
                    editorWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

                    editorWindow->enterModalState (true, juce::ModalCallbackFunction::create (
                        [safeSlider, editorWindow] (int choice)
                        {
                            if (choice == 1 && safeSlider != nullptr)
                            {
                                const auto text = editorWindow->getTextEditorContents ("value");
                                safeSlider->setValue (text.getDoubleValue(), juce::sendNotificationSync);
                            }

                            delete editorWindow;
                        }), false);
                    break;
                }

                case 3:
                    setClipboardValue (safeSlider->getValue());
                    break;

                case 4:
                    if (hasClipboardValue())
                        safeSlider->setValue (getClipboardValue(), juce::sendNotificationSync);
                    break;

                case 5:
                    processor.getMidiLearn().startLearning (parameterID);
                    processor.postToast ("Move a MIDI controller to map "
                                             + params::displayNameFor (parameterID) + ".",
                                         SampleLoader::Severity::info);
                    break;

                case 6:
                    processor.getMidiLearn().clearMappingFor (parameterID);
                    processor.postToast ("MIDI mapping cleared.", SampleLoader::Severity::info);
                    break;

                default:
                    break;
            }

            if (onChanged)
                onChanged();
        });
    }

    void ControlContextMenu::showFor (juce::Button& button,
                                      TextureAudioProcessor& processor,
                                      const juce::String& parameterID,
                                      std::function<void()> onChanged)
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&button.getLookAndFeel());
        addCommonItems (menu, processor, parameterID, false);

        juce::Component::SafePointer<juce::Button> safeButton (&button);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button),
                            [safeButton, &processor, parameterID, onChanged] (int result)
        {
            if (safeButton == nullptr || result == 0)
                return;

            switch (result)
            {
                case 1:
                    safeButton->setToggleState (params::defaultValueFor (parameterID) > 0.5f,
                                                juce::sendNotificationSync);
                    break;

                case 5:
                    processor.getMidiLearn().startLearning (parameterID);
                    processor.postToast ("Move a MIDI controller to map "
                                             + params::displayNameFor (parameterID) + ".",
                                         SampleLoader::Severity::info);
                    break;

                case 6:
                    processor.getMidiLearn().clearMappingFor (parameterID);
                    processor.postToast ("MIDI mapping cleared.", SampleLoader::Severity::info);
                    break;

                default:
                    break;
            }

            if (onChanged)
                onChanged();
        });
    }

    // =========================================================================
    //  ParameterKnob
    // =========================================================================

    ParameterKnob::ParameterKnob (TextureAudioProcessor& processorToUse,
                                  const juce::String& parameterIDToUse,
                                  const juce::String& labelTextToUse,
                                  const juce::String& tooltipText)
        : processor (processorToUse),
          parameterID (parameterIDToUse),
          labelText (labelTextToUse)
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters (TextureLookAndFeel::rotaryStartAngle,
                             TextureLookAndFeel::rotaryEndAngle,
                             true);
        setMouseDragSensitivity (normalDragSensitivity);
        setTooltip (tooltipText);
        setWantsKeyboardFocus (true);

        setDoubleClickReturnValue (true, params::defaultValueFor (parameterID));

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getValueTreeState(), parameterID, *this);

        setTitle (params::displayNameFor (parameterID));
        setDescription (tooltipText);
        setAccessible (true);
    }

    ParameterKnob::~ParameterKnob()
    {
        attachment.reset();
    }

    void ParameterKnob::setShowsLabel (bool shouldShow)
    {
        showsLabel = shouldShow;
        repaint();
    }

    void ParameterKnob::setCoarseSteps (juce::Array<double> steps)
    {
        coarseSteps = std::move (steps);
    }

    void ParameterKnob::updateSensitivity (const juce::MouseEvent& event)
    {
        if (event.mods.isCommandDown())
            setMouseDragSensitivity (ultraFineDragSensitivity);
        else if (event.mods.isShiftDown())
            setMouseDragSensitivity (fineDragSensitivity);
        else
            setMouseDragSensitivity (normalDragSensitivity);
    }

    void ParameterKnob::mouseDown (const juce::MouseEvent& event)
    {
        if (event.mods.isPopupMenu())
        {
            ControlContextMenu::showFor (*this, processor, parameterID, [this] { repaint(); });
            return;
        }

        updateSensitivity (event);
        juce::Slider::mouseDown (event);
    }

    void ParameterKnob::mouseUp (const juce::MouseEvent& event)
    {
        juce::Slider::mouseUp (event);

        // Ctrl/Cmd+click (without dragging) steps through the coarse positions.
        if (! coarseSteps.isEmpty()
            && event.mods.isCommandDown()
            && event.getDistanceFromDragStart() < 3)
        {
            const auto current = getValue();
            int nextIndex = 0;

            for (int i = 0; i < coarseSteps.size(); ++i)
            {
                if (coarseSteps[i] > current + 1.0e-4)
                {
                    nextIndex = i;
                    break;
                }
            }

            setValue (coarseSteps[nextIndex], juce::sendNotificationSync);
        }

        setMouseDragSensitivity (normalDragSensitivity);
    }

    void ParameterKnob::resized()
    {
        juce::Slider::resized();
    }

    void ParameterKnob::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds();

        const auto labelHeight = showsLabel ? 14 : 0;
        const auto knobArea = bounds.withTrimmedBottom (labelHeight);

        getLookAndFeel().drawRotarySlider (g,
                                           knobArea.getX(), knobArea.getY(),
                                           knobArea.getWidth(), knobArea.getHeight(),
                                           static_cast<float> (valueToProportionOfLength (getValue())),
                                           TextureLookAndFeel::rotaryStartAngle,
                                           TextureLookAndFeel::rotaryEndAngle,
                                           *this);

        if (! showsLabel)
            return;

        // The readout replaces the label while the knob is being touched (theme.md).
        const auto showValue = isMouseOverOrDragging();

        g.setColour (showValue ? colours::textPrimary : colours::textMuted);
        g.setFont (showValue ? TextureLookAndFeel::monoFont (11.0f) : TextureLookAndFeel::labelFont());
        g.drawText (showValue ? getTextFromValue (getValue()) : labelText,
                    bounds.removeFromBottom (labelHeight),
                    juce::Justification::centred,
                    false);
    }

    // =========================================================================
    //  ParameterToggle
    // =========================================================================

    ParameterToggle::ParameterToggle (TextureAudioProcessor& processorToUse,
                                      const juce::String& parameterIDToUse,
                                      const juce::String& buttonText,
                                      const juce::String& tooltipText)
        : processor (processorToUse),
          parameterID (parameterIDToUse)
    {
        setButtonText (buttonText);
        setTooltip (tooltipText);
        setWantsKeyboardFocus (true);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.getValueTreeState(), parameterID, *this);

        setTitle (params::displayNameFor (parameterID));
        setDescription (tooltipText);
        setAccessible (true);
    }

    ParameterToggle::~ParameterToggle()
    {
        attachment.reset();
    }

    void ParameterToggle::mouseDown (const juce::MouseEvent& event)
    {
        if (event.mods.isPopupMenu())
        {
            ControlContextMenu::showFor (*this, processor, parameterID, [this] { repaint(); });
            return;
        }

        juce::ToggleButton::mouseDown (event);
    }

    // =========================================================================
    //  LevelLed
    // =========================================================================

    LevelLed::LevelLed()
    {
        setInterceptsMouseClicks (false, false);
        setTooltip ("Output level. White at 0 dBFS, red when the output clips.");
    }

    void LevelLed::setLevel (float peak)
    {
        const auto clamped = juce::jmax (0.0f, peak);
        const auto over = clamped >= 1.0f;

        if (juce::approximatelyEqual (clamped, level) && over == isOver)
            return;

        level = clamped;
        isOver = over;
        repaint();
    }

    void LevelLed::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const auto colour = colours::levelLed (level);

        // A soft halo sells the glow without needing a blur.
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (bounds);

        g.setColour (colour);
        g.fillEllipse (bounds.reduced (bounds.getWidth() * 0.22f));

        g.setColour (colours::panelEdge);
        g.drawEllipse (bounds.reduced (0.5f), 1.0f);
    }

    // =========================================================================
    //  DataStreamView
    // =========================================================================

    DataStreamView::DataStreamView()
    {
        setInterceptsMouseClicks (false, false);
    }

    bool DataStreamView::push (const juce::String& line)
    {
        // The stream stops when there is nothing new to say (theme.md).
        if (line == lastLine)
            return false;

        lastLine = line;
        lines.push_back (line);

        while (static_cast<int> (lines.size()) > visibleLines)
            lines.pop_front();

        repaint();
        return true;
    }

    void DataStreamView::clear()
    {
        lines.clear();
        lastLine.clear();
        repaint();
    }

    void DataStreamView::paint (juce::Graphics& g)
    {
        if (lines.empty())
            return;

        const auto bounds = getLocalBounds().toFloat();
        const auto lineHeight = bounds.getHeight() / static_cast<float> (visibleLines);

        g.setFont (TextureLookAndFeel::monoFont (juce::jlimit (7.0f, 10.0f, lineHeight - 2.0f)));

        const auto count = static_cast<int> (lines.size());

        for (int i = 0; i < count; ++i)
        {
            // Top and bottom two lines fade away so the stream never competes with
            // the controls around it.
            const auto positionFromTop = i;
            const auto positionFromBottom = count - 1 - i;
            const auto edgeDistance = juce::jmin (positionFromTop, positionFromBottom);

            float alpha = 0.34f;

            if (edgeDistance == 0)
                alpha = 0.04f;
            else if (edgeDistance == 1)
                alpha = 0.16f;

            g.setColour (colours::dataStream.withAlpha (alpha));
            g.drawText (lines[static_cast<size_t> (i)],
                        bounds.withY (bounds.getY() + static_cast<float> (i) * lineHeight)
                              .withHeight (lineHeight),
                        juce::Justification::centredLeft,
                        false);
        }
    }

    // =========================================================================
    //  ToastBar
    // =========================================================================

    ToastBar::ToastBar()
    {
        setInterceptsMouseClicks (false, false);
    }

    ToastBar::~ToastBar()
    {
        stopTimer();
    }

    void ToastBar::show (const juce::String& message, Severity newSeverity)
    {
        if (message.isEmpty())
            return;

        // A new toast replaces the current one immediately: they never stack.
        text = message;
        severity = newSeverity;
        phase = Phase::fadingIn;
        elapsedMs = 0;
        opacity = 0.0f;

        startTimer (tickMs);
        repaint();
    }

    void ToastBar::dismiss()
    {
        if (phase == Phase::idle)
            return;

        phase = Phase::fadingOut;
        elapsedMs = 0;
    }

    void ToastBar::timerCallback()
    {
        elapsedMs += tickMs;

        switch (phase)
        {
            case Phase::fadingIn:
                opacity = juce::jlimit (0.0f, 1.0f, static_cast<float> (elapsedMs) / fadeInMs);

                if (elapsedMs >= fadeInMs)
                {
                    opacity = 1.0f;
                    phase = Phase::holding;
                    elapsedMs = 0;
                }
                break;

            case Phase::holding:
                if (elapsedMs >= holdMs)
                {
                    phase = Phase::fadingOut;
                    elapsedMs = 0;
                }
                break;

            case Phase::fadingOut:
                opacity = juce::jlimit (0.0f, 1.0f, 1.0f - static_cast<float> (elapsedMs) / fadeOutMs);

                if (elapsedMs >= fadeOutMs)
                {
                    opacity = 0.0f;
                    phase = Phase::idle;
                    text.clear();
                    stopTimer();
                }
                break;

            case Phase::idle:
                stopTimer();
                break;
        }

        repaint();
    }

    void ToastBar::paint (juce::Graphics& g)
    {
        if (phase == Phase::idle || text.isEmpty() || opacity <= 0.0f)
            return;

        auto accent = colours::accentPrimary;

        if (severity == Severity::warning)
            accent = colours::warning;
        else if (severity == Severity::error)
            accent = colours::error;

        const auto bounds = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (colours::panel.withAlpha (0.92f * opacity));
        g.fillRoundedRectangle (bounds, layout::panelCornerRadius);

        g.setColour (accent.withAlpha (0.55f * opacity));
        g.drawRoundedRectangle (bounds.reduced (0.5f), layout::panelCornerRadius, 1.0f);

        auto textArea = bounds.reduced (8.0f, 0.0f);

        // A small dot carries the severity so the text stays plain.
        const auto dot = textArea.removeFromLeft (12.0f);
        g.setColour (accent.withAlpha (opacity));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot.getCentre()));

        g.setColour (colours::textPrimary.withAlpha (opacity));
        g.setFont (TextureLookAndFeel::interfaceFont (12.0f));
        g.drawText (text, textArea.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    // =========================================================================
    //  SectionHeader
    // =========================================================================

    SectionHeader::SectionHeader (juce::String titleText, juce::Colour accentColour)
        : title (std::move (titleText)),
          accent (accentColour)
    {
        setInterceptsMouseClicks (false, false);
    }

    void SectionHeader::setTitle (const juce::String& newTitle)
    {
        title = newTitle;
        repaint();
    }

    void SectionHeader::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds();

        // 2 px accent bar, 12 px tall, to the left of the header (theme.md).
        const auto bar = bounds.removeFromLeft (2).withSizeKeepingCentre (2, 12);
        g.setColour (accent);
        g.fillRect (bar);

        bounds.removeFromLeft (6);

        g.setColour (colours::textPrimary);
        g.setFont (TextureLookAndFeel::labelFont());
        g.drawText (title.toUpperCase(), bounds, juce::Justification::centredLeft, false);
    }

    // =========================================================================
    //  TierBadge
    // =========================================================================

    TierBadge::TierBadge()
        : juce::Button ("Tier")
    {
        setTooltip ("Your licence tier. Click to open the licence panel.");
    }

    void TierBadge::setPro (bool shouldShowPro)
    {
        if (pro == shouldShowPro)
            return;

        pro = shouldShowPro;
        setTooltip (pro ? "TEXTURE Pro is active. Click for licence details."
                        : "Free tier. Click to unlock Pro features.");
        repaint();
    }

    void TierBadge::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                                 bool shouldDrawButtonAsDown)
    {
        juce::ignoreUnused (shouldDrawButtonAsDown);

        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const auto accent = pro ? colours::accentPro : colours::textMuted;

        g.setColour (accent.withAlpha (pro ? 0.18f : 0.08f));
        g.fillRoundedRectangle (bounds, layout::controlCornerRadius);

        g.setColour (shouldDrawButtonAsHighlighted ? accent.brighter (0.3f) : accent);
        g.drawRoundedRectangle (bounds, layout::controlCornerRadius, 1.0f);

        g.setFont (TextureLookAndFeel::labelFont());
        g.drawText (pro ? "PRO" : "FREE", bounds, juce::Justification::centred, false);
    }

    // =========================================================================
    //  IconButton
    // =========================================================================

    IconButton::IconButton (Icon iconToDraw, const juce::String& tooltipText)
        : juce::Button ("Icon"),
          icon (iconToDraw)
    {
        setTooltip (tooltipText);
        setTitle (tooltipText);
        setAccessible (true);
    }

    void IconButton::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                                  bool shouldDrawButtonAsDown)
    {
        auto colour = colours::textMuted;

        if (! isEnabled())
            colour = colour.withAlpha (0.4f);
        else if (shouldDrawButtonAsDown)
            colour = colours::accentPrimary;
        else if (shouldDrawButtonAsHighlighted)
            colour = colours::textPrimary;

        drawIcon (g, icon, getLocalBounds().toFloat().reduced (3.0f), colour);
    }

    void IconButton::drawIcon (juce::Graphics& g, Icon iconToDraw, juce::Rectangle<float> bounds,
                               juce::Colour colour)
    {
        g.setColour (colour);

        const auto centre = bounds.getCentre();
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

        switch (iconToDraw)
        {
            case Icon::gear:
            {
                juce::Path teeth;

                for (int i = 0; i < 8; ++i)
                {
                    const auto angle = static_cast<float> (i) * juce::MathConstants<float>::twoPi / 8.0f;
                    juce::Rectangle<float> tooth (2.4f, radius * 0.55f);
                    tooth.setCentre (centre.x + std::sin (angle) * radius * 0.72f,
                                     centre.y - std::cos (angle) * radius * 0.72f);

                    juce::Path single;
                    single.addRoundedRectangle (tooth, 1.0f);
                    single.applyTransform (juce::AffineTransform::rotation (angle, tooth.getCentreX(),
                                                                            tooth.getCentreY()));
                    teeth.addPath (single);
                }

                g.fillPath (teeth);
                g.drawEllipse (juce::Rectangle<float> (radius * 1.1f, radius * 1.1f).withCentre (centre), 1.6f);
                break;
            }

            case Icon::help:
            {
                g.drawEllipse (bounds.reduced (1.0f), 1.4f);
                g.setFont (TextureLookAndFeel::interfaceFont (radius * 1.3f, true));
                g.drawText ("?", bounds, juce::Justification::centred, false);
                break;
            }

            case Icon::dice:
            {
                g.drawRoundedRectangle (bounds.reduced (1.5f), 2.5f, 1.4f);

                const auto pip = 1.9f;
                const auto offset = radius * 0.42f;

                for (const auto& point : { juce::Point<float> (centre.x - offset, centre.y - offset),
                                           juce::Point<float> (centre.x + offset, centre.y - offset),
                                           juce::Point<float> (centre.x, centre.y),
                                           juce::Point<float> (centre.x - offset, centre.y + offset),
                                           juce::Point<float> (centre.x + offset, centre.y + offset) })
                {
                    g.fillEllipse (juce::Rectangle<float> (pip * 2.0f, pip * 2.0f).withCentre (point));
                }
                break;
            }

            case Icon::reset:
            {
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, radius * 0.7f, radius * 0.7f, 0.0f,
                                   juce::MathConstants<float>::pi * 0.35f,
                                   juce::MathConstants<float>::pi * 1.85f, true);
                g.strokePath (arc, juce::PathStrokeType (1.6f));

                juce::Path head;
                const auto tip = juce::Point<float> (centre.x + std::sin (juce::MathConstants<float>::pi * 0.35f) * radius * 0.7f,
                                                     centre.y - std::cos (juce::MathConstants<float>::pi * 0.35f) * radius * 0.7f);
                head.addTriangle (tip.x - 3.0f, tip.y - 3.0f, tip.x + 3.5f, tip.y - 1.0f, tip.x - 1.0f, tip.y + 3.5f);
                g.fillPath (head);
                break;
            }

            case Icon::chevronDown:
            {
                juce::Path chevron;
                chevron.startNewSubPath (centre.x - radius * 0.5f, centre.y - radius * 0.22f);
                chevron.lineTo (centre.x, centre.y + radius * 0.3f);
                chevron.lineTo (centre.x + radius * 0.5f, centre.y - radius * 0.22f);
                g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
                break;
            }

            case Icon::folder:
            {
                auto body = bounds.reduced (1.0f, 2.5f);
                juce::Path folder;
                folder.addRoundedRectangle (body.withTrimmedTop (body.getHeight() * 0.22f), 1.5f);
                folder.addRoundedRectangle (body.removeFromTop (body.getHeight() * 0.34f)
                                                 .withWidth (body.getWidth() * 0.45f), 1.0f);
                g.strokePath (folder, juce::PathStrokeType (1.4f));
                break;
            }

            case Icon::save:
            {
                g.drawRoundedRectangle (bounds.reduced (1.5f), 1.5f, 1.4f);
                auto shutter = bounds.reduced (bounds.getWidth() * 0.26f, 1.5f);
                g.fillRect (shutter.removeFromTop (shutter.getHeight() * 0.34f));
                g.drawRect (shutter.removeFromBottom (shutter.getHeight() * 0.55f), 1.2f);
                break;
            }

            case Icon::compare:
            {
                g.setFont (TextureLookAndFeel::labelFont());
                g.drawText ("A/B", bounds, juce::Justification::centred, false);
                break;
            }
        }
    }
}
