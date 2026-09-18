#include "PresetBrowser.h"

#include "../GUI/Colors.h"
#include "../GUI/LookAndFeel.h"
#include "../PluginProcessor.h"

namespace texture
{
    PresetBrowser::PresetBrowser (TextureAudioProcessor& processorToUse)
        : processor (processorToUse)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);

        searchBox.setTextToShowWhenEmpty ("Search by name, tag or author", colours::textMuted);
        searchBox.setFont (TextureLookAndFeel::interfaceFont (13.0f));
        searchBox.addListener (this);
        searchBox.setTitle ("Preset search");
        addAndMakeVisible (searchBox);

        list.setModel (this);
        list.setRowHeight (34);
        list.setColour (juce::ListBox::backgroundColourId, colours::background);
        list.setOutlineThickness (1);
        list.setTitle ("Preset list");
        addAndMakeVisible (list);

        juce::StringArray categories { "All" };
        categories.addArray (PresetManager::getCategories());

        for (const auto& category : categories)
        {
            auto* chip = categoryChips.add (new juce::TextButton (category));
            chip->setClickingTogglesState (false);
            chip->setToggleState (category == selectedCategory, juce::dontSendNotification);
            chip->setTooltip ("Show only " + category + " presets");
            chip->onClick = [this, category]
            {
                selectedCategory = category;

                for (auto* button : categoryChips)
                    button->setToggleState (button->getButtonText() == category, juce::dontSendNotification);

                applyFilters();
            };

            addAndMakeVisible (chip);
        }

        favouritesToggle.setTooltip ("Show only presets you have starred");
        favouritesToggle.onClick = [this] { applyFilters(); };
        addAndMakeVisible (favouritesToggle);

        rescanButton.setTooltip ("Rescan the preset folder for new files");
        rescanButton.onClick = [this]
        {
            processor.getPresetManager().refresh();
            refresh();
            processor.postToast ("Preset folder rescanned.", SampleLoader::Severity::info);
        };
        addAndMakeVisible (rescanButton);

        importButton.setTooltip ("Import a .texture preset from anywhere on disk");
        importButton.onClick = [this] { importFromFile(); };
        addAndMakeVisible (importButton);

        closeButton.setTooltip ("Close the browser (Esc)");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        refresh();
    }

    PresetBrowser::~PresetBrowser()
    {
        searchBox.removeListener (this);
        list.setModel (nullptr);
    }

    void PresetBrowser::refresh()
    {
        applyFilters();
    }

    void PresetBrowser::applyFilters()
    {
        visiblePresets = processor.getPresetManager().filter (selectedCategory,
                                                              searchBox.getText(),
                                                              favouritesToggle.getToggleState());
        list.updateContent();
        list.repaint();
    }

    void PresetBrowser::textEditorTextChanged (juce::TextEditor&)
    {
        applyFilters();
    }

    int PresetBrowser::getNumRows()
    {
        return static_cast<int> (visiblePresets.size());
    }

    void PresetBrowser::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height,
                                          bool rowIsSelected)
    {
        if (rowNumber < 0 || rowNumber >= static_cast<int> (visiblePresets.size()))
            return;

        const auto& info = visiblePresets[static_cast<size_t> (rowNumber)];
        juce::Rectangle<int> bounds (0, 0, width, height);

        if (rowIsSelected)
        {
            g.setColour (colours::accentPrimary.withAlpha (0.14f));
            g.fillRect (bounds);
        }

        g.setColour (colours::panelEdge.withAlpha (0.5f));
        g.fillRect (bounds.removeFromBottom (1));

        auto content = bounds.reduced (10, 4);

        // Favourite star.
        const auto starArea = content.removeFromLeft (18);

        if (info.isFavourite)
        {
            g.setColour (colours::warning);
            g.setFont (TextureLookAndFeel::interfaceFont (12.0f));
            g.drawText ("*", starArea, juce::Justification::centred, false);
        }

        const auto categoryArea = content.removeFromRight (90);

        g.setColour (info.isFactory ? colours::accentPro : colours::textMuted);
        g.setFont (TextureLookAndFeel::labelFont());
        g.drawText (info.category.toUpperCase(), categoryArea, juce::Justification::centredRight, false);

        g.setColour (colours::textPrimary);
        g.setFont (TextureLookAndFeel::interfaceFont (13.0f));
        g.drawText (info.name, content.removeFromTop (content.getHeight() / 2 + 2),
                    juce::Justification::centredLeft, true);

        g.setColour (colours::textMuted);
        g.setFont (TextureLookAndFeel::interfaceFont (10.0f));
        g.drawText (info.tags.joinIntoString (", "), content, juce::Justification::centredLeft, true);
    }

    void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& event)
    {
        if (event.mods.isPopupMenu())
            showRowContextMenu (row);
    }

    void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
    {
        loadRow (row);
    }

    void PresetBrowser::returnKeyPressed (int lastRowSelected)
    {
        loadRow (lastRowSelected);
    }

    void PresetBrowser::deleteKeyPressed (int lastRowSelected)
    {
        if (lastRowSelected < 0 || lastRowSelected >= static_cast<int> (visiblePresets.size()))
            return;

        const auto info = visiblePresets[static_cast<size_t> (lastRowSelected)];

        if (info.isFactory)
        {
            processor.postToast ("Factory presets cannot be deleted.", SampleLoader::Severity::info);
            return;
        }

        juce::Component::SafePointer<PresetBrowser> safeThis (this);

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Delete preset")
                .withMessage ("Delete \"" + info.name + "\"? This cannot be undone.")
                .withButton ("Delete")
                .withButton ("Cancel"),
            [safeThis, info] (int result)
            {
                if (safeThis == nullptr || result != 1)
                    return;

                if (info.file.deleteFile())
                {
                    safeThis->processor.getPresetManager().refresh();
                    safeThis->refresh();
                    safeThis->processor.postToast ("Deleted " + info.name + ".", SampleLoader::Severity::info);
                }
                else
                {
                    safeThis->processor.postToast ("Could not delete " + info.name + ".",
                                                   SampleLoader::Severity::warning);
                }
            });
    }

    void PresetBrowser::loadRow (int row)
    {
        if (row < 0 || row >= static_cast<int> (visiblePresets.size()))
            return;

        if (processor.loadPreset (visiblePresets[static_cast<size_t> (row)].file) && onClose)
            onClose();
    }

    void PresetBrowser::showRowContextMenu (int row)
    {
        if (row < 0 || row >= static_cast<int> (visiblePresets.size()))
            return;

        const auto info = visiblePresets[static_cast<size_t> (row)];

        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        menu.addItem (1, "Load");
        menu.addItem (2, info.isFavourite ? "Remove from favourites" : "Add to favourites");
        menu.addSeparator();
        menu.addItem (3, "Duplicate", ! info.isFactory);
        menu.addItem (4, "Delete", ! info.isFactory);
        menu.addSeparator();
        menu.addItem (5, "Show in file browser");
        menu.addItem (6, "Import from file...");

        juce::Component::SafePointer<PresetBrowser> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safeThis, info, row] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            auto& presets = safeThis->processor.getPresetManager();

            switch (result)
            {
                case 1:
                    safeThis->loadRow (row);
                    break;

                case 2:
                    presets.setFavourite (info.file, ! info.isFavourite);
                    safeThis->refresh();
                    break;

                case 3:
                {
                    PresetManager::PresetData data;
                    juce::String error;

                    if (presets.load (info.file, data, error) != PresetManager::Status::ok)
                    {
                        safeThis->processor.postToast (error, SampleLoader::Severity::warning);
                        return;
                    }

                    data.name = info.name + " Copy";
                    const auto destination = PresetManager::getUserPresetDirectory()
                                                 .getChildFile (info.category)
                                                 .getChildFile (data.name + PresetManager::fileExtension)
                                                 .getNonexistentSibling();

                    if (presets.save (destination, data, error) == PresetManager::Status::ok)
                    {
                        presets.refresh();
                        safeThis->refresh();
                        safeThis->processor.postToast ("Duplicated " + info.name + ".",
                                                       SampleLoader::Severity::info);
                    }
                    else
                    {
                        safeThis->processor.postToast (error, SampleLoader::Severity::warning);
                    }
                    break;
                }

                case 4:
                    safeThis->deleteKeyPressed (row);
                    break;

                case 5:
                    info.file.revealToUser();
                    break;

                case 6:
                    safeThis->importFromFile();
                    break;

                default:
                    break;
            }
        });
    }

    void PresetBrowser::importFromFile()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Import a .texture preset",
                                                            PresetManager::getUserPresetDirectory(),
                                                            "*.texture");

        juce::Component::SafePointer<PresetBrowser> safeThis (this);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto source = chooser.getResult();

            if (source == juce::File())
                return;

            auto& presets = safeThis->processor.getPresetManager();

            PresetManager::PresetData data;
            juce::String error;

            if (presets.load (source, data, error) != PresetManager::Status::ok)
            {
                safeThis->processor.postToast (error, SampleLoader::Severity::warning);
                return;
            }

            const auto destination = PresetManager::getUserPresetDirectory()
                                         .getChildFile (data.category)
                                         .getChildFile (source.getFileName())
                                         .getNonexistentSibling();

            if (source.copyFileTo (destination))
            {
                presets.refresh();
                safeThis->refresh();
                safeThis->processor.postToast ("Imported " + data.name + ".", SampleLoader::Severity::info);
            }
            else
            {
                safeThis->processor.postToast ("Could not import that preset.",
                                               SampleLoader::Severity::warning);
            }
        });
    }

    bool PresetBrowser::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onClose)
                onClose();

            return true;
        }

        return false;
    }

    void PresetBrowser::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background);

        auto header = getLocalBounds().removeFromTop (34);

        g.setColour (colours::panel);
        g.fillRect (header);

        g.setColour (colours::panelEdge);
        g.fillRect (header.removeFromBottom (1));

        g.setColour (colours::textPrimary);
        g.setFont (TextureLookAndFeel::interfaceFont (14.0f, true));
        g.drawText ("PRESETS", header.reduced (layout::windowEdgePadding, 0),
                    juce::Justification::centredLeft, false);

        g.setColour (colours::textMuted);
        g.setFont (TextureLookAndFeel::monoFont (10.0f));
        g.drawText (juce::String (static_cast<int> (visiblePresets.size())) + " shown",
                    header.reduced (layout::windowEdgePadding, 0),
                    juce::Justification::centredRight, false);
    }

    void PresetBrowser::resized()
    {
        auto bounds = getLocalBounds();
        bounds.removeFromTop (34);
        bounds = bounds.reduced (layout::windowEdgePadding, layout::grid);

        auto searchRow = bounds.removeFromTop (28);
        closeButton.setBounds (searchRow.removeFromRight (70));
        searchRow.removeFromRight (layout::grid);
        importButton.setBounds (searchRow.removeFromRight (80));
        searchRow.removeFromRight (4);
        rescanButton.setBounds (searchRow.removeFromRight (70));
        searchRow.removeFromRight (layout::grid);
        searchBox.setBounds (searchRow);

        bounds.removeFromTop (layout::grid);

        auto chipRow = bounds.removeFromTop (26);
        favouritesToggle.setBounds (chipRow.removeFromRight (100));

        const auto chipWidth = juce::jmax (56, chipRow.getWidth() / juce::jmax (1, categoryChips.size()) - 4);

        for (auto* chip : categoryChips)
        {
            chip->setBounds (chipRow.removeFromLeft (chipWidth));
            chipRow.removeFromLeft (4);
        }

        bounds.removeFromTop (layout::grid);
        list.setBounds (bounds);
    }
}
