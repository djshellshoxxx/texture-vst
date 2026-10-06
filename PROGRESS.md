# TEXTURE — BUILD PROGRESS

Crash-recovery file. Update after every milestone. **Read this first if resuming.**

- **Project root:** `C:/Users/new/Desktop/ssssssss/Texture`
- **Spec source:** `C:/Users/new/Desktop/ssssssss/*.md` (00–22, `include.md`, `theme.md`)
- **JUCE:** local shallow clone at `C:/Users/new/Desktop/JUCE_8_0_8` (tag 8.0.8),
  wired via `-DTEXTURE_JUCE_PATH=`; `cmake/JuceFetch.cmake` falls back to FetchContent.
- **Toolchain:** MSVC 2022 BuildTools 14.44, Windows SDK 10.0.26100, CMake 4.4.3, Ninja 1.13
- **Machine constraint:** 3.9 GB RAM total, ~0.6 GB free, 4 cores.
  **Always build with `--parallel 2` or fewer.** If the build dies with an
  out-of-memory or heap error, wait and retry with `--parallel 1`.

## Reproduce the build

```bash
cd C:/Users/new/Desktop/ssssssss/Texture
cmake -S . -B build/release -G "Visual Studio 17 2022" -A x64 \
      -DTEXTURE_JUCE_PATH="C:/Users/new/Desktop/JUCE_8_0_8"
cmake --build build/release --config Release --parallel 2
ctest --test-dir build/release -C Release --output-on-failure
```

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
| 17 | CDL baseline reconciliation: product spec, state hardening, controls, MIDI Learn, reduced motion | IN PROGRESS - implementation committed on `codex/spec-compliance-motion`; CI currently fails before any job steps on both this branch and its master base |
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
