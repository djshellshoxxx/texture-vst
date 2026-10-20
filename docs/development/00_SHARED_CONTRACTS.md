# Shared implementation contracts
Status: Proposed specification, 2026-10-20. No feature in this directory is implemented by this documentation change.
Baseline: master 38d9aec4556fc3b6594fe53888ea15d36698e43e.
Read [the index](README.md), [product specification](../PRODUCT_SPEC.md), [state contract](../STATE_COMPATIBILITY_SPEC.md), and [CDL baseline](../standards/CDL_PLUGIN_BASELINE.md).

## Scope and ownership
This package covers all 24 recommendations from the feature review plus two new effects. Each feature is independently deliverable, with explicit dependencies. Existing functionality must be extended rather than duplicated. Proposed names and parameter IDs are contracts for implementation review, not a claim that the parameters exist.
Keep the current default view simple. Advanced areas use labelled collapsible tabs/sections, retain a visible collapsed strip, and scroll when needed. Controls retain usable physical sizes; window resizing distributes space. Check 1280x720 and 1920x1080 at 100%, 125%, 150% and 200% OS scaling, using available logical work area. All essential controls must remain reachable by scrolling or keyboard. Do not add a new global layout rewrite as a prerequisite for unrelated DSP work.

## Module boundary
Use small modules under Source/Engine, Source/Modulation, Source/Effects, Source/IO, Source/Presets and Source/GUI. Pure planning/math objects must not depend on the editor or AudioProcessor. Processor coordinates parameter snapshots, commands and lifetime; it must not accumulate feature algorithms. DSP has prepare/reset/process-style interfaces, no UI calls. File and analysis workers produce immutable results, published with an explicit ownership/retirement protocol. Register every module and its tests in a maintained module index when implemented.

## Real-time contract
Audio processing performs no file/network operations, blocking locks, logging, heap allocation, destruction of large objects or GUI notification. Preallocate using a documented maximum; split oversized processing spans without dropping audio. Bound event, voice, grain and effect counts. GUI commands use bounded queues; use single-producer/single-consumer queues only when ownership actually meets that constraint. A separate atomic emergency-stop flag must work even when the ordinary command queue is full. Deallocate retired buffers off the audio thread after readers have acknowledged retirement.
Finite-value checks apply before coefficients, array indexes and integer conversions. Invalid persisted values reject the whole proposed state; defensive runtime values use a documented safe fallback. Tests include mono/stereo, zero/odd/variable blocks, 44.1/48/88.2/96/192 kHz and prepare/release cycles.

## Parameter and control precedence
Existing host IDs, ranges, choice ordering and defaults remain stable. Append IDs; never repurpose tune or layer_blend. Document smoothing, units, default, state ownership and automation for each new field.
The processing order is:
1. Host parameter base values (including explicit edits and learned CCs).
2. Global MORPH modulation -> personality mapping -> derived grain/effect targets. Journey MORPH offsets create separate per-voice grain targets as defined in F18; they never change shared effect targets.
3. Advanced grain overrides and independent tone/space overrides.
4. Sample-journey position contribution and per-note MPE offsets.
5. Final bounds, smoothing and DSP.
Snapshots supply base values through a separate internal overlay, never by writing APVTS from the audio thread. Snapshot details are defined in F21. Locks affect named user actions, never ongoing automation, MIDI or internal modulation.
Every audible feature enters the shared RenderPatch snapshot so offline and live paths use the same DSP and configuration. The existing block-rate MORPH path remains unchanged in Legacy mode. Newly deterministic features cannot claim whole-plugin block-size invariance while legacy modulation is block-rate.

## Compatibility and state ownership
Neutral defaults must preserve old presets, including the legacy random stream. Add tests rendering archived fixtures before and after each change. Bit identity is required for the same binary/toolchain configuration with new paths bypassed; use numeric error limits for cross-platform comparisons.
Proposed portable preset schema 3 extends schema 2 with a bounded features object. Read schemas 1/2, supply neutral defaults, and keep their existing sample semantics. Old apps may reject schema 3; never promise reverse compatibility. Raise the existing 8 KiB limit deliberately to 256 KiB for schema 3, retain bounded strings/arrays and validate the entire document before applying it. Compressed preset packages use the limits in F05.
Host root stays TexturePluginState; add optional FeatureState schema 1. Keep pluginVersion interpretation intact; do not substitute preset schema numbers for product versions. Missing FeatureState means neutral defaults. Reject unsupported FeatureState versions in new readers. Older binaries may ignore unknown fields and cannot be expected to reproduce new sounds.
Persist sound controls, seed and bounded curves in host state and schema-3 presets. Persist per-instance editing locks in host state only. Store global UI preferences in user settings. Never restore active audition notes, Hold, capture-armed state, job execution, effect delay histories or modal tails.
User actions apply as one undoable transaction with a coherent immutable sound snapshot. Extend undo for sample references and feature metadata where the feature requires it; do not silently imply existing parameter-only undo already covers them. A/B must include new sound state, with migration from existing parameter-only slots.

## Defaults, licensing and resource budgets
Do not silently change Free/Pro entitlements. Capability decisions remain centralized in LicenseManager. Implement each feature so its enabled state is explicit; commercial tier assignment is a release decision, and tests exercise permitted and denied capabilities. Disabled capabilities must have documented load behaviour, not corrupt state.
No network access is needed for these features. Resource budgets below are proposed acceptance targets, not benchmark claims. On a named reference machine at 48 kHz/128 frames, record median/p95/p99 callback time, baseline and enabled-feature overhead. Require p99 below 70% of the block deadline in the agreed 16-voice stress patch, no missed callbacks attributable to the feature in a 10-minute test, bounded memory and zero callback allocations. If the target fails, reduce configurable limits or optimize before claiming ready. Run a 24-hour soak before a stable release.

## Required evidence per feature
Create unit tests for algorithms and malformed inputs, integration tests for GUI -> state -> DSP -> export, and state migration fixtures. Include an audible test patch and expected outcome, automated signal checks, manual headphone/speaker review and GUI keyboard/accessibility checks.
Exercise rapid automation, undo/redo, preset/A-B changes, missing media, editor close/reopen, concurrent render and playback, and plugin removal during a background task. Log errors with seed, feature configuration, event timeline, OS/build and sample hash; never claim exhaustive parameter-space coverage.
CI must build/test Windows, macOS Intel/ARM as configured, Linux and CLAP paths; actual host/architecture execution must be distinguished from cross-building. Pluginval and DAW tests supplement unit tests. Update PROGRESS and RELEASE_VALIDATION only with observed results.
