# TEXTURE development specifications

26 feature specifications plus shared contracts and delivery guidance. Created 2026-10-20 from repository review at `38d9aec4556fc3b6594fe53888ea15d36698e43e`.

**Status: F01, F04 core, and F12 are implemented in the current development branch.** Their automated checks are included in the test suite; manual DAW and standalone validation remains in `../MANUAL_TEST_PLAN.md`. F04's optional layer-linking preference and note-detection worker remain deferred. The other 23 specifications remain proposals. F24 is explicitly research-gated; implementation must satisfy its prototype criteria before product integration.

## Start here
1. Read [Shared contracts](00_SHARED_CONTRACTS.md) for parameter precedence, backward compatibility, bounded DSP, state ownership and release gates.
2. Use [Delivery plan](DELIVERY_PLAN.md) for dependency order, module ownership and acceptance evidence.
3. Select a feature below and implement its smallest complete milestone, including UI, state, DSP and export.
4. Consult [Sources and audit](SOURCES_AND_AUDIT.md) for existing-code evidence and research boundaries.
5. Update the existing [progress](../../PROGRESS.md) and [release record](../RELEASE_VALIDATION.md) with observed results.

## Coverage and priority
Every recommendation from the preceding feature review has exactly one specification. Explicitly requested duplicates (such as locks and tuning) are consolidated. “Phase” is suggested delivery order, not a claim of scheduled work. Effort is qualitative; popularity has not been measured.

| ID | Feature specification | Benefit | Effort | Phase |
|---|---|---|---|---|
| F01 | [Audition button and Hold](F01_AUDITION_HOLD.md) | Usability | Easy–medium | Implemented |
| F02 | [Parameter locks](F02_PARAMETER_LOCKS.md) | Usability | Easy–medium | 1 |
| F03 | [Gentle Mutate](F03_GENTLE_MUTATE.md) | Exploration | Easy–medium | 1 |
| F04 | [Fine tuning and source-note controls](F04_FINE_TUNING_SOURCE_NOTE.md) | Musical accuracy | Medium | Core implemented |
| F05 | [Portable sample bundles and relinking](F05_PORTABLE_MEDIA.md) | Portability | Medium | 2 |
| F06 | [Configurable background WAV export](F06_BACKGROUND_EXPORT.md) | Export | Medium | 2 |
| F07 | [Independent tone and space controls](F07_TONE_SPACE.md) | Mix control | Medium | 2 |
| F08 | [Curated factory library](F08_CURATED_LIBRARY.md) | Content | Easy engineering; substantial sound design | 2 |
| F09 | [Variation history](F09_VARIATION_HISTORY.md) | Exploration | Medium | 2 |
| F10 | [Freeze and scan](F10_FREEZE_SCAN.md) | Sound design | Medium | 3 |
| F11 | [Mono, legato and glide](F11_MONO_LEGATO_GLIDE.md) | Performance | Medium | 2 |
| F12 | [Saved seed variation](F12_SAVED_SEED.md) | Reproducibility | Medium | Implemented |
| F13 | [Expandable grain controls](F13_GRAIN_PANEL.md) | Sound design | Medium | 3 |
| F14 | [Additional modulation shapes](F14_MODULATION_SHAPES.md) | Modulation | Medium | 3 |
| F15 | [Drag rendered audio to the DAW](F15_DRAG_AUDIO.md) | Workflow | Medium | 2 |
| F16 | [MORPH personalities](F16_MORPH_PERSONALITIES.md) | Product identity | Medium | 3 |
| F17 | [Attack-to-cloud](F17_ATTACK_TO_CLOUD.md) | Product identity | Medium–hard | 3 |
| F18 | [Sample journeys](F18_SAMPLE_JOURNEYS.md) | Movement | Medium–hard | 3 |
| F19 | [Rhythmic grain mode](F19_RHYTHMIC_GRAINS.md) | Rhythm | Medium–hard | 3 |
| F20 | [Texture family generator](F20_TEXTURE_FAMILY.md) | Exploration | Medium–hard | 3 |
| F21 | [Morph between sound snapshots](F21_SNAPSHOT_MORPH.md) | Performance | Hard | 4 |
| F22 | [Live audio capture](F22_LIVE_CAPTURE.md) | Capture | Hard | 4 |
| F23 | [MPE expression](F23_MPE.md) | Expression | Hard | 4 |
| F24 | [Spectral hybridization](F24_SPECTRAL_HYBRID.md) | Research | Very hard; research-gated | 5 |
| F25 | [Afterimage Loom effect](F25_AFTERIMAGE_LOOM.md) | New effect | Medium–hard | 3 |
| F26 | [Resonant Drift effect](F26_RESONANT_DRIFT.md) | New effect | Medium–hard | 3 |

## Two additional effect designs
[Afterimage Loom](F25_AFTERIMAGE_LOOM.md) reassembles recent instrument output into rhythmic, pitched and reversed fragment trails, with bounded feedback.
[Resonant Drift](F26_RESONANT_DRIFT.md) adds harmonic-to-metallic resonances with slow deterministic movement and optional note tracking.
Both default off and have explicit signal routing, control ranges, resource limits, persistence rules and audio tests. They are original product proposals built from established DSP concepts, not verified world-first inventions.

## Existing specifications reused
PRODUCT_SPEC.md remains the contract for the current application. STATE_COMPATIBILITY_SPEC.md remains authoritative for existing state; the shared contracts describe the proposed extension and migration required when implementing these features.
GUI_GAP_SPEC.md is a historical completed gap pass, not an unimplemented backlog.
RELEASE_VALIDATION.md and MANUAL_TEST_PLAN.md already cover release/host validation; this package adds feature-specific checks rather than duplicating those plans.
See `manifest.json` for a machine-readable feature/dependency index.
