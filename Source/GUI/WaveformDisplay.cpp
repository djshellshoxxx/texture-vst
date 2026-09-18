#include "WaveformDisplay.h"

#include "LookAndFeel.h"

namespace texture
{
    WaveformDisplay::WaveformDisplay()
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
    }

    void WaveformDisplay::setSample (SampleBufferPtr newSample)
    {
        sample = std::move (newSample);
        playhead = 0.0f;
        startMarker = 0.0f;
        imageIsStale = true;
        repaint();
    }

    void WaveformDisplay::setEnvelopeColour (juce::Colour newColour)
    {
        if (newColour == envelopeColour)
            return;

        envelopeColour = newColour;
        imageIsStale = true;
        repaint();
    }

    void WaveformDisplay::setPlayheadPosition (float normalisedPosition)
    {
        const auto clamped = juce::jlimit (0.0f, 1.0f, normalisedPosition);

        // Only repaint when the cursor actually moved by a visible amount.
        if (std::abs (clamped - playhead) * static_cast<float> (getWidth()) < 1.0f)
            return;

        playhead = clamped;
        repaint();
    }

    void WaveformDisplay::setStartMarker (float normalisedPosition)
    {
        startMarker = juce::jlimit (0.0f, 1.0f, normalisedPosition);
        repaint();
    }

    void WaveformDisplay::resized()
    {
        imageIsStale = true;
    }

    void WaveformDisplay::rebuildImage()
    {
        imageIsStale = false;

        const auto width = getWidth();
        const auto height = getHeight();

        if (width <= 0 || height <= 0 || ! hasSample())
        {
            cachedEnvelope = juce::Image();
            return;
        }

        cachedEnvelope = juce::Image (juce::Image::ARGB, width, height, true);
        juce::Graphics g (cachedEnvelope);

        const auto& peaks = sample->getWaveformPeaks();
        const auto& rms = sample->getWaveformRms();

        if (peaks.empty())
            return;

        const auto centreY = static_cast<float> (height) * 0.5f;
        const auto halfHeight = centreY - 2.0f;
        const auto count = static_cast<int> (peaks.size());

        juce::Path peakPath;
        juce::Path rmsPath;

        for (int x = 0; x < width; ++x)
        {
            const auto binIndex = juce::jlimit (0, count - 1,
                                                juce::roundToInt (static_cast<float> (x) / juce::jmax (1.0f, static_cast<float> (width - 1))
                                                                   * static_cast<float> (count - 1)));

            const auto peak = juce::jlimit (0.0f, 1.0f, peaks[static_cast<size_t> (binIndex)]) * halfHeight;
            const auto level = juce::jlimit (0.0f, 1.0f, rms[static_cast<size_t> (binIndex)]) * halfHeight;
            const auto fx = static_cast<float> (x);

            peakPath.addLineSegment ({ fx, centreY - juce::jmax (0.5f, peak),
                                       fx, centreY + juce::jmax (0.5f, peak) }, 1.0f);

            rmsPath.addLineSegment ({ fx, centreY - juce::jmax (0.5f, level),
                                      fx, centreY + juce::jmax (0.5f, level) }, 1.0f);
        }

        g.setColour (envelopeColour.withAlpha (0.35f));
        g.fillPath (peakPath);

        g.setColour (envelopeColour.withAlpha (0.85f));
        g.fillPath (rmsPath);

        // Centre line.
        g.setColour (colours::panelEdge.withAlpha (0.8f));
        g.drawHorizontalLine (juce::roundToInt (centreY), 0.0f, static_cast<float> (width));
    }

    void WaveformDisplay::paint (juce::Graphics& g)
    {
        if (! hasSample())
            return;

        if (imageIsStale || cachedEnvelope.isNull())
            rebuildImage();

        if (cachedEnvelope.isValid())
            g.drawImageAt (cachedEnvelope, 0, 0);

        const auto width = static_cast<float> (getWidth());
        const auto height = static_cast<float> (getHeight());

        // Start marker: where the user clicked, applied on the next note.
        if (startMarker > 0.0f)
        {
            g.setColour (colours::textMuted.withAlpha (0.5f));
            g.drawVerticalLine (juce::roundToInt (startMarker * width), 0.0f, height);
        }

        // Playhead.
        const auto x = playhead * width;

        g.setColour (colours::accentSecondary.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (x - 1.5f, 0.0f, 3.0f, height));

        g.setColour (colours::accentSecondary);
        g.drawVerticalLine (juce::roundToInt (x), 0.0f, height);
    }
}
