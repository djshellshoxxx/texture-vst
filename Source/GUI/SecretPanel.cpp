#include "SecretPanel.h"

#include "../PluginProcessor.h"

namespace texture
{
    namespace
    {
        void configureKnob (juce::Slider& knob, double minimum, double maximum, double interval,
                            double defaultValue, const juce::String& tooltip)
        {
            knob.setRange (minimum, maximum, interval);
            knob.setValue (defaultValue, juce::dontSendNotification);
            knob.setDoubleClickReturnValue (true, defaultValue);
            knob.setTooltip (tooltip + " Right-click to reset or type a value.");
            knob.setColour (juce::Slider::rotarySliderFillColourId, colours::accentSecondary);
        }
    }

    SecretPanel::SecretPanel (TextureAudioProcessor& processorToUse)
        : processor (processorToUse)
    {
        setOpaque (false);

        addAndMakeVisible (header);

        configureKnob (bitsKnob, SecretEffect::minBits, SecretEffect::maxBits, 1.0,
                       SecretEffect::defaultBits,
                       "Secret hidden effect - GRIT bit depth. Lower means coarser quantisation.");
        addAndMakeVisible (bitsKnob);

        configureKnob (rateKnob, SecretEffect::minDownsample, SecretEffect::maxDownsample, 1.0,
                       SecretEffect::defaultDownsample,
                       "Secret hidden effect - GRIT sample rate divider. Higher means more aliasing.");
        addAndMakeVisible (rateKnob);

        configureKnob (mixKnob, 0.0, 1.0, 0.001, SecretEffect::defaultMix,
                       "Secret hidden effect - GRIT wet mix. At zero the effect is fully bypassed.");
        addAndMakeVisible (mixKnob);

        mixKnob.textFromValueFunction = [] (double value) { return juce::String (juce::roundToInt (value * 100.0)) + "%"; };
        mixKnob.valueFromTextFunction = [] (const juce::String& text)
        {
            const auto value = text.getDoubleValue();
            return text.containsChar ('%') || value > 1.0 ? value / 100.0 : value;
        };

        blurb.setText ("You found it. GRIT crushes and decimates the output after the reverb. "
                       "It stays out of the host's automation list and off by default.",
                       juce::dontSendNotification);
        blurb.setFont (TextureLookAndFeel::interfaceFont (10.0f));
        blurb.setColour (juce::Label::textColourId, colours::textMuted);
        blurb.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (blurb);

        closeButton.setTooltip ("Hide the secret effect again (its settings are kept)");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        for (auto* knob : { &bitsKnob, &rateKnob, &mixKnob })
            knob->onValueChange = [this] { pushToProcessor(); };

        refreshFromProcessor();
    }

    SecretPanel::~SecretPanel() = default;

    void SecretPanel::refreshFromProcessor()
    {
        const auto settings = processor.getSecretSettings();

        isUpdatingFromProcessor = true;
        bitsKnob.setValue (settings.bitDepth, juce::dontSendNotification);
        rateKnob.setValue (settings.downsample, juce::dontSendNotification);
        mixKnob.setValue (settings.mix, juce::dontSendNotification);
        isUpdatingFromProcessor = false;
    }

    void SecretPanel::pushToProcessor()
    {
        if (isUpdatingFromProcessor)
            return;

        TextureAudioProcessor::SecretSettings settings;
        settings.bitDepth = static_cast<float> (bitsKnob.getValue());
        settings.downsample = static_cast<float> (rateKnob.getValue());
        settings.mix = static_cast<float> (mixKnob.getValue());
        settings.revealed = true;

        processor.setSecretSettings (settings);
    }

    void SecretPanel::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (colours::panel);
        g.fillRoundedRectangle (bounds, layout::panelCornerRadius);

        g.setColour (colours::accentSecondary.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds, layout::panelCornerRadius, 1.0f);
    }

    void SecretPanel::resized()
    {
        auto bounds = getLocalBounds().reduced (layout::grid);

        auto headerRow = bounds.removeFromTop (16);
        closeButton.setBounds (headerRow.removeFromRight (60).withSizeKeepingCentre (60, 16));
        header.setBounds (headerRow);

        bounds.removeFromTop (4);

        auto knobRow = bounds.removeFromTop (juce::jmin (64, bounds.getHeight() - 22));
        const auto knobWidth = knobRow.getWidth() / 3;

        for (auto* knob : { &bitsKnob, &rateKnob, &mixKnob })
            knob->setBounds (knobRow.removeFromLeft (knobWidth).reduced (2, 0));

        blurb.setBounds (bounds);
    }
}
