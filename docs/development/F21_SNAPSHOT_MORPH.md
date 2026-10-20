# F21: Morph between sound snapshots

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Hard. Dependencies: F04, F07, F12; shared snapshot/undo model.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Existing A/B switches parameter snapshots; it does not interpolate multiple complete sounds over time.

## First delivery boundary
Provide two snapshot slots sharing the same two sample identities. Capture A/B from complete sound state, then expose scene_morph 0..1 default 0 and scene_morph_on false. Captures are explicit buttons and one undo action; they never replace the existing A/B comparison feature. Label incompatible samples and refuse the first-version morph rather than silently switching them.
When enabled, the scene overlay supplies interpolated base values before normal modulation. Linear-domain controls interpolate linearly; frequencies, times and pitch ratios use their declared log/semitone domain. Integer/discrete settings select A below 0.5 and B at/above 0.5 with a 20 ms transition where audio switching is needed. Media, seeds and curves stay at A in v1 unless identical; list these excluded fields in the UI. New expressive continuous controls must register their interpolation policy.

## Automation and persistence
scene_morph is the host-automatable performance control. While scene mode is enabled, affected ordinary parameter base automation is retained in APVTS but not rendered; show “Scene controlled” indicators. Turning scene mode off returns to those base values through 20 ms smoothing. Edits while enabled modify neither endpoint implicitly; provide Edit A/Edit B modes as explicit non-performance actions.
Persist both bounded snapshots, the active mode and amount in FeatureState/schema 3. Cap each snapshot to 64 KiB and reject recursive snapshot nesting. Older patches have mode off. Export captures both endpoints and current amount; future performance recording is outside v1.

## Architecture and acceptance
SceneInterpolator is pure data/math and never writes APVTS from processBlock. SnapshotValidator checks media identity and registered field policies. Test exact endpoints, midpoint domains, mode toggling, overlapping automation, malformed snapshots and unsupported features. Verify inactive mode is legacy-identical.
A second milestone may add four scenes and cross-media interpolation using separately rendered engines with bounded crossfades; it requires a new CPU/voice budget and cannot be claimed complete with the two-scene shared-media implementation.
