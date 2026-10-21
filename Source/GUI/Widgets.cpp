#include "Widgets.h"

#include "../PluginProcessor.h"
#include "../Utility/MotionPreferences.h"
#include "../Utility/ControlValueParser.h"
#include "../Utility/ControlValueClipboard.h"

#include <cmath>

namespace texture
{
    namespace
    {
        /** Pixels of vertical travel for the full range of a knob (theme.md:
            plain drag is fine, Shift is coarse, Ctrl/Cmd is ultra-fine). */
        constexpr int normalDragSensitivity = 300;
        constexpr int coarseDragSensitivity = 100;       // Shift: 3x faster
        constexpr int ultraFineDragSensitivity = 3000;   // Ctrl/Cmd: 10x finer

        /** theme.md: "All value changes animate over 80ms ease-out." */
        constexpr double valueAnimationMs = 80.0;

        int sensitivityFor (const juce::ModifierKeys& mods)
        {
            if (mods.isCommandDown())
                return ultraFineDragSensitivity;

            return mods.isShiftDown() ? coarseDragSensitivity : normalDragSensitivity;
        }

        /** Shared knob setup: house drag feel, and no JUCE velocity-mode swap on Ctrl. */
        void configureRotary (juce::Slider& slider)
        {
            slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters (TextureLookAndFeel::rotaryStartAngle,
                                        TextureLookAndFeel::rotaryEndAngle, true);
            slider.setMouseDragSensitivity (normalDragSensitivity);
            slider.setVelocityModeParameters (1.0, 1, 0.0, false);
            slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            slider.setRepaintsOnMouseActivity (true);
        }

        void showValueEntry (juce::Slider& slider, const juce::String& name)
        {
            juce::Component::SafePointer<juce::Slider> safeSlider (&slider);

            auto* editorWindow = new juce::AlertWindow ("Enter value",
                                                        "Value for " + name
                                                            + " (" + juce::String (slider.getMinimum(), 3)
                                                            + " to " + juce::String (slider.getMaximum(), 3) + ")",
                                                        juce::MessageBoxIconType::NoIcon);

            editorWindow->addTextEditor ("value", juce::String (slider.getValue(), 4));
            editorWindow->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
            editorWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

            editorWindow->enterModalState (true, juce::ModalCallbackFunction::create (
                [safeSlider, editorWindow] (int choice)
                {
                    std::unique_ptr<juce::AlertWindow> owned (editorWindow);
                    if (choice != 1 || safeSlider == nullptr)
                        return;

                    const auto text = editorWindow->getTextEditorContents ("value");
                    const auto parsed = ControlValueParser::parse (text,
                                                                   safeSlider->getMinimum(),
                                                                   safeSlider->getMaximum());

                    if (! parsed.has_value())
                    {
                        juce::AlertWindow::showMessageBoxAsync (
                            juce::MessageBoxIconType::WarningIcon,
                            "Invalid value",
                            "Enter a numeric value from "
                                + juce::String (safeSlider->getMinimum(), 3)
                                + " to "
                                + juce::String (safeSlider->getMaximum(), 3)
                                + ".",
                            "OK",
                            safeSlider.getComponent());
                        return;
                    }

                    safeSlider->setValue (*parsed, juce::sendNotificationSync);
                }), false);
        }
        /** Builds the shared right-click menu entries for a parameter. */
        void addCommonItems (juce::PopupMenu& menu,
                             TextureAudioProcessor& processor,
                             const juce::String& parameterID,
                             bool includeValueItems,
                             bool canPasteValue = false)
        {
            if (includeValueItems)
            {
                menu.addItem (1, "Reset to default");
                menu.addItem (2, "Enter value...");
                menu.addItem (3, "Copy value");
                menu.addItem (4, "Paste value", canPasteValue);
                menu.addSeparator();
            }
            else
            {
                menu.addItem (1, "Reset to default");
                menu.addSeparator();
            }

            // Lock is an editing protection (F02), not a value: it sits above MIDI items.
            menu.addItem (8, processor.isFieldLocked (parameterID) ? "Unlock value"
                                                                    : "Lock value (keep out of Randomize and Mutate)");
            menu.addSeparator();

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

            if (processor.getMidiLearn().isLearning())
                menu.addItem (7, "Cancel MIDI Learn");
        }
    }

    // =========================================================================
    //  ControlContextMenu
    // =========================================================================
    void ControlContextMenu::showFor (juce::Slider& slider,
                                      TextureAudioProcessor& processor,
                                      const juce::String& parameterID,
                                      std::function<void()> onChanged)
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&slider.getLookAndFeel());
        addCommonItems (menu, processor, parameterID, true,
                        ControlValueClipboard::canPaste (parameterID,
                                                         slider.getMinimum(),
                                                         slider.getMaximum()));

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
                    showValueEntry (*safeSlider, params::displayNameFor (parameterID));
                    break;

                case 3:
                    ControlValueClipboard::copy (parameterID, safeSlider->getValue());
                    processor.postToast ("Value copied for " + params::displayNameFor (parameterID) + ".",
                                         SampleLoader::Severity::info);
                    break;

                case 4:
                {
                    const auto pasted = ControlValueClipboard::valueFor (parameterID,
                                                                         safeSlider->getMinimum(),
                                                                         safeSlider->getMaximum());

                    if (pasted.has_value())
                        safeSlider->setValue (*pasted, juce::sendNotificationSync);
                    else
                        processor.postToast ("Copied value is not compatible with this control.",
                                             SampleLoader::Severity::warning);
                    break;
                }

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

                case 7:
                    processor.getMidiLearn().cancelLearning();
                    processor.postToast ("MIDI learn cancelled.", SampleLoader::Severity::info);
                    break;

                case 8:
                    processor.setFieldLocked (parameterID, ! processor.isFieldLocked (parameterID));
                    break;

                default:
                    break;
            }

            if (onChanged)
                onChanged();
        });
    }

    void ControlContextMenu::showFor (juce::ComboBox& box,
                                      TextureAudioProcessor& processor,
                                      const juce::String& parameterID)
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&box.getLookAndFeel());
        addCommonItems (menu, processor, parameterID, false);

        juce::Component::SafePointer<juce::ComboBox> safeBox (&box);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (box),
                            [safeBox, &processor, parameterID] (int result)
        {
            if (safeBox == nullptr || result == 0)
                return;

            switch (result)
            {
                case 1:
                    safeBox->setSelectedItemIndex (juce::roundToInt (params::defaultValueFor (parameterID)),
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

                case 7:
                    processor.getMidiLearn().cancelLearning();
                    processor.postToast ("MIDI learn cancelled.", SampleLoader::Severity::info);
                    break;

                case 8:
                    processor.setFieldLocked (parameterID, ! processor.isFieldLocked (parameterID));
                    break;

                default:
                    break;
            }
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

                case 7:
                    processor.getMidiLearn().cancelLearning();
                    processor.postToast ("MIDI learn cancelled.", SampleLoader::Severity::info);
                    break;

                case 8:
                    processor.setFieldLocked (parameterID, ! processor.isFieldLocked (parameterID));
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
        configureRotary (*this);
        setTooltip (tooltipText);
        setWantsKeyboardFocus (true);

        setDoubleClickReturnValue (true, params::defaultValueFor (parameterID));

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getValueTreeState(), parameterID, *this);

        setTitle (params::displayNameFor (parameterID));
        setDescription (tooltipText);
        setAccessible (true);

        displayedProportion = animationTo = static_cast<float> (valueToProportionOfLength (getValue()));
    }

    ParameterKnob::~ParameterKnob()
    {
        stopTimer();
        attachment.reset();
    }

    void ParameterKnob::valueChanged()
    {
        const auto target = static_cast<float> (valueToProportionOfLength (getValue()));

        // The user's own gestures track the mouse exactly; everything else
        // (presets, reset, randomise, A/B, automation) eases in over 80 ms.
        if (isMouseButtonDown() || ! isShowing() || MotionPreferences::isReducedMotion())
        {
            stopTimer();
            displayedProportion = animationTo = target;
            repaint();
            return;
        }

        animationFrom = displayedProportion;
        animationTo = target;
        animationStartMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }

    void ParameterKnob::timerCallback()
    {
        const auto t = juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - animationStartMs)
                                                   / valueAnimationMs);
        const auto eased = static_cast<float> (1.0 - std::pow (1.0 - t, 3.0));

        displayedProportion = animationFrom + (animationTo - animationFrom) * eased;

        if (t >= 1.0)
        {
            displayedProportion = animationTo;
            stopTimer();
        }

        repaint();
    }

    void ParameterKnob::updateLearnIndicator()
    {
        auto& learn = processor.getMidiLearn();
        const auto learningThis = learn.isLearning() && learn.getLearningParameterID() == parameterID;

        if (learningThis || learningThis != isLearning)
            repaint();

        isLearning = learningThis;
    }

    void drawPadlock (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        const auto side = juce::jmin (area.getWidth(), area.getHeight());
        const auto body = juce::Rectangle<float> (side * 0.62f, side * 0.48f)
                              .withBottomY (area.getCentreY() + side * 0.42f)
                              .withCentre ({ area.getCentreX(), area.getCentreY() + side * 0.2f });
        const auto radius = side * 0.2f;
        const auto stroke = juce::jmax (1.1f, side * 0.09f);

        juce::Path shackle;
        shackle.addCentredArc (body.getCentreX(), body.getY(), radius, radius,
                               0.0f, juce::MathConstants<float>::pi,
                               juce::MathConstants<float>::twoPi, true);

        g.setColour (colour);
        g.strokePath (shackle, juce::PathStrokeType (stroke));
        g.fillRoundedRectangle (body, side * 0.1f);
    }

    void ParameterKnob::paintLearnRing (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (! isLearning)
            return;

        const auto phase = static_cast<float> (juce::Time::getMillisecondCounter() % 1000) / 1000.0f;
        const auto pulse = MotionPreferences::isReducedMotion()
                         ? 0.5f
                         : 0.5f + 0.5f * std::sin (phase * juce::MathConstants<float>::twoPi);
        const auto size = juce::jmin (area.getWidth(), area.getHeight());

        g.setColour (colours::accentPro.withAlpha (0.35f + 0.5f * pulse));
        g.drawEllipse (area.withSizeKeepingCentre (size, size).reduced (1.0f), 2.0f);
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
        setMouseDragSensitivity (sensitivityFor (event.mods));
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
                                           displayedProportion,
                                           TextureLookAndFeel::rotaryStartAngle,
                                           TextureLookAndFeel::rotaryEndAngle,
                                           *this);

        paintLearnRing (g, knobArea.toFloat());

        if (processor.isFieldLocked (parameterID))
        {
            const auto side = juce::jmin (knobArea.getWidth(), knobArea.getHeight()) * 0.34f;
            drawPadlock (g, juce::Rectangle<float> (static_cast<float> (knobArea.getRight()) - side,
                                                    static_cast<float> (knobArea.getY()), side, side),
                         colours::accentPrimary);
        }

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
    //  ParameterChoiceBox
    // =========================================================================

    ParameterChoiceBox::ParameterChoiceBox (TextureAudioProcessor& processorToUse,
                                            const juce::String& parameterIDToUse,
                                            const juce::StringArray& items,
                                            const juce::String& tooltipText)
        : processor (processorToUse),
          parameterID (parameterIDToUse)
    {
        addItemList (items, 1);
        setTooltip (tooltipText + " Right-click for reset and MIDI learn.");
        setTitle (params::displayNameFor (parameterID));

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.getValueTreeState(), parameterID, *this);
    }

    ParameterChoiceBox::~ParameterChoiceBox()
    {
        attachment.reset();
    }

    void ParameterChoiceBox::mouseDown (const juce::MouseEvent& event)
    {
        if (event.mods.isPopupMenu())
        {
            ControlContextMenu::showFor (*this, processor, parameterID);
            return;
        }

        juce::ComboBox::mouseDown (event);
    }

    // =========================================================================
    //  PlainKnob
    // =========================================================================

    PlainKnob::PlainKnob (juce::String labelTextToUse, juce::String suffix)
        : labelText (std::move (labelTextToUse))
    {
        configureRotary (*this);
        setTextValueSuffix (suffix);
        setWantsKeyboardFocus (true);
        setTitle (labelText);
    }

    void PlainKnob::mouseDown (const juce::MouseEvent& event)
    {
        if (event.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.setLookAndFeel (&getLookAndFeel());
            menu.addItem (1, "Reset to default");
            menu.addItem (2, "Enter value...");
            menu.addSeparator();
            menu.addItem (3, "MIDI learn: not available for the hidden effect", false, false);

            juce::Component::SafePointer<PlainKnob> safeThis (this);

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safeThis] (int result)
            {
                if (safeThis == nullptr)
                    return;

                if (result == 1)
                    safeThis->setValue (safeThis->getDoubleClickReturnValue(), juce::sendNotificationSync);
                else if (result == 2)
                    showValueEntry (*safeThis, safeThis->labelText);
            });
            return;
        }

        setMouseDragSensitivity (sensitivityFor (event.mods));
        juce::Slider::mouseDown (event);
    }

    void PlainKnob::mouseUp (const juce::MouseEvent& event)
    {
        juce::Slider::mouseUp (event);
        setMouseDragSensitivity (normalDragSensitivity);
    }

    void PlainKnob::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds();
        const auto knobArea = bounds.withTrimmedBottom (12);

        getLookAndFeel().drawRotarySlider (g, knobArea.getX(), knobArea.getY(),
                                           knobArea.getWidth(), knobArea.getHeight(),
                                           static_cast<float> (valueToProportionOfLength (getValue())),
                                           TextureLookAndFeel::rotaryStartAngle,
                                           TextureLookAndFeel::rotaryEndAngle, *this);

        const auto showValue = isMouseOverOrDragging();
        g.setColour (showValue ? colours::textPrimary : colours::textMuted);
        g.setFont (showValue ? TextureLookAndFeel::monoFont (10.0f) : TextureLookAndFeel::labelFont());
        g.drawText (showValue ? getTextFromValue (getValue()) : labelText,
                    bounds.removeFromBottom (12), juce::Justification::centred, false);
    }

    // =========================================================================
    //  LevelMeter
    // =========================================================================

    LevelMeter::LevelMeter()
    {
        setTooltip ("Output level, left and right. The thin line holds the peak for 1.5 s. "
                    "Click to reset the peak readout.");
        setTitle ("Output meter");
    }

    float LevelMeter::dbToX (float db, float width) const noexcept
    {
        return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, 0.0f, width);
    }

    void LevelMeter::pushLevels (float left, float right, double nowMs)
    {
        const auto elapsed = lastUpdateMs > 0.0 ? juce::jlimit (0.0, 200.0, nowMs - lastUpdateMs) : 0.0;
        lastUpdateMs = nowMs;

        auto changed = false;
        const float peaks[] { left, right };

        for (size_t i = 0; i < channels.size(); ++i)
        {
            auto& channel = channels[i];
            const auto db = juce::Decibels::gainToDecibels (peaks[i], minDb);

            // Instant attack, 30 dB/s release on the bar itself.
            const auto released = channel.levelDb - static_cast<float> (30.0 * elapsed / 1000.0);
            const auto newLevel = juce::jmax (db, released, minDb);

            const auto oldHold = channel.holdDb;

            if (db >= channel.holdDb)
            {
                channel.holdDb = db;
                channel.holdSinceMs = nowMs;
            }
            else if (nowMs - channel.holdSinceMs > 1500.0)
            {
                // Hold 1.5 s, then fall at 20 dB/s (theme.md).
                channel.holdDb = juce::jmax (newLevel, channel.holdDb - static_cast<float> (20.0 * elapsed / 1000.0));
            }

            changed = changed || std::abs (newLevel - channel.levelDb) > 0.05f
                              || std::abs (oldHold - channel.holdDb) > 0.05f;
            channel.levelDb = newLevel;

            if (db > peakReadoutDb)
            {
                peakReadoutDb = db;
                changed = true;
            }
        }

        if (changed)
            repaint();
    }

    void LevelMeter::mouseDown (const juce::MouseEvent&)
    {
        peakReadoutDb = minDb;

        for (auto& channel : channels)
            channel.holdDb = channel.levelDb;

        repaint();
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        // Numeric peak in mono, top right.
        auto top = bounds.removeFromTop (12.0f);
        g.setFont (TextureLookAndFeel::monoFont (10.0f));
        g.setColour (peakReadoutDb >= 0.0f ? colours::error : colours::textMuted);
        g.drawText (peakReadoutDb <= minDb ? juce::String ("-inf") : juce::String (peakReadoutDb, 1) + " dB",
                    top, juce::Justification::centredRight, false);

        g.setColour (colours::textMuted);
        g.setFont (TextureLookAndFeel::labelFont());
        g.drawText ("LEVEL", top, juce::Justification::centredLeft, false);

        auto scale = bounds.removeFromBottom (10.0f);
        bounds.removeFromTop (3.0f);

        const auto width = bounds.getWidth();
        const auto barHeight = juce::jmax (3.0f, (bounds.getHeight() - 2.0f) * 0.5f);

        juce::ColourGradient gradient (colours::accentPro, bounds.getX(), 0.0f,
                                       colours::error, bounds.getX() + width, 0.0f, false);
        gradient.addColour (dbToX (-18.0f, width) / width, juce::Colour (0xffe8532a));
        gradient.addColour (dbToX (-6.0f, width) / width, colours::warning);
        gradient.addColour (dbToX (0.0f, width) / width, colours::error);

        for (size_t i = 0; i < channels.size(); ++i)
        {
            const auto bar = juce::Rectangle<float> (bounds.getX(),
                                                     bounds.getY() + static_cast<float> (i) * (barHeight + 2.0f),
                                                     width, barHeight);

            g.setColour (colours::panelEdge.withAlpha (0.6f));
            g.fillRoundedRectangle (bar, 1.0f);

            const auto fill = dbToX (channels[i].levelDb, width);

            if (fill > 0.5f)
            {
                g.setGradientFill (gradient);
                g.fillRoundedRectangle (bar.withWidth (fill), 1.0f);
            }

            if (channels[i].holdDb > minDb)
            {
                g.setColour (channels[i].holdDb >= 0.0f ? colours::error : colours::textPrimary);
                g.fillRect (bar.getX() + dbToX (channels[i].holdDb, width) - 0.5f, bar.getY(), 1.0f, bar.getHeight());
            }
        }

        // Ticks every 12 dB, plus 0 dB.
        g.setFont (TextureLookAndFeel::monoFont (8.0f));

        for (const auto db : { -48.0f, -36.0f, -24.0f, -12.0f, 0.0f })
        {
            const auto x = bounds.getX() + dbToX (db, width);
            g.setColour (colours::textMuted.withAlpha (0.5f));
            g.fillRect (x, scale.getY(), 1.0f, 2.0f);
            g.drawText (juce::String (juce::roundToInt (db)),
                        juce::Rectangle<float> (x - 12.0f, scale.getY() + 1.0f, 24.0f, 9.0f),
                        juce::Justification::centred, false);
        }
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
        scrollOffset = MotionPreferences::isReducedMotion()
                     ? 0.0f
                     : juce::jmin (1.0f, scrollOffset + 1.0f);

        while (static_cast<int> (lines.size()) > visibleLines)
            lines.pop_front();

        repaint();
        return true;
    }

    void DataStreamView::tick()
    {
        if (MotionPreferences::isReducedMotion())
        {
            if (! juce::exactlyEqual (scrollOffset, 0.0f))
            {
                scrollOffset = 0.0f;
                repaint();
            }

            return;
        }

        if (scrollOffset <= 0.0f)
            return;

        scrollOffset = scrollOffset < 0.02f ? 0.0f : scrollOffset * 0.72f;
        repaint();
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
                        bounds.withY (bounds.getY() + (static_cast<float> (i) + scrollOffset) * lineHeight)
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
        elapsedMs = 0;

        if (MotionPreferences::isReducedMotion())
        {
            phase = Phase::holding;
            opacity = 1.0f;
        }
        else
        {
            phase = Phase::fadingIn;
            opacity = 0.0f;
        }

        startTimer (tickMs);
        repaint();
    }

    void ToastBar::dismiss()
    {
        if (phase == Phase::idle)
            return;

        if (MotionPreferences::isReducedMotion())
        {
            phase = Phase::idle;
            opacity = 0.0f;
            text.clear();
            stopTimer();
            repaint();
            return;
        }

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

        if (lockedCount > 0)
        {
            const auto glyph = bounds.removeFromRight (12).toFloat().withSizeKeepingCentre (9.0f, 10.0f);
            drawPadlock (g, glyph, colours::accentPrimary);
            bounds.removeFromRight (4);

            g.setColour (colours::textMuted);
            g.setFont (TextureLookAndFeel::labelFont());
            g.drawText (juce::String (lockedCount) + " of " + juce::String (totalCount) + " locked",
                        bounds, juce::Justification::centredRight, false);
        }

        g.setColour (colours::textPrimary);
        g.setFont (TextureLookAndFeel::labelFont());
        g.drawText (title.toUpperCase(), bounds, juce::Justification::centredLeft, false);
    }

    void SectionHeader::setLockCount (int locked, int total)
    {
        if (locked == lockedCount && total == totalCount)
            return;

        lockedCount = locked;
        totalCount = total;
        repaint();
    }

    void SectionHeader::mouseDown (const juce::MouseEvent& event)
    {
        if (event.mods.isPopupMenu() && onMenuRequested != nullptr)
            onMenuRequested();
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

        const auto flashing = ! MotionPreferences::isReducedMotion()
                           && juce::Time::getMillisecondCounter() - flashStartMs < 100;

        if (! isEnabled())
            colour = colour.withAlpha (0.4f);
        else if (shouldDrawButtonAsDown || flashing)
            colour = colours::accentPrimary;
        else if (shouldDrawButtonAsHighlighted)
            colour = colours::textPrimary;

        drawIcon (g, icon, getLocalBounds().toFloat().reduced (3.0f), colour);
    }

    void IconButton::clicked()
    {
        // 100 ms accent flash after every press (theme.md, momentary buttons).
        if (MotionPreferences::isReducedMotion())
            return;

        flashStartMs = juce::Time::getMillisecondCounter();
        repaint();

        juce::Component::SafePointer<IconButton> safeThis (this);
        juce::Timer::callAfterDelay (110, [safeThis] { if (safeThis != nullptr) safeThis->repaint(); });
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

            case Icon::chevronLeft:
            case Icon::lock:
                drawPadlock (g, bounds.reduced (radius * 0.2f), colour);
                break;

            case Icon::chevronRight:
            {
                const auto direction = iconToDraw == Icon::chevronLeft ? -1.0f : 1.0f;
                juce::Path chevron;
                chevron.startNewSubPath (centre.x - direction * radius * 0.22f, centre.y - radius * 0.5f);
                chevron.lineTo (centre.x + direction * radius * 0.3f, centre.y);
                chevron.lineTo (centre.x - direction * radius * 0.22f, centre.y + radius * 0.5f);
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
