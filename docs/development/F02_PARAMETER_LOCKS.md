# F02: Parameter locks

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Easy–medium. Dependencies: none.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Randomize and preset loading exist; per-control and group locks do not. TUNE preservation alone is not a generalized lock system.

## Behaviour and UI
Add a lock action/icon to parameter context menus and group headers. Groups: Pitch calibration, Envelope, Grain, Motion, Tone/Space and Layers. Default all unlocked. A small toolbar indicator opens the lock list and Unlock all.
Locks apply to Randomize, Gentle Mutate, Texture Family generation and optional preset browsing. Preset browsing respects locks only when a separate Respect locks while browsing preference is enabled (default off). Normal host project restore, Undo/Redo, A/B, explicit parameter edits, Reset and automation ignore locks. Reset shows that locks do not protect it in its tooltip.
Media locking is a separate lock for each sample reference and start region; it must never be conflated with layer_blend. A protected sample is retained when a preset is browsed with locks enabled; failure to resolve any unprotected media follows existing missing-media policy.

## Model and persistence
Create LockPolicy keyed by stable parameter IDs and media roles; use a pure filter action -> eligible fields. Capture locks and source patch once when an operation begins. Do not read a changing UI lock table during mutation or asynchronous preset application. Host state stores a bounded lock list; portable sound presets exclude editing locks. Unknown IDs are ignored with diagnostics, duplicates collapse, invalid field types reject the record. Lock toggle is editor undo state separate from sound undo.

## Acceptance
For every registered parameter, compare locked/unlocked action outcomes. A locked value and media reference must remain exactly unchanged after 100 generated variations. Automation must still move a locked knob. Verify individual/group mixed indicators, schema migration, sample relinking, preset restore versus browse distinction and atomic lock snapshots. Add LockPolicy unit tests independent of JUCE and processor integration tests for each action.
