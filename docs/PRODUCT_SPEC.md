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

The hidden GRIT controls are intentionally not host parameters. They are session-persisted product state and must not appear in the host automation list.

### Control gestures

TEXTURE uses one shared interaction contract for rotary controls:

- vertical drag changes value with the normal fine response;
- Shift-drag is coarse/faster;
- Ctrl/Cmd-drag is ultra-fine/slower;
- double-click resets to the documented default;
- right-click exposes the actions appropriate to the control;
- scalar parameter controls provide strict numeric entry, copy/paste and MIDI Learn;
- malformed or out-of-range numeric entry is rejected and must not be silently clamped;
- hidden GRIT controls provide reset and strict numeric entry but no MIDI Learn because they are deliberately outside host automation.

## 5. Motion and accessibility

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

## 6. State and files

Host project state is version-tagged. It includes APVTS parameter state plus non-parameter state needed to recreate the sound, including loaded sample references, start positions, preset identity, and GRIT settings. Validation must complete before any state is mutated. The detailed compatibility and rejection policy is defined in [STATE_COMPATIBILITY_SPEC.md](STATE_COMPATIBILITY_SPEC.md).

Preset load must be atomic from the user perspective: failed validation keeps the current sound. File loading and state-triggered sample reloads must not perform file I/O on the real-time audio callback.

## 7. Real-time requirements

The audio callback must remain free of file I/O, network I/O, blocking locks, UI calls, logging, and avoidable heap allocation. Cross-thread sample replacement must preserve lifetime until the audio thread has advanced beyond the old pointer. Outputs must remain finite under supported parameter values and hostile-but-parseable MIDI/event sequences.

## 8. Quality gates

Release evidence is tracked in [RELEASE_VALIDATION.md](RELEASE_VALIDATION.md).

Before beta:

1. CMake configure/build succeeds for every claimed CI target.
2. Unit tests and deterministic fuzz target pass.
3. The compliance record contains no undocumented baseline gaps.
4. The manual test plan is completed for at least one representative VST3 host and the standalone target.
5. Sample-rate changes, odd block sizes, state round-trip, preset failure behavior, MIDI edge cases and output finiteness are verified.
6. Known untested format/host combinations are stated rather than implied to be verified.

AAX and host-specific AU validation remain conditional until the required SDK/host is available.
