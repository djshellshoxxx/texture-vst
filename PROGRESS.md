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
| 7 | First successful build (VST3 + Standalone, Windows) | IN PROGRESS |
| 8 | Unit tests green (ctest) | PENDING |
| 9 | GitHub repo created and pushed | PENDING |
| 10 | Full manual test sweep: every control, every combination | PENDING |
| 11 | Logic/workflow review of the GUI | PENDING |
| 12 | CLAP build (`-DTEXTURE_BUILD_CLAP=ON`) | PENDING |
| 13 | Linux / macOS / AU / AAX build paths verified in CMake | PENDING |

## Deviations from `22_FILE_MANIFEST.md` (all deliberate, all documented in README)

- `Source/Utility/Logging.*` — the rotating `FileLogger` required by `16_ERROR_HANDLING.md`
- `Source/Utility/MidiLearn.*` — CC mapping required by `include.md`, read from the audio thread
- `Source/Utility/ParameterIDs.cpp` — `20_BEST_PRACTICES.md` bans executable code in headers
- `Source/IO/FactoryContent.*` — shared by the build tool and the plugin's first-launch install
- `Source/Effects/SecretEffect.*` — the hidden effect from `include.md`, kept out of the APVTS
- `Source/GUI/Widgets.*`, `HelpPanel.*`, `DebugWindow.*`, `SecretPanel.*` — help, debug,
  troubleshooting and easter-egg features from `include.md`
- `tests/TestHelpers.h` — shared fixtures

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

## Log

- 2026-09-18: Read all 25 spec files. Created tree. Cloned JUCE 8.0.8.
- 2026-09-18: Wrote Utility, Engine, Effects, Modulation, IO, License, Presets. Commit 1.
- 2026-09-18: Wrote PluginProcessor, PresetManager, WavExporter, LookAndFeel, Widgets. Commit 2.
- 2026-09-18: Wrote the whole GUI layer, build system, tools, tests, icon, fonts, docs.
- 2026-09-18: CMake configure succeeded against local JUCE. First full build running.
