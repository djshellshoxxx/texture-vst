# F20: Texture family generator

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium–hard. Dependencies: F02, F03, F06, F09, F12.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Randomize creates one result. There is no batch of related candidates with preview, history and collection export.

## Workflow
Generate 4 or 8 candidates (default 4) from one captured baseline, lock snapshot and parent seed. Controls: mutation strength 0–100 default 20; variation groups; audition sequence Single/Chord; Keep parent visible. Candidate i uses a derived operation seed, not the live note PRNG.
The generator first produces validated patch data, then renders previews on a bounded worker queue (one render at a time per instance). The live patch stays unchanged until Apply. Show per-candidate play/stop, star, seed and changed groups. Applying a candidate creates one undo action and one history entry. Re-generate retains starred candidates, within an eight-slot cap.

## Preview loudness and persistence
Use the same 2-second held note plus 2-second tail for preview comparisons by default. Calculate RMS over active signal; adjust preview-only gain toward -20 dBFS RMS, limited to ±6 dB, and reduce further if needed to keep sample peak <=-1 dBFS. Mark near-silent previews instead of amplifying them strongly. This is not LUFS normalization or a true-peak guarantee. Store the original patch gain unchanged.
Save family writes individually named schema-3 presets and a small collection manifest with parent/candidate seed and media hashes. Optional bundling delegates to F05. Do not create a separate incompatible preset format.

## Modules, cancellation and tests
FamilyPlanner uses MutationPlanner repeatedly; PreviewRenderQueue uses ExportJob engines; FamilyView owns UI only. Every job carries the baseline revision and immutable media references. Changing the live patch does not alter in-progress candidates. Cancel drops queued renders, safely terminates the active render and keeps already starred patch data.
Test seed reproducibility, all-locked no-op, maximum candidate count, worker cancellation, preview level bounds, clipping detection and atomic save failures. Verify Apply/Undo restores complete media and sound state. Compare user success in a small task-based trial: time to retain three useful related sounds versus ordinary Randomize. Popularity remains a hypothesis until observed.
