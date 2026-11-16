#include "ScanPanel.h"

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
        constexpr int modeWidth = 120;
        constexpr int freezeWidth = 72;
    }

    struct ScanPanel::LayerRow
    {
        LayerRow (TextureAudioProcessor& processor, int layer)
            : label(),
              modeBox (processor, layer == 0 ? params::layer1ScanMode : params::layer2ScanMode,
                       juce::StringArray { "Legacy", "Manual", "Scan" },
                       "Legacy: the voices' own read heads, as before. Manual: the position below. "
                       "Scan: the position moves at the speed below and wraps."),
              freezeToggle (processor, layer == 0 ? params::layer1Freeze : params::layer2Freeze, "Freeze",
                            "Hold the current read point while notes keep playing. The point is written to the "
                            "position when Freeze turns on, so a saved patch returns there."),
              positionKnob (processor, layer == 0 ? params::layer1ScanPosition : params::layer2ScanPosition,
                            "Position", "Where grains read in Manual and Scan modes. This does not trim the sample."),
              speedKnob (processor, layer == 0 ? params::layer1ScanSpeed : params::layer2ScanSpeed,
                         "Speed", "Scan speed in source lengths per second. Negative runs backwards."),
              spreadKnob (processor, layer == 0 ? params::layer1FreezeSpread : params::layer2FreezeSpread,
                          "Spread", "While frozen, how far grains wander from the frozen point.")
        {
            label.setText ("Layer " + juce::String (layer + 1), juce::dontSendNotification);
            label.setFont (TextureLookAndFeel::labelFont());
            label.setColour (juce::Label::textColourId, colours::textMuted);
            positionKnob.setShowsLabel (true);
            speedKnob.setShowsLabel (true);
            spreadKnob.setShowsLabel (true);
        }

        juce::Label label;
        ParameterChoiceBox modeBox;
        ParameterToggle freezeToggle;
        ParameterKnob positionKnob;
        ParameterKnob speedKnob;
        ParameterKnob spreadKnob;
    };

    ScanPanel::ScanPanel (TextureAudioProcessor& processor, std::function<void()> layoutChanged)
        : onLayoutChanged (std::move (layoutChanged))
    {
        header.setButtonText ("Scan and freeze");
        header.setTooltip ("Show or hide the read position, scan and freeze controls for each layer.");
        header.setClickingTogglesState (true);
        header.setToggleState (false, juce::dontSendNotification);
        header.onClick = [this] { if (onLayoutChanged) onLayoutChanged(); };
        addAndMakeVisible (header);

        for (int layer = 0; layer < 2; ++layer)
        {
            auto& row = rows[static_cast<std::size_t> (layer)];
            row = std::make_unique<LayerRow> (processor, layer);
            addAndMakeVisible (row->label);
            addAndMakeVisible (row->modeBox);
            addAndMakeVisible (row->freezeToggle);
            addAndMakeVisible (row->positionKnob);
            addAndMakeVisible (row->speedKnob);
            addAndMakeVisible (row->spreadKnob);
        }
    }

    ScanPanel::~ScanPanel() = default;

    int ScanPanel::preferredHeight() const
    {
        int height = rowHeight;

        if (header.getToggleState())
            height += 2 * (gap / 2 + rowHeight + knobHeight);

        return height;
    }

    void ScanPanel::resized()
    {
        auto area = getLocalBounds();
        header.setBounds (area.removeFromTop (rowHeight).removeFromLeft (150));

        const auto open = header.getToggleState();

        for (auto& row : rows)
        {
            row->label.setVisible (open);
            row->modeBox.setVisible (open);
            row->freezeToggle.setVisible (open);
            row->positionKnob.setVisible (open);
            row->speedKnob.setVisible (open);
            row->spreadKnob.setVisible (open);
        }

        if (! open)
            return;

        for (auto& row : rows)
        {
            area.removeFromTop (gap / 2);
            auto topRow = area.removeFromTop (rowHeight);
            row->label.setBounds (topRow.removeFromLeft (48));
            row->modeBox.setBounds (topRow.removeFromLeft (modeWidth));
            topRow.removeFromLeft (gap);
            row->freezeToggle.setBounds (topRow.removeFromLeft (freezeWidth));

            auto knobRow = area.removeFromTop (knobHeight);
            row->positionKnob.setBounds (knobRow.removeFromLeft (knobWidth));
            row->speedKnob.setBounds (knobRow.removeFromLeft (knobWidth));
            row->spreadKnob.setBounds (knobRow.removeFromLeft (knobWidth));
        }
    }
}
