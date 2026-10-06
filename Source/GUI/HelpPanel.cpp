#include "HelpPanel.h"

#include "../PluginProcessor.h"

namespace texture
{
    namespace
    {
        constexpr const char* homepageUrl = "https://texture-synth.com";
        constexpr const char* circuitDriftUrl = "https://djshellshoxxx.github.io/circuitdriftlabs/";
        constexpr const char* githubUrl = "https://github.com/djshellshoxxx/texture-vst";
        constexpr const char* supportEmail = "support@texture-synth.com";

        juce::String versionString()
        {
           #if defined (JucePlugin_VersionString)
            return JucePlugin_VersionString;
           #else
            return "1.0.0";
           #endif
        }
    }

    juce::String HelpPanel::buildManualText()
    {
        juce::StringArray text;

        text.add ("TEXTURE " + versionString());
        text.add ("A drag-and-drop granular pad synthesizer built around one knob.");
        text.add ("");

        text.add ("=== QUICK START ===");
        text.add ("1. Drop any audio file onto the left panel, or click it to browse.");
        text.add ("2. Play a chord. The sample is assumed to be tuned to C3 (MIDI 60).");
        text.add ("3. Turn MORPH. That is the whole instrument.");
        text.add ("");
        text.add ("At MORPH 0% you hear near-clean playback of your sample with a hint of");
        text.add ("reverb. At 35% (the default) it is a lush pad. At 100% the source has");
        text.add ("dissolved into a diffuse ambient wash. Everything in between is a");
        text.add ("continuous blend, so automate it and the sound evolves with the track.");
        text.add ("");

        text.add ("=== THE WORKFLOW ===");
        text.add ("TEXTURE is deliberately small. There is no filter, no arpeggiator and no");
        text.add ("modulation matrix. The intended loop is: find a sample, set MORPH, shape");
        text.add ("the envelope, play. If a sound is not working, change the sample rather");
        text.add ("than hunting for a control. Dropping a new file swaps it instantly and");
        text.add ("keeps every other setting, which makes auditioning very fast.");
        text.add ("");

        text.add ("=== THE INTERFACE ===");
        text.add ("");
        text.add ("HEADER");
        text.add ("  LED            Output level. Grey is silence, white is near 0 dBFS,");
        text.add ("                 red means the output is clipping - lower the Gain knob.");
        text.add ("  TEXTURE        The wordmark. Look very closely at it...");
        text.add ("  < Preset >     The patch you are on. Click the name for the list, the");
        text.add ("                 arrows for the previous or next preset. An asterisk");
        text.add ("                 means unsaved edits.");
        text.add ("  File menu      Open, Save, Save As, load or clear the sample, Export");
        text.add ("                 audio, A/B copy, Options, Interface size (75-200%),");
        text.add ("                 and Open location in file browser.");
        text.add ("  A / B          Two parameter snapshots. Click a letter to switch,");
        text.add ("                 use the File menu to copy one into the other.");
        text.add ("  Randomize      New settings every press. Each press starts from");
        text.add ("                 defaults first, so results never drift.");
        text.add ("  Reset          Every control back to its default value.");
        text.add ("  Help           This page.");
        text.add ("  Gear           Settings: licence, folders, preferences, about.");
        text.add ("                 Preferences also includes Reduce interface motion.");
        text.add ("  FREE / PRO     Your licence tier. Click to open the licence panel.");
        text.add ("");
        text.add ("SAMPLE PANEL (left)");
        text.add ("  Drop an audio file (WAV, AIFF, FLAC, MP3, OGG) or a .texture preset.");
        text.add ("  Click the empty panel to browse. Right-click for load, clear and");
        text.add ("  reveal-in-file-browser. Once loaded you see the waveform, the file");
        text.add ("  name, its length, channel count and original sample rate, and a");
        text.add ("  magenta playhead showing where the engine is reading.");
        text.add ("  Click the waveform to set where new notes start reading (a thin");
        text.add ("  marker). Alt-click, or right-click > Reset start marker, clears it.");
        text.add ("  The marker is saved with your session and cleared by a new sample.");
        text.add ("  Files longer than 30 seconds are truncated. Files above 100 MB are");
        text.add ("  rejected. More than two channels are folded to the first two.");
        text.add ("");
        text.add ("MORPH (right)");
        text.add ("  The one knob. Drag vertically for fine control. Shift-drag is coarse");
        text.add ("  (three times faster), Ctrl/Cmd-drag is ultra-fine (ten times slower).");
        text.add ("  Double-click resets to 35%. Every knob in TEXTURE drags the same way.");
        text.add ("  Ctrl/Cmd-click steps through 0, 35, 70 and 100%. Right-click for");
        text.add ("  numeric entry, copy/paste and MIDI mapping.");
        text.add ("  MORPH simultaneously moves grain count, grain length, overlap,");
        text.add ("  position jitter, detune, stereo spread, reverb send, chorus depth,");
        text.add ("  saturation and a gentle high shelf.");
        text.add ("");
        text.add ("ENVELOPE (A D S R)");
        text.add ("  Attack, Decay, Sustain, Release for every voice. The long default");
        text.add ("  release of 1.2 s is what makes it a pad. Hover any knob for its value.");
        text.add ("");
        text.add ("REVERB");
        text.add ("  A pill toggle. On by default. When off the reverb is fully bypassed,");
        text.add ("  so the dry signal is untouched.");
        text.add ("");
        text.add ("GAIN");
        text.add ("  Output level, -24 dB to +6 dB, applied after the effects.");
        text.add ("");
        text.add ("LEVEL METER");
        text.add ("  Left and right output. Teal is quiet, orange is healthy, yellow is");
        text.add ("  close to clipping, red is over 0 dB. The thin line holds each peak");
        text.add ("  for 1.5 seconds. The number is the highest peak so far - click the");
        text.add ("  meter to reset it.");
        text.add ("");
        text.add ("DATA STREAM");
        text.add ("  The faint green text beside the LFO is a live feed of what TEXTURE is");
        text.add ("  doing: parameter changes, notes, controllers, voices and grains. It");
        text.add ("  stops when nothing is changing. It is there to look at, not to read.");
        text.add ("");
        text.add ("LFO (Pro)");
        text.add ("  A sine that offsets MORPH. Rate 0.05 to 8 Hz, or synced to host tempo");
        text.add ("  in note divisions. Depth 1 gives a swing of plus or minus 50% around");
        text.add ("  wherever MORPH is set. Host automation on MORPH always wins; the LFO");
        text.add ("  adds to it and folds softly at the ends of the range.");
        text.add ("");
        text.add ("LAYER 2 (Pro)");
        text.add ("  A second sample slot blended with the first. Both share one MORPH.");
        text.add ("");

        text.add ("=== RIGHT-CLICK ON ANY CONTROL ===");
        text.add ("  Reset to default");
        text.add ("  Enter value...");
        text.add ("  Copy value / Paste value");
        text.add ("  Learn MIDI CC - the knob pulses teal until you move a controller");
        text.add ("  With Reduce interface motion enabled, the learn ring stays steady");
        text.add ("  instead of pulsing and other optional UI animation is removed.");
        text.add ("  Clear MIDI mapping");
        text.add ("CC 64 (sustain), 120 and 123 keep their standard meaning and cannot be");
        text.add ("mapped. Mappings are saved globally, not per project. The LFO division");
        text.add ("dropdown offers reset and MIDI learn too.");
        text.add ("");

        text.add ("=== MIDI ===");
        text.add ("  Omni: all channels are accepted.");
        text.add ("  Velocity scales amplitude between 30% and 100%.");
        text.add ("  Pitch bend is plus or minus 2 semitones.");
        text.add ("  Sustain pedal (CC64) holds notes after key-up.");
        text.add ("  16 voices. The oldest, quietest voice is stolen when they run out.");
        text.add ("  TEXTURE has no sequencer, so it neither imports nor exports MIDI.");
        text.add ("");

        text.add ("=== KEYBOARD SHORTCUTS ===");
        text.add ("Click an empty part of the window first so TEXTURE has keyboard focus.");
        text.add ("  Ctrl/Cmd+S         Save (Save as for factory or unnamed presets)");
        text.add ("  Ctrl/Cmd+Shift+S   Save as");
        text.add ("  Ctrl/Cmd+O         Open a preset file");
        text.add ("  Ctrl/Cmd+R         Randomize");
        text.add ("  F1                 This help page");
        text.add ("  Esc                Close any open panel");
        text.add ("  Tab / Shift+Tab    Move between controls");
        text.add ("  Arrow keys         Nudge the focused knob");
        text.add ("");

        text.add ("=== EXPORT AUDIO (Pro) ===");
        text.add ("File > Export audio renders the current patch as a C3 hit followed by a");
        text.add ("second C3 hit with a four second tail. You choose the sample rate and");
        text.add ("bit depth. When it finishes, a dialog reports the file name, location,");
        text.add ("length and quality.");
        text.add ("");

        text.add ("=== PRESETS ===");
        text.add ("Presets are plain JSON files with a .texture extension, so they can be");
        text.add ("emailed or posted anywhere. They store the parameters and a reference to");
        text.add ("the sample - not the audio itself.");
        text.add ("");
        text.add ("Where they live:");
        text.add ("  Factory   <data folder>/Presets/Factory/<Category>/");
        text.add ("  Yours     <data folder>/Presets/User/<Category>/");
        text.add ("The data folder is:");
        text.add ("  Windows   %APPDATA%\\Texture\\");
        text.add ("  macOS     ~/Library/Application Support/Texture/");
        text.add ("  Linux     ~/.config/Texture/");
        text.add ("File > Open location in file browser, or the Open data folder button");
        text.add ("at the bottom of this page, takes you straight there.");
        text.add ("");
        text.add ("If a preset does not appear:");
        text.add ("  1. Check the extension is exactly .texture (not .texture.txt).");
        text.add ("  2. Put it under the Presets folder above - subfolders are scanned.");
        text.add ("  3. Open the browser and press Rescan.");
        text.add ("  4. A preset larger than 8 KB is skipped as malformed.");
        text.add ("If a preset loads but is silent, its sample reference could not be");
        text.add ("resolved. TEXTURE looks for the absolute path first, then inside the");
        text.add ("factory samples folder, then inside your preset folder. Drop the sample");
        text.add ("in again and re-save the preset to repair it.");
        text.add ("");

        text.add ("=== INSTALLING AND UNINSTALLING BY HAND ===");
        text.add ("If the installer did not work, copy the plugin yourself.");
        text.add ("");
        text.add ("Windows VST3:");
        text.add ("  C:\\Program Files\\Common Files\\VST3\\Texture.vst3");
        text.add ("macOS VST3:");
        text.add ("  /Library/Audio/Plug-Ins/VST3/Texture.vst3");
        text.add ("macOS AU:");
        text.add ("  /Library/Audio/Plug-Ins/Components/Texture.component");
        text.add ("Linux VST3:");
        text.add ("  ~/.vst3/Texture.vst3");
        text.add ("CLAP (all platforms) goes in the CLAP folder beside the VST3 one.");
        text.add ("");
        text.add ("To uninstall, delete that file, then delete the data folder listed");
        text.add ("above if you also want your presets, settings and licence gone.");
        text.add ("Rescan plugins in your DAW afterwards.");
        text.add ("");

        text.add ("=== TROUBLESHOOTING ===");
        text.add ("");
        text.add ("The plugin does not appear in my DAW");
        text.add ("  Rescan the plugin folder. Check you copied the 64-bit build into the");
        text.add ("  folder your DAW actually scans. On macOS, open the plugin once so");
        text.add ("  Gatekeeper can approve it.");
        text.add ("");
        text.add ("It loads but makes no sound");
        text.add ("  TEXTURE is silent until a sample is loaded - that is deliberate, never");
        text.add ("  a bug. Drop a file. Then check the Gain knob and the LED.");
        text.add ("");
        text.add ("A note hangs");
        text.add ("  Send All Notes Off or All Sound Off from your DAW. Reset only restores");
        text.add ("  control values; it is not a panic button. If your controller has a");
        text.add ("  stuck sustain pedal, CC64 is the usual cause.");
        text.add ("");
        text.add ("Crackles or high CPU");
        text.add ("  Raise your audio buffer size. MORPH near 100% with sixteen voices is");
        text.add ("  the heaviest case by a wide margin. The debug window shows live CPU.");
        text.add ("");
        text.add ("My sample sounds out of tune");
        text.add ("  Samples are assumed to be tuned to C3. Transpose in your DAW, or");
        text.add ("  re-pitch the file before dropping it in.");
        text.add ("");
        text.add ("Everything is broken");
        text.add ("  Help > Open debug window > Reset all settings to default clears preferences,");
        text.add ("  MIDI mappings, the licence and cached logs. It is destructive but it");
        text.add ("  fixes a corrupted install.");
        text.add ("");

        text.add ("=== FILES TO SEND SUPPORT ===");
        text.add ("Two different files, and a hard crash needs both.");
        text.add ("");
        text.add ("Troubleshooting file (Debug > Export troubleshooting file)");
        text.add ("  A light self-diagnostic: your settings, audio and MIDI configuration,");
        text.add ("  licence state, version, sample details and host information. Written");
        text.add ("  the moment you ask for it. Start here - it solves most minor issues,");
        text.add ("  and you can read it yourself.");
        text.add ("");
        text.add ("Crash log (Debug > Create log file on crash)");
        text.add ("  A tick box, off every time the plugin loads. Turn it on and a log is");
        text.add ("  started, named with the date and time. A copy of the troubleshooting");
        text.add ("  file is placed at the top of it, then every event is appended as it");
        text.add ("  happens, so whatever preceded a crash is on disk.");
        text.add ("");
        text.add ("Both land in the data folder listed above. If TEXTURE keeps crashing,");
        text.add ("send both files to " + juce::String (supportEmail) + " with a description of");
        text.add ("what you were doing, which DAW and which operating system.");
        text.add ("");

        text.add ("=== LICENCE ===");
        text.add ("TEXTURE source code and original project materials are proprietary.");
        text.add ("Third-party components remain under their own licences. Inter is under");
        text.add ("the SIL Open Font License and JUCE is used under its own terms.");
        text.add ("Activation is entirely offline - TEXTURE never contacts a server and");
        text.add ("collects no analytics of any kind.");
        text.add ("");

        text.add ("=== LINKS ===");
        text.add ("  Circuit Drift Labs  " + juce::String (circuitDriftUrl));
        text.add ("  Product site        " + juce::String (homepageUrl));
        text.add ("  Source              " + juce::String (githubUrl));
        text.add ("  Developer contact   " + juce::String (supportEmail));
        text.add ("");
        text.add ("TEXTURE " + versionString() + " - drop a sample, play a chord, it sounds finished.");

        return text.joinIntoString ("\n");
    }

    HelpPanel::HelpPanel (TextureAudioProcessor& processorToUse)
        : processor (processorToUse)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);

        body.setMultiLine (true, false);
        body.setReadOnly (true);
        body.setScrollbarsShown (true);
        body.setCaretVisible (false);
        body.setPopupMenuEnabled (true);
        body.setFont (TextureLookAndFeel::monoFont (12.0f));
        body.setColour (juce::TextEditor::backgroundColourId, colours::background);
        body.setColour (juce::TextEditor::outlineColourId, colours::panelEdge);
        body.setText (buildManualText(), false);
        body.setTitle ("TEXTURE manual");
        addAndMakeVisible (body);

        debugButton.setTooltip ("Open the live debug window: raw internals, crash logging "
                                "and the troubleshooting export");
        debugButton.onClick = [this] { if (onOpenDebugWindow) onOpenDebugWindow(); };
        addAndMakeVisible (debugButton);

        githubButton.setTooltip (githubUrl);
        githubButton.onClick = [] { juce::URL (githubUrl).launchInDefaultBrowser(); };
        addAndMakeVisible (githubButton);

        homepageButton.setTooltip (circuitDriftUrl);
        homepageButton.onClick = [] { juce::URL (circuitDriftUrl).launchInDefaultBrowser(); };
        addAndMakeVisible (homepageButton);

        supportButton.setTooltip (supportEmail);
        supportButton.onClick = []
        {
            juce::URL (juce::String ("mailto:") + supportEmail
                       + "?subject=TEXTURE%20" + versionString() + "%20contact")
                .launchInDefaultBrowser();
        };
        addAndMakeVisible (supportButton);

        dataFolderButton.setTooltip ("Open the folder holding presets, settings and logs");
        dataFolderButton.onClick = []
        {
            const auto directory = PresetManager::getPluginDataDirectory();
            directory.createDirectory();
            directory.revealToUser();
        };
        addAndMakeVisible (dataFolderButton);

        for (const auto* section : { "Quick start", "The workflow", "The interface", "Right-click",
                                     "MIDI", "Keyboard shortcuts", "Export audio", "Presets",
                                     "Installing", "Troubleshooting", "Files to send", "Licence", "Links" })
        {
            auto* button = sectionButtons.add (new juce::TextButton (section));
            button->setTooltip ("Jump to " + juce::String (section));
            button->onClick = [this, title = juce::String (section)] { scrollToSection (title); };
            addAndMakeVisible (button);
        }

        closeButton.setTooltip ("Close help (Esc)");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);
    }

    HelpPanel::~HelpPanel() = default;

    void HelpPanel::scrollToSection (const juce::String& sectionTitle)
    {
        const auto text = body.getText();
        const auto index = text.indexOfIgnoreCase ("=== " + sectionTitle.toUpperCase());

        if (index < 0)
            return;

        // Scroll to the end first so the section lands at the top, not the bottom.
        body.moveCaretToEnd();
        body.setCaretPosition (index);
        body.scrollEditorToPositionCaret (0, 0);
    }

    bool HelpPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onClose)
                onClose();

            return true;
        }

        return false;
    }

    void HelpPanel::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background);

        auto header = getLocalBounds().removeFromTop (34);

        g.setColour (colours::panel);
        g.fillRect (header);

        g.setColour (colours::panelEdge);
        g.fillRect (header.removeFromBottom (1));

        g.setColour (colours::textPrimary);
        g.setFont (TextureLookAndFeel::interfaceFont (14.0f, true));
        g.drawText ("HELP", header.reduced (layout::windowEdgePadding, 0),
                    juce::Justification::centredLeft, false);

        g.setColour (colours::textMuted);
        g.setFont (TextureLookAndFeel::monoFont (10.0f));
        g.drawText ("v" + versionString() + (processor.isPro() ? "  PRO" : "  FREE"),
                    header.reduced (layout::windowEdgePadding, 0),
                    juce::Justification::centredRight, false);
    }

    void HelpPanel::resized()
    {
        auto bounds = getLocalBounds();
        bounds.removeFromTop (34);
        bounds = bounds.reduced (layout::windowEdgePadding, layout::grid);

        auto buttonRow = bounds.removeFromBottom (28);
        closeButton.setBounds (buttonRow.removeFromRight (70));
        buttonRow.removeFromRight (layout::grid);

        const auto buttonWidth = juce::jmax (80, buttonRow.getWidth() / 5 - 4);

        debugButton.setBounds (buttonRow.removeFromLeft (juce::jmax (140, buttonWidth)));
        buttonRow.removeFromLeft (4);
        dataFolderButton.setBounds (buttonRow.removeFromLeft (buttonWidth));
        buttonRow.removeFromLeft (4);
        githubButton.setBounds (buttonRow.removeFromLeft (juce::jmin (80, buttonWidth)));
        buttonRow.removeFromLeft (4);
        homepageButton.setBounds (buttonRow.removeFromLeft (juce::jmin (90, buttonWidth)));
        buttonRow.removeFromLeft (4);
        supportButton.setBounds (buttonRow.removeFromLeft (juce::jmin (110, buttonWidth)));

        bounds.removeFromBottom (layout::grid);

        auto index = bounds.removeFromLeft (132);
        bounds.removeFromLeft (layout::grid);
        const auto rowHeight = juce::jmin (24, index.getHeight() / juce::jmax (1, sectionButtons.size()));

        for (auto* button : sectionButtons)
            button->setBounds (index.removeFromTop (rowHeight).reduced (0, 1));

        body.setBounds (bounds);
    }
}
