# TEXTURE — BUILD PROGRESS

Crash-recovery file. Update after every milestone. **Read this first if resuming.**

- **Project root:** the current repository checkout.
- **Specs:** checked-in root design documents and `docs/`.
- **JUCE:** fetched at the pinned revision by `cmake/JuceFetch.cmake`.
- **Builds:** use the platform CMake presets below. GitHub Actions builds Windows,
  universal macOS, and Linux on native runners.

## Reproduce the build

```bash
cmake --preset linux-release
cmake --build --preset linux-release --parallel 2
ctest --test-dir build/linux-release -C Release --output-on-failure
```

On Windows use `windows-release`; on macOS use `macos-release`.

## Milestones

| # | Milestone | Status |
|---|-----------|--------|
| 0 | Spec read, tree created, git init | DONE |
| 1 | Utility + Engine (SampleBuffer, Grain, Voice, GranularEngine) | DONE |
| 2 | Effects chain + Modulation (Saturator/Chorus/Reverb/ADSR/MorphLFO) | DONE |
| 3 | Processor: APVTS, MIDI, state save/load, A/B, randomize, diagnostics | DONE |
| 4 | IO (SampleLoader, WavExporter, FactoryContent), License, Presets | DONE |
| 5 | GUI (LookAndFeel, Widgets, MorphKnob, DropZone, Waveform, MainPanel, Settings, Help, Debug, Browser, Secret) | DONE |
| 6 | Build system, tools, icon, fonts, tests, README/LICENSE | DONE |
| 7 | First successful build (VST3 + Standalone, Windows) | DONE |
| 8 | Unit tests green (ctest) | DONE — 11053 checks, 0 failures |
| 9 | GitHub repo created and pushed | DONE — `djshellshoxxx/texture-vst`, private |
| 10 | Full manual test sweep: every control, every combination | PLAN WRITTEN, NEEDS A HUMAN |
| 11 | Logic/workflow review of the GUI | DONE — one finding, fixed |
| 12 | CLAP build (`-DTEXTURE_BUILD_CLAP=ON`) | DONE — `Texture.clap` built on MSVC |
| 13 | Linux / macOS / AU / AAX build paths verified in CMake | DONE for Windows, macOS and Linux; AAX unverified (no SDK) |
| 14 | CI: three-platform matrix in `.github/workflows/build.yml` | DONE — all four jobs green |
| 16 | GUI gap pass (`docs/GUI_GAP_SPEC.md`, B1-B6, M1-M14) | DONE - builds clean, ctest 100%, layout checked in standalone |
| 17 | CDL baseline reconciliation: product spec, state hardening, controls, MIDI Learn, reduced motion | DONE - merged to master in PR #1 (`3e78b7e9`) |
| 18 | v0.0.1 beta: merge post-merge branch, gcc build fixes, fuzz fix, Undo/Tune/Reverse/Shimmer, local release package validation | DONE |
| 15 | Fuzzer (`17_TESTING.md`) as its own `texture_fuzz` CTest entry | DONE |

## Deviations from `22_FILE_MANIFEST.md` (all deliberate, all documented in README)

- `Source/Utility/Logging.*` — the rotating `FileLogger` required by `16_ERROR_HANDLING.md`
- `Source/Utility/MidiLearn.*` — CC mapping required by `include.md`, read from the audio thread
- `Source/Utility/ParameterIDs.cpp` — `20_BEST_PRACTICES.md` bans executable code in headers
- `Source/Utility/MotionPreferences.h` — shared reduced-motion preference used by otherwise independent GUI components
- `Source/Utility/StateSchema.h` — pre-application host-state validation so malformed/future state is rejected atomically
- `Source/Utility/ControlValueParser.h` — strict numeric entry parsing and range validation
- `Source/Utility/ControlValueClipboard.h` — parameter-aware copy/paste validation
- `Source/IO/FactoryContent.*` — shared by the build tool and the plugin's first-launch install
- `Source/Effects/SecretEffect.*` — the hidden effect from `include.md`, kept out of the APVTS
- `Source/GUI/Widgets.*`, `HelpPanel.*`, `DebugWindow.*`, `SecretPanel.*` — help, debug,
  troubleshooting and easter-egg features from `include.md`
- `tests/TestHelpers.h` — shared fixtures
- `tests/test_fuzz.cpp` — the 60-second fuzzer `17_TESTING.md` asks for, as its own
  `texture_fuzz` CTest entry so its failures never read as unit-test failures
- `.github/workflows/build.yml` — the CI matrix `17_TESTING.md` asks for
- `docs/MANUAL_TEST_PLAN.md` — the pre-release sweep, expanded from the checklist in
  `17_TESTING.md` into something an operator can actually work through
- `docs/PRODUCT_SPEC.md`, `docs/BASELINE_COMPLIANCE.md`,
  `docs/STATE_COMPATIBILITY_SPEC.md`, `docs/RELEASE_VALIDATION.md` — product-level
  contracts and evidence required by the shared CDL baseline
- `tests/test_motion_preferences.cpp`, `test_state_schema.cpp`,
  `test_control_value_parser.cpp`, `test_control_value_clipboard.cpp`,
  `test_midi_learn.cpp` — regression coverage for the baseline reconciliation work

## Key design decisions (do not "fix" these without reading the reason)

1. **Tukey grain window, not pure Hann.** `06_MORPH_KNOB_BEHAVIOUR.md` wants 0% overlap
   *and* clean playback at MORPH 0. A Hann window with no overlap is a 50 Hz tremolo.
   The taper tracks the overlap: ~2% at MORPH 0, full Hann at MORPH 1. See `Grain::windowValue`.
2. **Factory samples are generated at runtime, not bundled.** 20 × 30 s stereo would be
   ~115 MB, against a 15 MB installer budget in `10_CONSTRAINTS.md`.
3. **No parameter is ever written from `processBlock`.** Learned CCs go through atomics
   and `AsyncUpdater`.
4. **MainPanel is laid out once at 720×480 and scaled.** The window is locked to 3:2,
   so the scale is exact and there is no second layout path.
5. **Secret effect (GRIT) lives outside the APVTS** so finding the easter egg never
   changes the host's automation list. It is persisted in the plugin state instead.
6. **`-Werror` is on our source files, never on the target.** JUCE compiles its
   module sources *into* our targets, so a target-level `-Werror` makes JUCE's own
   warnings fatal for us, which `18_BUILD_AND_PACKAGING.md` rules out. It also puts
   the flags last on the command line, after the INTERFACE options JUCE's
   recommended warning flags arrive as — which is the only way the
   `-Wno-implicit-int-conversion-on-negation` opt-out survives their `-Wconversion`.
   Check the generated project if you change this: our `.cpp` files should carry
   `TreatWarningAsError`, and no JUCE module source should.
7. **The sample hot-swap stores a raw pointer, not a `shared_ptr`.**
   `std::atomic<std::shared_ptr<T>>` does not exist in Apple's libc++ (the plugin
   would not build on macOS), and where it does exist it manipulates a reference
   count under a lock table on every load — a lock on the audio thread, and the
   opposite of the single atomic pointer store `05_SAMPLE_LOADER.md` specifies.
   Ownership stays on the message thread, and a retired buffer is freed only after
   `processBlock` has completed a whole block since the swap. Do not "simplify"
   `releaseRetiredIfSafe` away: resetting the old pointer immediately is a
   use-after-free.
8. **Factory presets are generated on first launch too**, by
   `PresetManager::installFactoryPresetsIfNeeded`, for the same reason as the samples
   and because they have to reference the generated sample paths. The
   `factory_samples/presets/<Category>/` folders in the repo are placeholders only.
9. **`PresetManager::load` tests `getDynamicObject()`, not `var::isObject()`.**
   juce::var reports an array as an object as well, so `isObject()` let a file
   containing `[1, 2, 3]` through and it then read every field as a default. This
   was a real bug, caught by `test_preset_manager`.
10. **A voice stops spawning grains when its envelope goes inactive**, not merely when
   it leaves the release phase. `isInReleasePhase()` returns false again once the
   release *completes*, so the original condition re-opened the gate on a dead voice
   and it never retired. Caught by `test_granular_engine`. See `Voice::renderAdding`.

## What is left

Latest review: see `docs/REVIEW_2026-10-20.md` for branch ancestry, waveform and
Randomize fixes, focused test evidence, and current CI/release discrepancies.

1. **Milestone 10, the manual sweep.** `docs/MANUAL_TEST_PLAN.md` is written and
   ready to work through, but it needs a human at a DAW with an audio device.
   Reaper is installed on the build machine and the VST3 is already in
   `C:/Program Files/Common Files/VST3/`, so that host is one double-click away.
   Ableton, FL and Logic are not on this machine.
2. **AAX.** The CMake path is written and guarded behind `TEXTURE_AAX_SDK_PATH`,
   but it has never been configured: the AAX SDK requires an Avid developer
   account, so nothing here has exercised it.
3. **AU.** Built by CI on macOS as part of the plugin, but never loaded in Logic.
4. The 24-hour soak and the CPU measurements in `15_PERFORMANCE_TARGETS.md` are
   part of the manual sweep and have not been run.

## Log

- 2026-09-18: Read all 25 spec files. Created tree. Cloned JUCE 8.0.8.
- 2026-09-18: Wrote Utility, Engine, Effects, Modulation, IO, License, Presets. Commit 1.
- 2026-09-18: Wrote PluginProcessor, PresetManager, WavExporter, LookAndFeel, Widgets. Commit 2.
- 2026-09-18: Wrote the whole GUI layer, build system, tools, tests, icon, fonts, docs.
- 2026-09-18: CMake configure succeeded against local JUCE. First full build running.
- 2026-09-18: **Milestone 7 done.** Fixed the first-compile breaks: four missing JUCE
  module includes, a forward-declared base class, a `copyFrom` overload that does not
  exist, two `const char* + const char*` concatenations, a shadowed `playHead`, three
  range-`for` loops over brace-lists of mixed pointer types that MSVC will not deduce,
  and `DebugPanel::Readout`, which was declared but never implemented. `Texture.vst3`
  and `Texture.exe` both build; JUCE's `juce_vst3_helper` loaded the built VST3
  out-of-process to generate its `moduleinfo.json`, which is the first real evidence
  the binary is a valid VST3.
- 2026-09-18: **Milestone 8 done.** ctest green: 11053 checks, 0 failures, plus the
  fuzzer. Getting there needed two genuine product fixes — see design decisions 7 and 8.
- 2026-09-18: Added the fuzzer, the CI matrix, the manual test plan, and the explicit
  tab order `09_GUI_SPEC.md` specifies and nothing had set.
- 2026-09-18: **Milestone 9 done.** `github.com/djshellshoxxx/texture-vst`, private,
  `master` pushed. The first CI run failed and was worth every second: a lambda
  parameter shadowing an enclosing `buffer` in `test_sample_loader.cpp` broke both
  macOS and Linux at `-Werror` while MSVC at `/W4` said nothing, and the
  `windows-release` preset pinned a Visual Studio version the runner no longer has.
- 2026-09-18: **Milestone 12 done.** `-DTEXTURE_BUILD_CLAP=ON` configures, fetches
  clap-juce-extensions and links `Texture.clap` on MSVC with no errors.
- 2026-09-18: **Milestones 13 and 14 done.** All four CI jobs green: Windows/MSVC,
  macOS/clang, Linux/gcc (each building the plugin and running both CTest entries)
  and the Linux CLAP build. Run 35377995851.
- 2026-09-18: CI earned its keep. Four rounds, each finding something MSVC at `/W4`
  is silent about: a lambda parameter shadowing an enclosing local; our
  `addListener`/`removeListener`/`processBlock` hiding the `juce::AudioProcessor`
  overloads the format wrappers call; three more parameter/member shadows; a
  `windows-release` preset pinned to a Visual Studio version the runner no longer
  has; `-Werror` leaking onto JUCE's own module sources; and finally
  `std::atomic<std::shared_ptr<T>>`, which Apple's libc++ does not implement, so the
  plugin could not be built for macOS at all. See design decisions 6 and 7 — the
  last two were real defects, not pedantry.
- 2026-09-18: **Milestone 11 done.** GUI reviewed against `09_GUI_SPEC.md`: window
  constraints, the 320 px settings slide-in, toast timings, the preset dropdown,
  waveform click-to-mark and the accessibility titles all match. The one real gap was
  the tab order, now `MainPanel::applyFocusOrder`.

- 2026-09-30: Audited GUI against specs/include.md + theme.md, wrote `docs/GUI_GAP_SPEC.md`, implemented all items. Spec sources 00-22 are no longer on disk; only include.md/theme.md remain.


- 2026-10-06: **Milestone 17 started.** Reconciled TEXTURE with the new shared CDL
  plugin baseline. Added `docs/PRODUCT_SPEC.md` and `docs/BASELINE_COMPLIANCE.md`,
  wrote the reduced-motion contract test first, then implemented a persistent
  Preferences toggle that removes nonessential knob easing, MIDI-learn pulsing,
  settings-panel sliding, data-stream glide, icon-button flash and toast fades while
  retaining information-bearing meters and state feedback. Also corrected the stale
  README licence summary. GitHub Actions currently reports all jobs failed before
  producing job steps/logs; the same no-step failure is present on base commit
  `f41cc7d`, so CI validation remains unresolved rather than recorded as a code
  failure or success.

- 2026-10-06: Continued milestone 17. Added atomic host-state validation before any
  APVTS/product state mutation, including rejection of future major versions,
  incomplete parameter state, out-of-range sample-layer indices and duplicate layer
  records. Added `docs/STATE_COMPATIBILITY_SPEC.md` and its regression suite.
  Tightened direct numeric entry so malformed/out-of-range text is rejected instead
  of silently clamped; replaced the raw-number control clipboard with a
  parameter-aware clipboard that rejects cross-parameter/out-of-range paste.
  Completed the MIDI Learn contract with a persistent global learn indicator,
  explicit cancel, 15-second timeout, deterministic timeout tests and documented
  MIDI/automation precedence. Updated Help/About to match current proprietary
  licensing, corrected the stuck-note Reset guidance, and linked Circuit Drift Labs.
  Reconciled the stale legacy `LICENSE.md` with the repository's proprietary
  `LICENSE`. Current CI execution remains unavailable for new test evidence because
  jobs fail before returning executable steps on both branch and base.

- 2026-10-06: MIDI audit found and fixed a persistence bug: `clearMappingFor()`
  removed a CC assignment only in memory, so the mapping could reappear after restart.
  Added a recreation regression test, persisted single-parameter clears, and adjusted
  remapping so the completed mapping table is written once instead of saving an
  intermediate empty state.

- 2026-10-06: **Post-merge development continued on `codex/post-merge-development`.**
  Hardened portable preset validation so known parameters are type/range checked on
  both load and save, invalid presets remain atomic, and unknown future metadata
  cannot enter the active parameter set. Defined deterministic synced-LFO behavior
  for stop, missing PPQ, seeks/loops and invalid tempo. Extended host-state validation
  to reject invalid sample start positions, duplicate singleton metadata and invalid
  GRIT payloads.
- 2026-10-06: Reworked offline WAV rendering so scheduled note events are split at
  exact sample positions instead of 512-sample boundaries. Export now captures both
  sample layers, sample-start positions, dual-layer blend, effective MORPH-LFO
  rate/depth and GRIT settings. The product spec now states that export does not
  normalize or limit beyond the normal signal chain.
- 2026-10-06: Reverb enable/disable now crossfades over 20 ms while its tail continues
  to evolve behind bypass, avoiding the previous hard processed/dry switch. Fixed
  EffectsChain startup so the 0 dB output-gain default begins at unity instead of
  unintentionally ramping from silence over the first 30 ms.

- 2026-10-06: Fixed effect-tail idle tracking. The old fixed 0.999-per-callback
  decay made idle duration depend on host block size and could keep the complete
  effects chain active for roughly a minute after audible output ended. Tail energy
  now decays from elapsed sample time with regression coverage comparing small and
  large host blocks.

- 2026-10-07: v0.0.1 beta. Merged `codex/post-merge-development` (PR #2) and removed
  the merged branches. That branch had never compiled on gcc: fixed seven compile
  errors (ambiguous `File = {}`, var/float ternaries, an ambiguous WavExporter
  overload, a hidden `Button::clicked`, `-Wfloat-equal`, a missing test stream
  operator) and a stale schema-version test. The 60 s fuzzer then found a real bug:
  an Inf sample entering the saturator left `tanh` as `1/tanh(drive)`, a spike of up
  to 100x full scale that rang through the reverb; the saturator now scrubs
  non-finite input. Added parameter Undo/Redo (usability), TUNE (value), REVERSE
  (fun) and SHIMMER (effect), each with unit tests and fuzz coverage. At that time,
  GitHub Actions had no available runners (jobs ended with runner id 0 and no steps),
  so the release binaries were built
  locally: Linux natively, Windows by cross-compiling with MinGW-w64.
- 2026-10-07 (later): v0.0.1 beta packages were validated locally. pluginval strictness 5
  passed on the Linux VST3. Windows binaries cross-built with clang-cl/libc++ passed the
  unit tests and fuzzer under Wine; the Windows GUI still needed a real Windows run.
  These packages were not published as an official GitHub Release; see
  `docs/RELEASE_VALIDATION.md` for current release evidence.


## 2026-10-20: Development specification package and implementation candidate

Created [26 feature specifications](docs/development/README.md) covering the feature roadmap and workflow/performance controls, including Afterimage Loom and Resonant Drift effect designs. Implemented F01 Audition and Hold, F04 per-layer source pitch calibration, and F12 deterministic seeded variation. F04's optional layer-link preference and automatic note detector remain deferred; the other 23 specifications remain proposals.

The source audit also fixed channel-specific MIDI note/sustain ownership, random-mode preset validation, truncated RIFF/WAV acceptance, source-pitch measurement coverage, and compile issues in random-stream defaults, state serialization, and export tests. The Linux Release configuration completed for VST3 and Standalone. CTest passed all three entries (109,332 unit assertions, the 60-second fuzz run, and waveform math). Native Windows and universal macOS release builds remain pending GitHub Actions/tag validation.

## 2026-10-21: F02 parameter locks and F03 Gentle Mutate

Implemented F02 editing locks and F03 Gentle Mutate as one feature branch. `LockPolicy` (lock registry, bounded lock set, action table, lock undo) and `MutationPlanner` (pure triangular mutation on a captured baseline, with a dedicated mutation lane in `VersionedRandomStreams`) live under `Source/Presets/`. Locks are per-instance host state in an optional `FeatureState` schema 1 block, validated by `StateSchema`; older states load with no locks. Randomize and Mutate honour locks; reset, automation, MIDI, restore and undo do not. Gentle Mutate defaults to 15% on Grain and Motion. A separate `EditToolsPanel` overlay holds Mutate, strength, group checkboxes and the lock list; locks are also set from control menus, group headers and a padlock glyph.

Evidence: the Linux Release build passes and `texture_unit_tests` reports 0 failures, including the new lock, planner and FeatureState suites. Not yet done: a manual DAW or standalone GUI pass, the 60-second fuzz run for this branch, processor-level undo and state round-trip checks (the test binary does not link the processor), preset-browse locking, and media locks.

## 2026-10-21: F07 independent tone and space

Implemented F07 core. Nine appended parameters (`tone_space_mode`, `tone_hp_hz`, `tone_lp_hz`, `tone_filter_on`, `tone_tilt_db`, `space_width`, `space_reverb`, `space_chorus`, `tone_drive`) default to Follow MORPH, so older presets and sessions sound as before. `ToneSpaceRouter` (pure) resolves the MORPH or independent targets and ramps between them; `Biquad` (allocation-free RBJ sections) provides the 12 dB/oct tone filter and the high shelf. `EffectsChain` runs filter, then saturator/chorus/reverb/GRIT, then width, then shelf. Controls live in a new "Tone and space" overlay with collapsible Tone and Space sections and "Start from current sound". Randomize leaves these controls alone; Gentle Mutate does not change them. Export renders the same settings.

Evidence: `texture_unit_tests` passes 120,819 checks. New suites: `test_tone_space` (routing, transitions, filter response, width, parity with Follow MORPH, stability across sample rates and block sizes, state migration, preset validation) and `test_legacy_render_fixtures` (archived poly and Follow MORPH renders, tolerance 1e-5).

Deviations and gaps:
- The 20 ms mode transition ramps the control values and the filter and width mix linearly. It is not a dual-path output crossfade, which would need a second effects chain.
- The high shelf was rewritten as an allocation-free biquad. The previous `makeHighShelf` call allocated on the audio thread whenever tilt changed. Its output matches the archived fixture within 1e-5 but is not bit-identical.
- Presets stay at schema 2: the new controls are ordinary parameters. The schema-3 features object is not needed for F07.
- No DAW, listening, CPU or 24-hour soak evidence yet. Tone and space are available on both tiers pending the release decision.
- Processor-level state migration is covered through the `StateSchema::missingParameterIDs` seam, not through a live processor test.

## 2026-10-21: F11 mono, legato and glide

Implemented F11 core. Four appended parameters: `voice_mode` (Poly default, Mono retrigger, Mono legato), `note_priority` (Last default, High, Low), `glide_ms` (0 to 2000 ms, logarithmic above zero, off by default) and `glide_mode` (Always, Overlap only default). Poly with zero glide is the default and keeps the golden fixtures within 1e-5.

`MonoNoteStack` (pure, 32 keys) tracks held keys by channel and note, counts repeated presses with a saturating count, retains released keys under the sustain pedal, and selects by priority. `decideMonoLine` (pure) chooses between release, retarget under the same envelope, and restart. `GranularEngine` owns the line. Glide, a 5 ms velocity ramp and a 10 ms retire fade are in `Voice`. Mode changes adopt the sounding key's voice rather than striking it again, and leave held keys sounding as poly notes. Glide is pitch-stepped every 16 samples, and grains follow the pitch. Export uses the same configuration. The performance section is in the Tone, space and performance overlay. The lock registry gains a Performance group (appended as group 6). Randomize leaves these controls alone.

Evidence: `texture_unit_tests` passes 121,854 checks. `test_mono_legato` covers the stack, the decision rules, glide arithmetic, all release orders of three keys, priorities, repeated keys, channel ownership, sustain, zero-velocity note-ons, glide endpoints and duration, mode transitions, panic, audition isolation, and a seeded 4000-event stream that must end with no sounding note.

Gaps:
- Overlap only glides a press that lands on a held key. A fallback to a held key after release does not glide in that mode. This is a reading of the spec's "overlap" and is recorded here, not decided with the user.
- Glide is applied to grains in 16-sample steps, not per sample.
- No MPE interaction (F23). Audition uses the polyphonic pool in every mode.
- The stack holds 32 keys per engine; beyond that the oldest released key, then the oldest held key, is evicted.
- No DAW, listening or CPU evidence. Performance controls are available on both tiers pending the release decision.

## 2026-10-21: F09 variation history

Implemented F09 core. Randomize and Mutate record the sound they produce. `VariationHistory` (pure, 32 entries, eight pins, cursor walking) evicts the oldest unpinned entry first, and keeps retained sample audio within 256 MiB, counting each buffer once. Entries hold a `SoundSnapshot`: every parameter, both sample references with starts and file identities, and the seed. Media is shared by immutable reference. `VariationPreview` (pure) applies a variation as one undo step named "Preview variation". Cancel undoes that step exactly; accept keeps the sound. Moving between variations cancels the earlier preview first. Snapshots are validated with the preset load rules (`PresetManager::isValidParameterValue`). History is session-local: a project restore clears it. The overlay gains a Variations section with previous and next, accept and cancel, clear, a retained-media readout and a list with Star (preview, then the normal Save dialog) and Pin.

Evidence: `texture_unit_tests` covers the history, pins, cursor, the preview state machine with a fake host, shared-media accounting and the range rules (`test_variation_history`). The plugin target builds.

Gaps:
- The processor glue (apply, restore, budget eviction, processor destruction while previewing, and host restore) has no automated test: the test binary does not link the processor. Those paths were reviewed, not run.
- If the user edits after a preview, Cancel keeps the preview rather than reverting the edit, and says so.
- Undoing a preview leaves it on the redo stack, so redo can bring it back. A later edit clears that.
- Accepted Family candidates (F20) are not implemented, so they do not record variations.
- No DAW or keyboard/accessibility pass for the new controls yet.


## 2026-10-21: F05 portable sample bundles and relinking

Implemented F05 core. A `.texturepack` is a ZIP of `manifest.json`, `preset.texture` and `media/<sha256>.<ext>`, written through `AtomicFileWriter` (temp file, size check, then rename). `BundleFormat`/`MediaManifest` (pure) define the fixed entry names, the path policy and strict manifest validation. `BundleArchive` reads with hard limits: 256 MiB archive, 512 MiB expanded, 16 entries, 64 KiB manifest, 8 KiB preset, 100 MiB per audio file, 30 s per layer. Sizes are enforced while streaming; every audio file is hashed and its decoded header compared with the manifest; output goes only into a fresh staging folder under names the reader generates. `BundleImportPlanner` validates preset against manifest and computes library paths before anything is committed; `MediaStore` is content-addressed and never deletes. `MediaRelinker` searches one user-chosen folder (depth 4, 5000 files, 64 hashes, no links, no roots), with content matches ranked above filename-only matches. Presets gain an optional per-layer `sha256` key (schema 2, additive; older readers ignore it). `PortableMediaWorker` runs jobs off the message thread, one at a time. Export of bundles is Pro-gated, matching WAV export; import is open to all tiers. This tier split is a provisional release decision.

Evidence: `texture_unit_tests` covers path policy, malformed manifests, traversal and absolute names, duplicate and unexpected entries, hash and size and format mismatches, every limit, truncated and non-zip files, staging cleanup, planner disagreement, write and round-trip, a two-layer shared file, dedupe, collision-free presets, atomic replace and abandon, content store, and relink ranking and depth.

Gaps:
- Processor glue (`relinkLayer`, `captureBundleSource`, and the missing-sample hook) and the dialogs are not automated: the test binary does not link the processor. The plugin target builds.
- Negative-path tests were not mutation-checked against a deliberately broken guard.
- Relink shows no audition for a filename-only match, and it takes the first candidate without a picker.
- Symlink rejection relies on JUCE's `ZipEntry::isSymbolicLink`; no test archive exercises it.
- Stale staging folders from a crash are not swept at startup.
- Bundle preset limit is still the schema-2 8 KiB; the 256 KiB schema-3 ceiling is not implemented.
- Normal user presets carry no content hash, so relinking them is by chosen file or filename only.
- No Windows, macOS or DAW verification of bundle round trips, and no help-panel text yet.

## 2026-10-21: F06 configurable background WAV export

Implemented F06 core. The export window offers Legacy two hits (the original schedule, unchanged), Single note, Chord (F01 shapes, now shared through `Engine/AuditionChords.h`) and Repeated phrase, with note, velocity, hold, tail, repeats and gap controls. `ExportPlanner` (pure) computes exact sample boundaries and rejects, rather than clamps, out-of-range values and renders over 120 s. `PatchSnapshot` is copied on the message thread. `ExportJob` renders with private engines and effects, checks cancellation before every 512-sample block, writes through `AtomicFileWriter`, reads the header back, and publishes only a complete file. `ExportController` (owned by the processor) runs one job at a time, exposes an atomic progress counter, keeps the result until collected, and cancels and joins on destruction, so closing the window or removing the plugin does not leave a partial file. Clipping is counted and reported; no limiter is added. The legacy `WavExporter::render` now delegates to the same job.

Evidence: `test_export_planner` covers legacy boundaries (exact integers), the 60 s legacy cap, single-note and phrase timing, exact hit positions, rate scaling, retrigger ordering, the 120 s rejection, and every range check. `test_export_job` covers planned length and peaks, byte-identical fixed-seed renders, failure to create a destination, cancellation before and during a render with no partial file and an existing destination kept, clip counting, controller single-job behaviour, cancel-then-restart, destruction during a render, and refused requests. Legacy output is byte-identical to the pre-F06 build (9ee9f55) for a stereo 48 kHz/24-bit render and a mono 44.1 kHz/16-bit render. The golden comparison was run against that build and is not committed, because a stored hash would depend on the toolchain.

Gaps:
- The dialog, progress polling and processor wiring are not automated (the test binary does not link the processor or GUI).
- The Help panel still describes only the legacy export.
- Live versus offline DSP comparison on identical timelines is not done.
- Rendering on a worker is not measured against the 250 ms cancel budget on slow storage; the per-block check is the only bound.
- No DAW or platform verification; no 24-hour or callback-budget measurement.

## 2026-10-21: F15 drag rendered audio to the DAW

Implemented F15 core. `RenderedAudioCache` (pure) owns the application-managed folder, the leases and the 2 GiB cap. Names are sanitised and carry a content ID (the first 8 hex of SHA-256); a finished render that duplicates stored audio is reused, and a different file is never overwritten. The only deletion path is `clearUnleased()`, a user action that skips leased files and touches only `.wav` files in the cache folder. The export folder is a user setting. `RenderedAudioTile` shows the last finished render with a waveform, starts an operating-system file drag only from a complete, existing file, and holds a lease until the drag ends. "Render for drag" in the export window renders the current configuration into the cache. The naming and hashing run on the worker through the controller's post-success hook. The processor collects every finished render on its own timer, so the drag tile updates even when the window is closed. Clearing and folder changes are refused while a render runs.

Evidence: `test_rendered_audio_cache` covers unique and sanitised reservations, Unicode folders, content-ID naming, reuse of identical audio, leases and their release, the cap and its message, the rule that nothing is deleted implicitly, the deletion boundary, folder changes, and missing files. `test_export_job` covers the post-success hook ordering. The plugin target builds.

Gaps:
- No drag has been verified in any DAW or on Windows, macOS or Linux desktop environments. Manual host checks with screenshots and imported duration and channel count are still required.
- Cancelled drags, dropped drags and editor closure during a drag are not automated.
- The cache cap is fixed at 2 GiB; it is not a user setting.
- A crash during a render can leave a `.partial` file; no startup sweep exists yet.
- The Help panel does not describe drag renders.
- Processor-level dispatch is not automated (the test binary does not link the processor).

## 2026-10-21: F13 expandable grain controls

Implemented F13 core. Five manual grain controls (`grain_size_ms`, `grain_density_hz`, `grain_jitter`, `grain_detune_cents`, `grain_pan_spread`), each with a `*_follow` switch, appended as ten parameters. Follow defaults to on, so every patch renders as before. `GrainTargetResolver` (pure) turns the Follow flags and manual values into the `MorphTargets` the voices and export use, once per block. A manual density spawns at `1 / density` and derives the overlap from duration. A manual duration keeps the legacy cadence. Jitter, detune and spread ramp over 20 ms; size and density apply from the next block. Turning Follow off copies the effective value into the manual control first, as one undo step. Gentle Mutate may move a manual grain value only while its Follow flag is off. The grain controls sit in a collapsed "Grains" section of the sound overlay, which reports the pool status. The grain fields are in the lock registry as group Grain. The Help panel describes them.

Evidence: `texture_unit_tests` passes 125,272 checks. `test_grain_controls` covers Follow-all equal to the MORPH targets across the whole range, every Follow combination, duration-only and density-only cadence, clamping and non-finite input, the pool limit, the 20 ms ramp and its settling, block-partition invariance of the follow path, the follow-change plan and the continuity of taking a field from Follow to manual, Mutate eligibility, lock groups, preset ranges, the migration default, measured spawn counts (10 Hz gives exactly 20 spawns in two seconds; 200 Hz with 500 ms grains never exceeds 8 active grains), and a pinned manual-path render. The legacy render fixtures are unchanged. The preset round-trip fixture gained the F13 values. The export test shows Follow renders are reproducible and manual values change the file. In a standalone build under Xvfb, the Grains section expands, the readouts match the MORPH values, and turning Size's Follow off keeps the readout at 66 ms.

Deviations and gaps:
- The pinned fixture and the spawn-count tests come from this build. They pin the implementation, not an independent reference.
- Smoothing is per block, so a manual value that is ramping can render slightly differently at a different host block size or in export. Follow-all is exact.
- The pool is still 8 grains per voice. Oversubscription skips spawns deterministically and never steals; no larger pool was added.
- Processor glue (`setGrainFollow`, effective-value publication, the mutation gate in `buildMutationFields`) is reviewed and checked in the standalone, not in an automated test, because the test binary does not link the processor.
- A/B and variation history capture these parameters through the existing all-parameter paths. Those paths are not separately tested for grain values.
- Presets stay at schema 2: the controls are ordinary parameters, as with F07.
- No DAW, listening, CPU, keyboard or accessibility pass yet. Commercial tier assignment is unchanged; these controls are on both tiers pending the release decision.

