# TEXTURE — Product Specification

**Status:** Active product specification  
**Product:** TEXTURE  
**Shared baseline:** [Circuit Drift Labs Shared Audio Plugin Standard](standards/CDL_PLUGIN_BASELINE.md)  
**Compliance record:** [BASELINE_COMPLIANCE.md](BASELINE_COMPLIANCE.md)

## 1. Product profile

TEXTURE is a granular sample instrument. The following baseline profiles apply:

- **Instrument plug-in:** VST3 and AU, with optional CLAP and guarded AAX build support.
- **Standalone companion:** JUCE standalone target.
- **Offline renderer/exporter:** stereo WAV export from the current sound.

TEXTURE is not an audio effect plug-in and is not a MIDI effect.

## 2. Product purpose

The primary workflow is intentionally narrow: load one audio sample, play MIDI notes, and use **MORPH** to move continuously from recognizable sample playback toward a diffused granular pad. The default state should already be musically useful without requiring deep synthesis knowledge.

The instrument provides up to 16 voices, up to 8 grains per voice, ADSR, MORPH-dependent granular movement, optional MORPH LFO, a fixed effects chain, sample drag/drop, presets, MIDI Learn, A/B comparison, offline WAV export, diagnostics, Free/Pro gating, and a hidden GRIT effect.

## 3. Supported targets and host contract

- Formats: VST3, AU on macOS, optional CLAP, standalone; AAX build path is conditional on the Avid SDK.
- Audio output: mono or stereo host output is accepted where the processor reports support; internal rendering is stereo and folded safely for mono.
- MIDI: omni note input, velocity to amplitude, pitch bend, sustain pedal, all-notes-off, all-sound-off, and MIDI CC Learn.
- Tempo sync: when PPQ is available, the MORPH LFO phase follows host musical position across play, stop, seek and loop jumps. While stopped without PPQ it freezes; while playing without PPQ it free-runs from the last valid tempo. Invalid/missing tempo retains the last valid tempo, initially 120 BPM.
- Latency: zero samples.
- Tail: reported from the longest voice release plus reverb tail.
- Samples: WAV, AIFF, FLAC, MP3 and OGG through JUCE format readers.
- Presets: portable .texture preset files plus host project state.
- Network: no automatic telemetry, analytics, update checks, audio upload, preset upload, or diagnostic upload.

## 4. Primary controls

The stable host parameters are the IDs declared in `Source/Utility/ParameterIDs.h`. Their ranges, defaults and display names in code are authoritative and must remain synchronized with presets, GUI readouts, help, and host automation.

Core control groups:

- MORPH macro.
- ADSR attack, decay, sustain and release.
- Reverb enable and output gain.
- MORPH LFO rate, depth, sync and division.
- Dual-layer blend where the current licence tier permits it.
- Grain controls (all tiers, added in 0.0.1): TUNE (-24..+24 whole semitones, default 0), REVERSE (0..1 probability that a grain reads backwards, default 0) and SHIMMER (0..1; up to half of all grains play an octave up at -3 dB, default 0). All three default to off, so presets and host states saved before 0.0.1 restore unchanged; with REVERSE and SHIMMER at zero no extra random draws are taken, so rendering is bit-identical to earlier versions.

### Undo

Parameter edits are undoable (Ctrl/Cmd+Z; redo with Ctrl/Cmd+Shift+Z or Ctrl/Cmd+Y; also in the File menu). Every mouse gesture in the editor, preset load, reset, randomise and A/B recall begins a new undo step. Restoring host state clears the history. Sample files, sample start markers, GRIT and MIDI mappings are not parameters and are not part of the undo history.

The hidden GRIT controls are intentionally not host parameters. They are session-persisted product state and must not appear in the host automation list.

### Control gestures

TEXTURE uses one shared interaction contract for rotary controls:

- vertical drag changes value with the normal fine response;
- Shift-drag is coarse/faster;
- Ctrl/Cmd-drag is ultra-fine/slower;
- double-click resets to the documented default;
- right-click exposes the actions appropriate to the control;
- scalar parameter controls provide strict numeric entry, parameter-aware copy/paste and MIDI Learn;
- malformed or out-of-range numeric entry is rejected and must not be silently clamped;
- copied scalar values retain their parameter identity; Paste is enabled only for the same parameter ID and only while the value remains inside that control's valid range;
- hidden GRIT controls provide reset and strict numeric entry but no MIDI Learn because they are deliberately outside host automation.

## 5. MIDI and automation contract

- MIDI input is omni.
- Note velocity scales voice amplitude.
- Pitch bend range is plus/minus two semitones.
- CC64 retains sustain-pedal behavior.
- CC120/121/122/123/124/125/126/127 are reserved and cannot be learned.
- All Notes Off stops voices; All Sound Off stops voices and clears effect tails.
- MIDI Learn arms one target parameter at a time. The interface must show a persistent global learn state for every target type.
- Learn mode can be cancelled explicitly from a control context menu and automatically expires after 15 seconds.
- The next non-reserved CC received while learning becomes the mapping and is consumed by the learn operation rather than immediately changing the target.
- One parameter maps to at most one CC and one CC maps to at most one parameter. A new mapping replaces the previous mapping occupying either side of that relationship.
- Learned CC changes are transferred to the message thread and applied with `setValueNotifyingHost`; the audio callback never writes host parameters.
- MIDI CC and host automation operate on the same base parameter value. Whichever valid update is applied last wins. Internal MORPH LFO modulation is additive to the base parameter and does not overwrite it.
- Mappings are global user preferences, not host-project sound state.

## 6. Motion and accessibility

TEXTURE uses optional interface motion for control easing, MIDI-learn pulsing, panel transitions, transient button flashes, toast fades, and the background data stream. Preferences MUST provide **Reduce interface motion**.

When reduced motion is enabled:

- control value changes update visually without easing;
- MIDI-learn remains visibly indicated but does not pulse;
- settings panels open and close without sliding;
- data-stream updates do not glide;
- icon-button post-click flashes are disabled;
- toast messages remain readable but skip fade-in/fade-out motion;
- meters and other information-bearing live feedback remain active.

Reduced motion is a user preference, not part of host project state or sound state.

## 7. State and files

Host project state is version-tagged. It includes APVTS parameter state plus non-parameter state needed to recreate the sound, including loaded sample references, start positions, preset identity, and GRIT settings. Validation must complete before any state is mutated. The detailed compatibility and rejection policy is defined in [STATE_COMPATIBILITY_SPEC.md](STATE_COMPATIBILITY_SPEC.md).

Preset load must be atomic from the user perspective: failed validation keeps the current sound. Portable preset schema 2 stores both sample-layer references, both normalized start positions and all documented host parameters. Schema 1 presets remain readable by migrating the legacy `sample_path` field to Layer 1. Known parameter values are type-checked and range-checked before application or serialization; unknown parameter metadata is ignored rather than entering the active parameter set. Loading a preset makes its media state authoritative: an empty or missing reference leaves that layer empty rather than inheriting the previous patch. File loading and state-triggered sample reloads must not perform file I/O on the real-time audio callback.

Host state also persists both A/B parameter snapshots and the active comparison slot. Older host state without A/B data initializes both snapshots from the restored current parameter state.

## 8. Offline export

Pro WAV export reproduces the audible patch rather than only the first sample layer. The renderer captures both sample layers, per-layer start positions, dual-layer blend, the effective MORPH LFO rate/depth at export start, ADSR, TUNE, REVERSE, SHIMMER, reverb state, output gain and GRIT settings.

Export uses stereo WAV at the user-selected supported sample rate and integer bit depth. It renders two C3 notes using the documented hold/gap/tail pattern. Note events are split at exact sample positions rather than rounded to render-block boundaries. The offline LFO starts from phase zero for deterministic output. Export performs no normalization or limiting beyond the instrument's normal DSP chain and output-gain setting.

## 9. Real-time requirements

Reverb enable/disable transitions are crossfaded over 20 ms so bypass does not hard-switch between processed and dry audio. Output gain is smoothed over 30 ms; MORPH is smoothed over 20 ms. Effect-tail retirement uses elapsed audio time rather than callback count so idle optimization behaves consistently at different host block sizes. The audio callback must remain free of file I/O, network I/O, blocking locks, UI calls, logging, and avoidable heap allocation. Cross-thread sample replacement must preserve lifetime until the audio thread has advanced beyond the old pointer. Outputs must remain finite under supported parameter values and hostile-but-parseable MIDI/event sequences.

## 10. Quality gates

Release evidence is tracked in [RELEASE_VALIDATION.md](RELEASE_VALIDATION.md).

Before beta:

1. CMake configure/build succeeds for every claimed CI target.
2. Unit tests and deterministic fuzz target pass.
3. The compliance record contains no undocumented baseline gaps.
4. The manual test plan is completed for at least one representative VST3 host and the standalone target.
5. Sample-rate changes, odd block sizes, state round-trip, preset failure behavior, MIDI edge cases and output finiteness are verified.
6. Known untested format/host combinations are stated rather than implied to be verified.

AAX and host-specific AU validation remain conditional until the required SDK/host is available.
