# Codebase Map: where to find what

| I want to... | Look in |
|---|---|
| Plugin processor, MIDI dispatch, state | `Source/PluginProcessor.*` |
| Editor window and scaling | `Source/PluginEditor.*` |
| Granular engine, grains, voices | `Source/Engine/` (`GranularEngine`, `Grain`, `Voice`, `WaveformMath.h`) |
| Effects | `Source/Effects/` (`Saturator`, `Chorus`, `Reverb`, `EffectsChain`) |
| Envelope and LFO | `Source/Modulation/` |
| Sample loading, WAV export, factory content | `Source/IO/` |
| Presets and browser | `Source/Presets/` |
| Panels, widgets, look and feel | `Source/GUI/` |
| Licence check (Pro) | `Source/License/` |
| MIDI learn, parameter IDs, state schema | `Source/Utility/` |
| Tests | `tests/` (16 files), CTest target `Texture_Tests` |
| Test/sample generator | `tools/generate_placeholder_samples.cpp` |
| Build and CI | `CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `.github/workflows/` |
| Product and release docs | `docs/` (`PRODUCT_SPEC.md`, `RELEASE_VALIDATION.md`, `development/`) |
