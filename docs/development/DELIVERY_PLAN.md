# Delivery plan and cross-feature verification

Status: Proposed sequence, not implementation evidence.

## Implementation batches
| Batch | Deliverables | Exit condition |
|---|---|---|
| Foundation | F02 locks, F04 pitch model, F12 random streams, F01 audition; then F03 mutation | Tested action policy, pitch arithmetic, independent random lanes, owned notes and complete state defaults |
| Usability/value | F05 bundles, F06 background export, F07 tone/space, F08 first 10 presets, F09 history, F11 mono/glide; F15 after F06 | Portable recall, safe file writes, responsive rendering, no stuck notes and usable factory coverage |
| Distinctive sound | F13 grain targets then F19 rhythm; F10 scan, F14 modulation, F17 attack/cloud; F16 after rhythm; F18 after scan/modulation; F20 after mutation/history/export; F25/F26 separately | Every feature has listening evidence, deterministic fixtures, bounded resource use and explicit legacy bypass |
| Advanced performance | F21 shared-media scene interpolation, F22 capture, F23 MPE | Named-host routing/expression tests and complete state/lifetime tests |
| Research | F24 offline spectral prototype | Go/no-go reconstruction, musical usefulness and resource measurements |

Do not build every feature in a single PR. One feature or one dependency milestone per PR, with feature ID in the title. Spec completion does not authorize silently shipping placeholders. Integrate optional functionality only when its full path works. No additional external dependencies are chosen by these specs.

## Proposed module map
These are proposed modules, not existing files. Split production code into .h/.cpp where executable code is nontrivial.
- Engine: SourcePitchModel (F04), SamplePositionController (F10), MonoNoteStack/GlideEnvelope (F11), GrainTargetResolver (F13), AttackCloudVoice (F17), GrainClock (F19).
- Modulation: VersionedRandomStreams (F12), ModulationOscillator (F14), PersonalityMap (F16), JourneyEvaluator (F18), SceneInterpolator (F21), MpeEventRouter/VoiceExpressionState (F23).
- Presets: LockPolicy (F02), MutationPlanner (F03), VariationHistory (F09), FamilyPlanner (F20), SnapshotValidator (F21).
- IO: MediaManifest/BundleReader/BundleWriter/MediaStore/RelinkController (F05), ExportPlanner/ExportJob (F06), RenderedAudioCache (F15), CaptureBusAdapter/CaptureSnapshotWorker (F22), SpectralHybridWorker (F24).
- Effects: AfterimageLoom/HistoryRing/FragmentPool/PatternClock (F25), ResonantDrift/ModalBank/MaterialRatioMap/DriftOscillators (F26).
- GUI/controllers: AuditionController (F01) and feature panels using the above modules; no DSP or filesystem work in paint/timer callbacks.

A shared immutable PatchSnapshot must enumerate legacy parameters, samples/start markers, GRIT and all newly implemented feature state. RenderPatch adds event/transport/seed initialization. Define ownership and limits before adding media-history features. Do not introduce a second parameter registry or a second preset browser.

## Cross-feature conflict decisions
| Interaction | Required result |
|---|---|
| Locks vs host automation | Automation wins; locks constrain only named editing actions |
| Locks vs project restore | Restore is authoritative and ignores editing locks |
| Fine tuning vs Randomize | Calibration remains unchanged unless explicitly opted in |
| Mono legato vs attack/cloud | Overlap preserves transition; retrigger restarts it |
| Freeze vs journey | Freeze owns centre; unfreeze resumes current journey phase |
| Personalities vs advanced grains | Explicit overrides take precedence over mapped targets |
| Tone/space vs MORPH | Independent mode owns those targets; Follow preserves legacy |
| Scenes vs ordinary automation | Scene overlay owns rendered base while enabled; stored APVTS values remain available on exit |
| Seed vs unrelated new feature | Separate lanes prevent random draw-order coupling |
| MPE vs MIDI Learn | Reserved per-note expression bypasses ordinary learn |
| Live capture vs project save | Capture must be durably collected before the save is acknowledged |
| Export vs live editing | Export uses an immutable snapshot and private DSP |
| Loom plus Drift | Serial chain, no feedback crossing the module boundary |

## Proposed acceptance matrix
Every feature must identify its automated tests and manual checks. Proposed names below are categories, not files already created:
- Unit: parameter mapping/ranges, random golden vectors, time conversion, note ownership, serialization, migration, malformed data.
- Integration: control -> processor -> DSP -> state -> export; denied capability behaviour; old-preset fixtures; A/B and Undo.
- Audio: silence, impulse, sine sweeps, noise, known samples, rapid automation, boundary notes, dual-layer output, nonfinite inputs and finite-output assertions.
- GUI: keyboard/focus, tooltips, collapsed indicators, display scaling/scroll reachability, progress/cancel, editor destruction.
- Stress: seeded event/parameter campaigns with recorded iterations, separate failure logs and reproducible inputs; no claim that random testing enumerates every combination.
- Release: Windows/macOS/Linux builds, CLAP path, native execution where available, pluginval, named DAWs, 10-minute callback budget run and 24-hour soak.

For temporal tests allow at most one sample error where rounding is involved. For static bypass compare identical-build fixtures bitwise after transitions complete. For cross-platform rendering publish the selected max/RMS error tolerances with the fixture; do not pick a tolerance merely to hide a regression. For audible changes use gain-matched listening in addition to numeric checks.

## Spec decisions versus product decisions
Control names, ranges and algorithms here are proposed v1 contracts. Changes before implementation should be recorded in the relevant feature revision and manifest. Commercial tier assignment and release version remain unselected; implementations must route capabilities centrally.
F04 automatic pitch detection, F21 cross-media/four-scene expansion and F24 real-time spectral processing are explicitly later stages; the initial feature must not claim these are delivered.
