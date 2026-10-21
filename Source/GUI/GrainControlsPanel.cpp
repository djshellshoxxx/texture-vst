#include "GrainControlsPanel.h"

#include "../PluginProcessor.h"
#include "Colors.h"
#include "LookAndFeel.h"

namespace texture
{
    namespace
    {
        constexpr int rowHeight = 22;
        constexpr int gap = 6;
        constexpr int knobWidth = 64;
        constexpr int knobHeight = 84;

        struct FieldText
        {
            const char* label;
            const char* tooltip;
        };

        constexpr std::array<FieldText, numGrainFields> fieldText {{
            { "Size", "Grain length in milliseconds. Follow MORPH sets it from the MORPH knob." },
            { "Density", "Grains started per second in each voice. Follow MORPH derives it from size and overlap." },
            { "Jitter", "How far each grain's start wanders from the read point. Zero is exact." },
            { "Detune", "Random pitch offset per grain, in cents, up and down." },
            { "Spread", "Stereo spread of each grain. Zero keeps grains centred." }
        }};

        juce::String formatEffective (GrainField field, float value)
        {
            switch (field)
            {
                case GrainField::size:    return juce::String (juce::roundToInt (value)) + " ms";
                case GrainField::density: return juce::String (value, 1) + " Hz";
                case GrainField::jitter:  return juce::String (juce::roundToInt (value * 100.0f)) + "%";
                case GrainField::detune:  return juce::String (juce::roundToInt (value)) + " ct";
                case GrainField::pan:     return juce::String (juce::roundToInt (value * 100.0f)) + "%";
            }

            return {};
        }
    }

    struct GrainControlsPanel::Column
    {
        Column (TextureAudioProcessor& processor, GrainField gridField)
            : field (gridField),
              knob (processor, grainValueParameterID (gridField), fieldText[static_cast<std::size_t> (gridField)].label,
                    juce::String (fieldText[static_cast<std::size_t> (gridField)].tooltip)
                        + " Used when Follow MORPH is off.")
        {
            knob.setShowsLabel (true);

            follow.setButtonText ("Follow");
            follow.setClickingTogglesState (true);
            follow.setTooltip ("On: MORPH sets this control and the value above is ignored. "
                               "Off: the value above is used. Turning Follow off copies the current value into it first.");

            readout.setFont (TextureLookAndFeel::labelFont());
            readout.setColour (juce::Label::textColourId, colours::textMuted);
            readout.setMinimumHorizontalScale (0.7f);
            readout.setJustificationType (juce::Justification::centred);
            readout.setTooltip ("The value the voices are using now.");
        }

        GrainField field;
        ParameterKnob knob;
        juce::TextButton follow;
        juce::Label readout;
    };

    GrainControlsPanel::GrainControlsPanel (TextureAudioProcessor& owner, std::function<void()> layoutChanged)
        : processor (owner),
          onLayoutChanged (std::move (layoutChanged))
    {
        header.setButtonText ("Grains");
        header.setTooltip ("Show or hide the grain overrides: size, density, jitter, detune and spread.");
        header.setClickingTogglesState (true);
        header.setToggleState (false, juce::dontSendNotification);
        header.onClick = [this] { if (onLayoutChanged) onLayoutChanged(); };
        addAndMakeVisible (header);

        poolLabel.setFont (TextureLookAndFeel::labelFont());
        poolLabel.setColour (juce::Label::textColourId, colours::textMuted);
        poolLabel.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (poolLabel);

        for (int i = 0; i < numGrainFields; ++i)
        {
            const auto field = static_cast<GrainField> (i);
            auto column = std::make_unique<Column> (processor, field);

            column->follow.onClick = [this, field]
            {
                const auto settings = processor.getGrainControlSettings();
                processor.setGrainFollow (field, ! settings.follow[static_cast<std::size_t> (field)]);
                refresh();
            };

            addAndMakeVisible (column->knob);
            addAndMakeVisible (column->follow);
            addAndMakeVisible (column->readout);
            columns[static_cast<std::size_t> (i)] = std::move (column);
        }

        refresh();
    }

    GrainControlsPanel::~GrainControlsPanel() = default;

    int GrainControlsPanel::preferredHeight() const
    {
        int height = rowHeight;                        // header

        if (header.getToggleState())
            height += gap / 2 + rowHeight                // pool status
                    + knobHeight + rowHeight + rowHeight; // knob, Follow, readout

        return height;
    }

    void GrainControlsPanel::refresh()
    {
        const auto settings = processor.getGrainControlSettings();

        for (int i = 0; i < numGrainFields; ++i)
        {
            const auto field = static_cast<GrainField> (i);
            auto& column = *columns[static_cast<std::size_t> (i)];
            column.follow.setToggleState (settings.follow[static_cast<std::size_t> (i)], juce::dontSendNotification);
            column.readout.setText ("Now " + formatEffective (field, processor.getGrainEffectiveValue (field)),
                                    juce::dontSendNotification);
        }

        const auto over = processor.isGrainOversubscribed();
        poolLabel.setText (over ? "Pool full: some grains are skipped. Shorten the size or lower the density."
                                : "Up to 8 grains per voice. Extra grains are skipped when it is full.",
                           juce::dontSendNotification);
        poolLabel.setColour (juce::Label::textColourId, over ? colours::accentPrimary : colours::textMuted);
    }

    void GrainControlsPanel::resized()
    {
        auto area = getLocalBounds();

        header.setBounds (area.removeFromTop (rowHeight).removeFromLeft (120));

        const auto open = header.getToggleState();

        for (auto& column : columns)
        {
            column->knob.setVisible (open);
            column->follow.setVisible (open);
            column->readout.setVisible (open);
        }

        poolLabel.setVisible (open);

        if (! open)
            return;

        area.removeFromTop (gap / 2);
        poolLabel.setBounds (area.removeFromTop (rowHeight));

        auto knobRow = area.removeFromTop (knobHeight);
        auto followRow = area.removeFromTop (rowHeight);
        auto readoutRow = area.removeFromTop (rowHeight);

        for (auto& column : columns)
        {
            column->knob.setBounds (knobRow.removeFromLeft (knobWidth));
            column->follow.setBounds (followRow.removeFromLeft (knobWidth).reduced (2, 1));
            column->readout.setBounds (readoutRow.removeFromLeft (knobWidth));
        }
    }
}
