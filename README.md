# TEXTURE

**Drop a sample. Play a chord. It sounds finished.**

TEXTURE is a granular pad synthesizer that turns any audio file into a playable,
evolving instrument. The whole instrument is one large knob labelled **MORPH**,
which sweeps continuously from clean sample playback at 0% to a fully diffused
ambient wash at 100%. The default position, 35%, is a lush pad.

Ships as **VST3**, **AU** (macOS), **CLAP** (optional) and a **standalone** app.

---

## Table of contents

- [What it does](#what-it-does)
- [Quick start](#quick-start)
- [Building](#building)
- [Project layout](#project-layout)
- [Running the tests](#running-the-tests)
- [Where things live at runtime](#where-things-live-at-runtime)
- [Free vs Pro](#free-vs-pro)
- [Troubleshooting](#troubleshooting)
- [Design notes](#design-notes)
- [Licence](#licence)

---

## What it does

| | |
|---|---|
| **Engine** | 16-voice granular sampler, up to 8 grains per voice, linear-interpolated resampling |
| **MORPH** | One knob driving grain count, length, overlap, position jitter, detune, stereo spread, reverb send, chorus depth, saturation and a high-shelf lift |
| **Envelope** | One ADSR per voice, with a long 1.2 s default release |
| **Effects** | Saturator → chorus → reverb → high shelf → DC blocker → output gain, fixed order |
| **Input** | Drag and drop WAV, AIFF, FLAC, MP3, OGG, or a `.texture` preset |
| **MIDI** | Omni, velocity to amplitude, ±2 semitone pitch bend, sustain pedal, CC learn on every control |
| **Latency** | Zero. No lookahead, no oversampling. |

It deliberately has no filter, no arpeggiator, no wavetables, no modulation
matrix and no delay. Scope discipline is the product.

---

## Quick start

1. Drop any audio file onto the left panel, or click it to browse.
2. Play a chord. Samples are assumed to be tuned to **C3 (MIDI 60)**.
3. Turn MORPH.

That is the entire workflow. Press **F1** inside the plugin for the full manual,
which also covers troubleshooting and the debug tools.

---

## Building

### Requirements

| | |
|---|---|
| CMake | 3.22 or newer |
| C++ | C++20 |
| JUCE | 8.0.8 (fetched automatically, or point at a local checkout) |
| Windows | MSVC 2022, v143 toolset, Windows 10 SDK |
| macOS | Xcode 15+, deployment target 11.0, universal x86_64 + arm64 |
| Linux | gcc 12+ or clang 15+, plus the usual JUCE X11/ALSA/freetype dev packages |

### The short version

```bash
cmake --preset release
cmake --build --preset release
```

`COPY_PLUGIN_AFTER_BUILD` is on, so the VST3 lands in your system plugin folder
automatically.

### Using a local JUCE checkout

Fetching JUCE takes a while and needs a network. If you already have a checkout,
point the build at it and nothing is downloaded:

```bash
cmake -S . -B build/release -DTEXTURE_JUCE_PATH=/path/to/JUCE
cmake --build build/release --config Release
```

### Build options

| Option | Default | What it does |
|---|---|---|
| `TEXTURE_JUCE_PATH` | *(empty)* | Use a local JUCE checkout instead of fetching |
| `TEXTURE_JUCE_TAG` | `8.0.8` | Which JUCE tag to fetch |
| `TEXTURE_BUILD_TESTS` | `ON` | Build `Texture_Tests` and register it with CTest |
| `TEXTURE_BUILD_CLAP` | `OFF` | Also build a CLAP (fetches `clap-juce-extensions`) |
| `TEXTURE_ENABLE_ASAN` | `OFF` | AddressSanitizer + UBSan on clang/gcc. Never ship one. |
| `TEXTURE_WARNINGS_AS_ERRORS` | `ON` | `/WX` or `-Werror` on our own code only |
| `TEXTURE_CODESIGN_IDENTITY` | *(empty)* | Developer ID for macOS release builds |
| `TEXTURE_AAX_SDK_PATH` | *(empty)* | Path to Avid's AAX SDK; adds the AAX target |

Warnings are errors for TEXTURE's own sources and only for those. JUCE emits
warnings we do not own, so its targets are left alone.

### Presets

`CMakePresets.json` ships `debug`, `release`, `asan`, `release-clap`, and
per-platform `windows-release` / `macos-release` / `linux-release`.

### Low-memory machines

JUCE translation units are large. On a machine with under 8 GB of RAM, build with
`--parallel 2` (or `1`) rather than letting CMake pick; MSVC otherwise runs out of
address space linking several targets at once.

### Factory samples

Real curated content does not exist yet, so the twenty factory pads are
**generated**, not shipped: detuned major-triad drones from A2 to G4, thirty
seconds each. Thirty seconds of 48 kHz stereo is about 5.8 MB per file, so
bundling them would blow the 15 MB installer budget many times over.

- The build runs `generate_placeholder_samples`, which writes them into
  `factory_samples/` for developers.
- The plugin runs the same code on first launch, writing them into the user's
  factory samples folder so the bundled presets resolve.

Both paths share `Source/IO/FactoryContent.cpp`, so the two sets are identical.
Existing files are never overwritten — drop real samples in under the same names
and they survive every rebuild.

---

## Project layout

```
Texture/
├── CMakeLists.txt            Top-level build
├── CMakePresets.json         debug / release / asan / clap / per-platform
├── cmake/JuceFetch.cmake     Pinned JUCE, or a local checkout
├── Source/
│   ├── PluginProcessor.*     APVTS, MIDI dispatch, state, diagnostics
│   ├── PluginEditor.*        Window, look and feel, 3:2 scaling
│   ├── Engine/               SampleBuffer, Grain, Voice, GranularEngine
│   ├── Effects/              Saturator, Chorus, Reverb, EffectsChain, SecretEffect
│   ├── Modulation/           EnvelopeADSR, MorphLFO
│   ├── IO/                   SampleLoader, WavExporter, FactoryContent
│   ├── Presets/              PresetManager, PresetBrowser
│   ├── License/              LicenseManager
│   ├── GUI/                  MainPanel, MorphKnob, DropZone, WaveformDisplay,
│   │                         SettingsPanel, HelpPanel, DebugWindow, SecretPanel,
│   │                         Widgets, LookAndFeel, Colors
│   └── Utility/              ParameterIDs, DspHelpers, AtomicHelpers,
│                             Logging, MidiLearn
├── Resources/                Inter fonts, logo/icon
├── factory_samples/          Generated pads and the bundled presets
├── tests/                    juce::UnitTest suites, run by CTest
└── tools/                    generate_placeholder_samples
```

Four files sit outside the original manifest in `22_FILE_MANIFEST.md`, each for a
documented reason:

| File | Why |
|---|---|
| `Utility/Logging.*` | `16_ERROR_HANDLING.md` requires a rotating `FileLogger` with levelled, component-tagged lines. That is not a one-liner, and putting it in the processor would make it untestable. |
| `Utility/MidiLearn.*` | `include.md` requires MIDI mapping from every control's right-click menu. The table is read from the audio thread, so it needs its own lock-free home. |
| `IO/FactoryContent.*` | Shared by the build tool and the plugin, so the samples a developer gets are byte-identical to the ones a user gets. |
| `Effects/SecretEffect.*` | The hidden effect from `include.md`. Kept out of the APVTS so finding it never changes the host's automation list. |
| `GUI/Widgets.*`, `GUI/HelpPanel.*`, `GUI/DebugWindow.*`, `GUI/SecretPanel.*` | The help section, debug window, troubleshooting export and easter egg required by `include.md`, plus the shared controls that give every knob its right-click menu. |

`Utility/ParameterIDs.cpp` exists because `20_BEST_PRACTICES.md` forbids
executable code in headers, and the ID-to-display-name mapping cannot be
`constexpr`.

---

## Running the tests

```bash
cmake --build --preset release
ctest --preset release
```

Or run the binary directly for readable output:

```bash
./build/release/tests/Texture_Tests_artefacts/Release/Texture\ Tests
```

The suites cover the engine (silence without a sample, voice stealing, polyphony
ceiling, pitch bend, release, allocation-freedom, NaN handling, odd block sizes,
the full MORPH mapping table, LFO folding), the sample loader (every format,
truncation, downmixing, resampling, concurrent loads, waveform cache), the
licence manager (activation, rejection, normalisation, persistence, masking), the
MIDI-learn table, and the preset manager (round-trip, corrupt files, schema
guards, path resolution, filtering).

A second CTest entry, `texture_fuzz`, pushes random parameters, random MIDI and
random buffer sizes through the voice pool and the effects chain for 60 seconds
and asserts the output stays finite and bounded. It runs from a fixed seed, so a
failure reproduces. Shorten it while iterating, or lengthen it for a soak:

```bash
TEXTURE_FUZZ_SECONDS=5    ctest --preset release -R texture_fuzz
TEXTURE_FUZZ_SECONDS=3600 ctest --preset release -R texture_fuzz
```

`.github/workflows/build.yml` runs the configure, build and both CTest entries on
Windows/MSVC, macOS/clang and Linux/gcc for every push, plus a Linux CLAP build.

Plugin-in-DAW testing is manual. The full sweep — every control, every host, the
soak and the recovery paths — is `docs/MANUAL_TEST_PLAN.md`; the shorter
pre-release list it expands on is in `17_TESTING.md`.

---

## Where things live at runtime

| | |
|---|---|
| Windows | `%APPDATA%\Texture\` |
| macOS | `~/Library/Application Support/Texture/` |
| Linux | `~/.config/Texture/` |

```
Texture/
├── log.txt                 Session log, rotated at 1 MB, three kept
├── Texture.settings        Preferences, licence, MIDI mappings, window size
├── Presets/Factory/...     Bundled presets, by category
├── Presets/User/...        Yours
└── Samples/Factory/...     The twenty generated pads
```

Presets are plain JSON with a `.texture` extension, so they can be emailed or
posted anywhere. They reference a sample by path; they never embed audio.

---

## Free vs Pro

The free tier is the full engine, forever: no CPU limit, no watermark, no nag
screens, no time limit. Pro is a genuine upgrade, not a nag remover.

| | Free | Pro |
|---|---|---|
| Granular engine, MORPH, ADSR, reverb, gain | ✅ | ✅ |
| Drag and drop any sample | ✅ | ✅ |
| Factory samples | 20 | 500+ |
| Session recall | ✅ | ✅ |
| Preset browser | — | ✅ |
| Dual-layer | — | ✅ |
| MORPH LFO | — | ✅ |
| WAV export | — | ✅ |

Pro parameters exist in the parameter tree in both tiers. In the Free tier the
DSP ignores MORPH-LFO depth and Layer Blend while preserving their stored values,
so a Pro project can be opened and saved without destroying those settings and
they return immediately after Pro activation.

Activation is offline: the key is a base32 HMAC-SHA256 of your email. There is no
server, no phone-home, and no obfuscation — the check exists to keep honest users
honest.

> **Before release:** replace `PRO_LICENSE_SECRET` in
> `Source/License/LicenseManager.cpp`. Every build sharing that placeholder shares
> its keys.

---

## Troubleshooting

Press **F1** in the plugin, or read `Source/GUI/HelpPanel.cpp` — the manual lives
there as a single string, so it can never drift out of sync with the build.

Two files exist for support, and they do different jobs:

- **Troubleshooting file** — a snapshot written when you ask for it: settings,
  audio and MIDI configuration, licence state, sample details, host and version.
  Start here; it is readable and it solves most minor problems.
- **Crash log** — a running record, off every time the plugin loads. Tick
  *Create log file on crash* in the debug window and one is started, with a copy
  of the troubleshooting snapshot at its head.

Both land in the data folder above. A hard crash needs both.

The debug window also lives behind **shift-click on the gear icon**.

### Manual install

| Platform | Path |
|---|---|
| Windows VST3 | `C:\Program Files\Common Files\VST3\Texture.vst3` |
| macOS VST3 | `/Library/Audio/Plug-Ins/VST3/Texture.vst3` |
| macOS AU | `/Library/Audio/Plug-Ins/Components/Texture.component` |
| Linux VST3 | `~/.vst3/Texture.vst3` |

To uninstall, delete that file, then delete the data folder if you also want your
presets, settings and licence gone.

---

## Design notes

A few decisions worth knowing before reading the source.

**The grain window is a Tukey, not a pure Hann.** At MORPH 0 the spec asks for
one grain at a time with 0% overlap *and* for the result to sound like clean
playback. A true Hann window on a non-overlapped grain is a 50 Hz tremolo, not
clean playback. The taper therefore tracks the overlap: it collapses to a 2%
fade at MORPH 0 (continuous playback with no clicks) and opens to a full Hann as
the grains stack up. See `Grain::windowValue`.

**Nothing writes parameters from the audio thread.** A learned MIDI CC is
captured into an atomic in `processBlock` and applied on the message thread via
`AsyncUpdater`. A few milliseconds of latency on a knob gesture is free; a
`setValueNotifyingHost` call from the audio thread is not.

**Sample swaps are a pointer store.** `SampleLoader` produces a finished,
immutable `SampleBuffer` on a background thread; the message thread publishes it
with one atomic store and keeps its own `shared_ptr` so the old buffer's
destructor can never run on the audio thread.

**The layout never reflows.** `MainPanel` is laid out once at 720 × 480 and the
editor applies a uniform scale. Because the window is locked to 3:2, that is
exact at every size, and there is no second layout path to keep in sync.

**Everything is generated, nothing is bundled.** Factory samples, presets and the
icon are all produced by code in this repository. There is no binary content to
licence, audit, or accidentally infringe.

---

## Licence

TEXTURE source code is proprietary. Inter remains under the SIL Open Font License.
See [LICENSE](LICENSE), [LICENSE.md](LICENSE.md), and
[COPYRIGHT-TRADEMARK.md](COPYRIGHT-TRADEMARK.md).

TEXTURE makes no network connections. No telemetry, no analytics, no update
checks, no crash reporting.


## Required shared plug-in standard

This project follows the [Circuit Drift Labs Shared Audio Plugin Standard](docs/standards/CDL_PLUGIN_BASELINE.md). It is required for the plug-in target; standalone-only requirements apply only when a standalone target is included. The [product specification](docs/PRODUCT_SPEC.md) supplements the shared standard.
Current implementation status and exceptions are tracked in the
[baseline compliance record](docs/BASELINE_COMPLIANCE.md).
