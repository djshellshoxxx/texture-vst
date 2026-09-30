# TEXTURE — GUI gap spec (2026-09-30)

Audit of the shipped GUI against `specs/include.md` and `specs/theme.md`.
Everything already present (help, debug window, easter egg, file menu, A/B,
randomize, reset, drag and drop, export dialog, LED, signature notch, version
footer, right-click menus on knobs/toggles) is not repeated here. Items below
are what is missing or broken, and what gets built.

## Broken (functional bugs)

| # | Gap | Fix |
|---|-----|-----|
| B1 | Preferences > "Show tooltips" toggle has no handler: it is neither saved nor applied (include.md, Options). | Save on click, apply live via editor callback. |
| B2 | Preferences > "Default output gain" is saved but never read. | Processor applies it to new instances on construction. |
| B3 | Preferences > sample folder is saved but never used. | Chooser starts at the last folder used, else the Preferences sample folder, else the factory pads. |
| B4 | Waveform click-to-mark draws a start marker the engine ignores. | Make it real: per-layer sample start, used by voices on note-on, persisted in plugin state, reset when a new sample loads. |
| B5 | Data stream (theme.md) cycles six line types every tick, so it never stops scrolling when idle. | Emit a line only when the underlying value changes; scroll smoothly. |
| B6 | Help text points to menus that do not exist ("Settings > Debug", "Settings > Preferences > Open data folder"). | Correct the paths. |

## Missing (spec items not implemented)

| # | Spec | Gap | Build |
|---|------|-----|-------|
| M1 | theme.md Meters | No level meter at all. | Horizontal stereo meter in the Output section: smooth teal→orange→yellow→red gradient, 1 px peak hold (1.5 s, then 20 dB/s), mono peak readout top-right. Processor exposes lock-free per-channel peaks. |
| M2 | theme.md Header | Header is 40 px with a 22 px wordmark. | 32 px strip, 14 px semibold name. |
| M3 | theme.md Interaction | Value changes jump. | Knobs animate external changes (preset, reset, randomize, A/B, automation) over 80 ms ease-out. |
| M4 | theme.md Interaction | No hover brighten, no resize cursor. | Knob brightens ~8 % on hover; vertical-resize cursor on knobs. |
| M5 | theme.md Interaction | Shift = finer (spec: Shift = coarse, Ctrl/Cmd = ultra-fine). JUCE's velocity-mode swap also hijacks Ctrl. | Normal fine drag, Shift coarse, Ctrl/Cmd ultra-fine; velocity swap disabled. Help/tooltips updated. |
| M6 | theme.md Buttons | Momentary press flash exists only while held. | 100 ms accent flash after each header icon click. |
| M7 | theme.md Sliders | Linear slider (Settings default gain) uses the stock JUCE look. | Themed track/fill/thumb/ticks. |
| M8 | include.md Right-click | LFO division dropdown and the hidden GRIT knobs have no right-click menu. | Division: reset + MIDI learn. GRIT: reset + enter value (not a parameter, so no MIDI learn — stated in the menu). |
| M9 | include.md Hover tooltips | GRIT knobs show no value on hover. | Hover/drag value readout under each GRIT knob. |
| M10 | include.md Help | Manual is one long wall of text. | Section index beside the text that jumps to each section; add a keyboard-shortcut section. |
| M11 | include.md Right-click | No visual feedback while MIDI learn is armed. | Pulsing teal ring on the knob waiting for a CC. |
| M12 | include.md Debug | Debug window shows live state but not "data as features are invoked". | Scrolling event history (parameter changes, notes, CCs, preset/sample loads). |
| M13 | UX | File menu cannot load/clear a sample, shows no shortcuts, and has no interface size option. | Add Load/Clear sample, shortcut hints, Interface size submenu (75–200 %). |
| M14 | theme.md Header ("preset selector") | Stepping presets requires opening the menu. | Previous/next arrows beside the preset name. |

## Deliberately not changed

- Two re-tinted accents (MORPH blue→magenta). The product's MORPH ring is the identity; teal stays for Pro/mod.
- Rounded window corners: a plugin window is owned by the host.
- MIDI import/export: TEXTURE has no sequencer (include.md says skip).
- Mono font: system monospace is used; no JetBrains Mono binary is available offline.
