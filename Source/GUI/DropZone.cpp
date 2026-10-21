#include "DropZone.h"

#include "../PluginProcessor.h"
#include "LookAndFeel.h"

#include "../Utility/ParameterIDs.h"

namespace texture
{
    namespace
    {
        constexpr const char* lastDirectoryKey = "last_sample_directory";
        constexpr const char* sampleFolderKey = "sample_folder";

        /** juce::String decodes a plain char* as ASCII, one code point per byte, so
            a UTF-8 literal has to be wrapped or its bytes arrive as Latin-1 -- the
            middot used to reach the screen as "A-circumflex plus dot". JUCE asserts
            on this in Debug; Release silently draws the wrong glyphs. */
        juce::String separator()
        {
            return juce::String::fromUTF8 ("  \xc2\xb7  ");
        }

        juce::String sourceNoteName (int midiNote)
        {
            static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const auto note = juce::jlimit (0, 127, midiNote);
            return juce::String (names[note % 12]) + juce::String (note / 12 - 2)
                 + " (" + juce::String (note) + ")";
        }
    }

    DropZone::DropZone (TextureAudioProcessor& processorToUse, int layerIndexToUse)
        : processor (processorToUse),
          layerIndex (layerIndexToUse)
    {
        waveform = std::make_unique<WaveformDisplay>();
        addAndMakeVisible (*waveform);

        sourceRootBox.setTooltip ("Source root note. MIDI 60 is labelled C3 in TEXTURE.");
        for (int note = 0; note <= 127; ++note)
            sourceRootBox.addItem (sourceNoteName (note), note + 1);
        sourceRootBox.setSelectedId (61, juce::dontSendNotification);
        addAndMakeVisible (sourceRootBox);

        sourceFineSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        sourceFineSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 48, 18);
        sourceFineSlider.setNumDecimalPlacesToDisplay (1);
        sourceFineSlider.setTextValueSuffix ("c");
        sourceFineSlider.setTooltip ("Fine source tuning in cents. Active notes glide to the new calibration over 20 ms.");
        addAndMakeVisible (sourceFineSlider);

        const auto rootId = layerIndex == 0 ? params::layer1Root : params::layer2Root;
        const auto fineId = layerIndex == 0 ? params::layer1FineCents : params::layer2FineCents;
        sourceRootAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.getValueTreeState(), rootId, sourceRootBox);
        sourceFineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getValueTreeState(), fineId, sourceFineSlider);

        setTooltip ("Drop an audio file or a .texture preset here, or click to browse. Once loaded, "
                    "click the waveform to set where notes start (Alt-click clears it). "
                    "Right-click for more options.");
        setTitle ("Sample slot " + juce::String (layerIndex + 1));
        setAccessible (true);
        setWantsKeyboardFocus (true);

        refreshFromProcessor();
    }

    DropZone::~DropZone() = default;

    void DropZone::refreshFromProcessor()
    {
        auto buffer = processor.getSampleForLayer (layerIndex);

        if (buffer != nullptr && buffer->isValid())
        {
            sampleName = buffer->getName();
            sampleDetails = juce::String (buffer->getLengthSeconds(), 2) + " s" + separator()
                          + juce::String (buffer->getOriginalChannelCount()) + " ch" + separator()
                          + juce::String (buffer->getOriginalSampleRate() / 1000.0, 1) + " kHz";
            waveform->setSample (buffer);
            waveform->setStartMarker (processor.getSampleStart (layerIndex));
        }
        else
        {
            sampleName.clear();
            sampleDetails.clear();
            waveform->setSample (nullptr);
        }

        sourceRootBox.setEnabled (buffer != nullptr && buffer->isValid());
        sourceFineSlider.setEnabled (buffer != nullptr && buffer->isValid());

        repaint();
    }

    void DropZone::setPlayheadPosition (float normalisedPosition)
    {
        waveform->setPlayheadPosition (normalisedPosition);
    }

    void DropZone::setEnvelopeColour (juce::Colour colour)
    {
        waveform->setEnvelopeColour (colour);
    }

    void DropZone::resized()
    {
        auto bounds = getLocalBounds().reduced (layout::grid);
        bounds.removeFromBottom (50);   // filename and source-pitch calibration controls
        waveform->setBounds (bounds);
        auto calibrationRow = getLocalBounds().removeFromBottom (22).reduced (layout::grid, 1);
        sourceRootBox.setBounds (calibrationRow.removeFromLeft (96));
        calibrationRow.removeFromLeft (4);
        sourceFineSlider.setBounds (calibrationRow);
    }

    void DropZone::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (colours::panel);
        g.fillRoundedRectangle (bounds, layout::panelCornerRadius);

        const auto hasSample = waveform->hasSample();

        if (! hasSample)
        {
            // Empty state: dashed border and an invitation (09_GUI_SPEC.md).
            const float dashes[] = { 5.0f, 4.0f };

            juce::Path border;
            border.addRoundedRectangle (bounds.reduced (1.0f), layout::panelCornerRadius);

            juce::PathStrokeType stroke (isDragOver ? 2.0f : 1.0f);
            juce::Path dashed;
            stroke.createDashedStroke (dashed, border, dashes, 2);

            g.setColour (isDragOver ? colours::accentPrimary
                                    : (isHovered ? colours::textMuted : colours::panelEdge));
            g.fillPath (dashed);

            g.setColour (isDragOver ? colours::accentPrimary : colours::textMuted);
            g.setFont (TextureLookAndFeel::interfaceFont (13.0f));
            g.drawText (isDragOver ? "Release to load"
                                   : "Drop a sample or click to browse",
                        bounds, juce::Justification::centred, false);

            // The slim layer-2 slot has no room for the format hint.
            if (! isDragOver && bounds.getHeight() >= 80.0f)
            {
                g.setColour (colours::textMuted.withAlpha (0.6f));
                g.setFont (TextureLookAndFeel::interfaceFont (10.0f));
                g.drawText (juce::String::fromUTF8 ("WAV \xc2\xb7 AIFF \xc2\xb7 FLAC \xc2\xb7 "
                                                    "MP3 \xc2\xb7 OGG \xc2\xb7 .texture"),
                            bounds.withTrimmedTop (bounds.getHeight() * 0.5f + 18.0f),
                            juce::Justification::centredTop, false);
            }

            return;
        }

        g.setColour (isDragOver ? colours::accentPrimary : colours::panelEdge);
        g.drawRoundedRectangle (bounds, layout::panelCornerRadius, isDragOver ? 2.0f : 1.0f);

        auto caption = getLocalBounds().removeFromBottom (50).reduced (layout::grid, 2);

        g.setColour (colours::textPrimary);
        g.setFont (TextureLookAndFeel::interfaceFont (10.0f, true));
        g.drawText (sampleName, caption.removeFromTop (12), juce::Justification::centredLeft, true);

        g.setColour (colours::textMuted);
        g.setFont (TextureLookAndFeel::monoFont (9.0f));
        g.drawText (sampleDetails, caption.removeFromTop (10), juce::Justification::centredLeft, true);

        if (isDragOver)
        {
            g.setColour (colours::accentPrimary.withAlpha (0.12f));
            g.fillRoundedRectangle (bounds, layout::panelCornerRadius);
        }
    }

    void DropZone::mouseEnter (const juce::MouseEvent&)
    {
        isHovered = true;
        repaint();
    }

    void DropZone::mouseExit (const juce::MouseEvent&)
    {
        isHovered = false;
        repaint();
    }

    void DropZone::mouseDown (const juce::MouseEvent& event)
    {
        if (event.mods.isPopupMenu())
        {
            showContextMenu();
            return;
        }

        if (! waveform->hasSample())
        {
            openFileChooser();
            return;
        }

        // Clicking a loaded waveform sets where notes start reading; the engine
        // picks it up on the next note (09_GUI_SPEC.md). Alt-click clears it.
        const auto area = waveform->getBounds();

        if (area.contains (event.getPosition()))
        {
            const auto normalised = event.mods.isAltDown()
                                        ? 0.0f
                                        : static_cast<float> (event.x - area.getX())
                                              / juce::jmax (1.0f, static_cast<float> (area.getWidth()));
            processor.setSampleStart (layerIndex, normalised);
            waveform->setStartMarker (normalised);
        }
    }

    void DropZone::showContextMenu()
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());

        const auto hasSample = waveform->hasSample();

        menu.addItem (1, "Load sample...");
        menu.addItem (2, "Clear sample", hasSample);
        menu.addItem (5, "Reset start marker", hasSample && processor.getSampleStart (layerIndex) > 0.0f);
        menu.addSeparator();
        menu.addItem (3, "Show sample in file browser", hasSample);
        menu.addItem (4, "Open factory samples folder");
        menu.addItem (6, "Reset source pitch calibration", hasSample);

        juce::Component::SafePointer<DropZone> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safeThis] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            switch (result)
            {
                case 1:
                    safeThis->openFileChooser();
                    break;

                case 2:
                    safeThis->processor.clearLayer (safeThis->layerIndex);
                    break;

                case 3:
                {
                    const auto file = safeThis->processor.getSampleFileForLayer (safeThis->layerIndex);

                    if (file.existsAsFile())
                        file.revealToUser();
                    break;
                }

                case 4:
                    PresetManager::getFactorySampleDirectory().revealToUser();
                    break;

                case 5:
                    safeThis->processor.setSampleStart (safeThis->layerIndex, 0.0f);
                    safeThis->waveform->setStartMarker (0.0f);
                    break;

                case 6:
                {
                    safeThis->processor.getUndoManager().beginNewTransaction ("Reset source pitch calibration");
                    safeThis->sourceRootBox.setSelectedId (61);
                    safeThis->sourceFineSlider.setValue (0.0);
                    break;
                }

                default:
                    break;
            }
        });
    }

    void DropZone::openFileChooser()
    {
        // Last folder used, else the sample folder from Preferences, else the factory pads.
        auto startDirectory = PresetManager::getFactorySampleDirectory();

        if (auto* store = processor.getApplicationProperties().getUserSettings())
        {
            const juce::File configured (store->getValue (sampleFolderKey));
            const juce::File remembered (store->getValue (lastDirectoryKey));

            if (remembered.isDirectory())
                startDirectory = remembered;
            else if (configured.isDirectory())
                startDirectory = configured;
        }

        const auto wildcard = processor.getSampleLoader().getSupportedWildcard() + ";*.texture";

        fileChooser = std::make_unique<juce::FileChooser> ("Load a sample or preset",
                                                            startDirectory,
                                                            wildcard);

        juce::Component::SafePointer<DropZone> safeThis (this);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto file = chooser.getResult();

            if (file != juce::File())
                safeThis->processor.loadFileIntoLayer (file, safeThis->layerIndex);
        });
    }

    bool DropZone::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (const auto& path : files)
        {
            const juce::File file (path);

            if (SampleLoader::isPresetFile (file) || processor.getSampleLoader().isSupportedAudioFile (file))
                return true;
        }

        return ! files.isEmpty();   // still accept, so we can explain what went wrong
    }

    void DropZone::fileDragEnter (const juce::StringArray&, int, int)
    {
        isDragOver = true;
        repaint();
    }

    void DropZone::fileDragExit (const juce::StringArray&)
    {
        isDragOver = false;
        repaint();
    }

    void DropZone::filesDropped (const juce::StringArray& files, int, int)
    {
        isDragOver = false;
        repaint();

        if (files.isEmpty())
            return;

        if (files.size() > 1)
            processor.postToast ("Only the first file was loaded.", SampleLoader::Severity::info);

        processor.loadFileIntoLayer (juce::File (files[0]), layerIndex);
    }
}
