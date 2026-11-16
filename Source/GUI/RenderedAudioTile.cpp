#include "RenderedAudioTile.h"

namespace texture
{
    namespace
    {
        constexpr int dragThresholdPixels = 6;
    }

    RenderedAudioTile::RenderedAudioTile (TextureAudioProcessor& audioProcessor)
        : processor (audioProcessor)
    {
        setTooltip ("Drag this WAV into your DAW. Some hosts ignore file drops; use Show file instead.");
    }

    void RenderedAudioTile::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).brighter (0.06f));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colours::grey);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        const auto* audio = processor.getLastRenderedAudio();
        auto text = bounds.reduced (10.0f, 8.0f);

        if (audio == nullptr)
        {
            g.setColour (juce::Colours::lightgrey);
            g.drawFittedText ("Render for drag to create a WAV you can drag into your DAW.",
                              text.toNearestInt(), juce::Justification::centred, 2);
            return;
        }

        if (! audio->file.existsAsFile())
        {
            g.setColour (juce::Colours::orange);
            g.drawFittedText ("The rendered file is no longer on disk. Render again to drag it.",
                              text.toNearestInt(), juce::Justification::centred, 2);
            return;
        }

        const auto header = text.removeFromTop (18.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (audio->file.getFileName(), header, juce::Justification::centredLeft, true);

        const auto info = text.removeFromTop (16.0f);
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (juce::String (audio->lengthSeconds, 2) + " s  |  " + juce::String (audio->sampleRate / 1000.0, 1)
                        + " kHz  |  " + juce::String (audio->bitDepth) + "-bit stereo WAV  |  "
                        + juce::File::descriptionOfSizeInBytes (audio->bytes),
                    info, juce::Justification::centredLeft, true);

        const auto wave = text.removeFromTop (juce::jmax (12.0f, text.getHeight() - 22.0f));
        g.setColour (juce::Colours::cornflowerblue);

        const auto& peaks = audio->peaks;
        const auto count = static_cast<int> (peaks.size());

        for (int i = 0; i < count; ++i)
        {
            const auto x = wave.getX() + wave.getWidth() * static_cast<float> (i) / static_cast<float> (count);
            const auto half = juce::jlimit (0.0f, 1.0f, peaks[static_cast<size_t> (i)]) * wave.getHeight() * 0.5f;
            g.drawLine (x, wave.getCentreY() - half, x, wave.getCentreY() + half, 1.0f);
        }

        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Drag to a DAW. Some hosts ignore file drops: use Show file.",
                    text.removeFromBottom (18.0f), juce::Justification::centredLeft, true);
    }

    void RenderedAudioTile::mouseDrag (const juce::MouseEvent& event)
    {
        if (dragging || event.getDistanceFromDragStart() < dragThresholdPixels)
            return;

        const auto* audio = processor.getLastRenderedAudio();

        // Never start a drag from a missing or incomplete file.
        if (audio == nullptr || ! audio->file.existsAsFile())
            return;

        // The lease keeps the file while the drag runs. It ends when JUCE reports the
        // drag finished or cancelled, and never deletes the file: a DAW may reference it.
        auto lease = processor.getRenderedAudioCache().acquireLease (audio->file);

        juce::StringArray files;
        files.add (audio->file.getFullPathName());

        dragging = true;
        const auto started = juce::DragAndDropContainer::performExternalDragDropOfFiles (
            files, false, this, [lease]() mutable { lease.reset(); });
        dragging = false;

        if (! started)
            lease.reset();
    }
}
