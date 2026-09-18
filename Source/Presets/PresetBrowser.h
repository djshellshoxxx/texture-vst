#pragma once

#include "PresetManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace texture
{
    class TextureAudioProcessor;

    /** The Pro preset browser: a modal overlay with category chips, a search box
        and a scrollable list (12_PRESET_SYSTEM.md).

        The list is served from PresetManager's cache; disk is only rescanned when
        the user asks for it (21_WHAT_NOT_TO_DO.md, item 37).
    */
    class PresetBrowser : public juce::Component,
                          private juce::ListBoxModel,
                          private juce::TextEditor::Listener
    {
    public:
        explicit PresetBrowser (TextureAudioProcessor& processor);
        ~PresetBrowser() override;

        /** Re-reads the cached index and reapplies the current filters. */
        void refresh();

        /** Called when the browser wants to close itself. */
        std::function<void()> onClose;

        void paint (juce::Graphics& g) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

    private:
        // --- ListBoxModel -----------------------------------------------------
        int getNumRows() override;
        void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height,
                               bool rowIsSelected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent& event) override;
        void listBoxItemClicked (int row, const juce::MouseEvent& event) override;
        void deleteKeyPressed (int lastRowSelected) override;
        void returnKeyPressed (int lastRowSelected) override;

        // --- TextEditor::Listener ---------------------------------------------
        void textEditorTextChanged (juce::TextEditor& editor) override;

        void applyFilters();
        void loadRow (int row);
        void showRowContextMenu (int row);
        void importFromFile();

        TextureAudioProcessor& processor;

        juce::TextEditor searchBox;
        juce::ListBox list;
        juce::TextButton closeButton { "Close" };
        juce::TextButton rescanButton { "Rescan" };
        juce::TextButton importButton { "Import..." };
        juce::ToggleButton favouritesToggle { "Favourites" };

        juce::OwnedArray<juce::TextButton> categoryChips;
        juce::String selectedCategory { "All" };

        std::vector<PresetManager::PresetInfo> visiblePresets;
        std::unique_ptr<juce::FileChooser> fileChooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
    };
}
