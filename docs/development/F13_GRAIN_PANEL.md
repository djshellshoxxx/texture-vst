# F13: Expandable grain controls

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: shared target precedence.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
MorphTargets already derives grain duration, overlap, jitter, detuning and spread. Independent user overrides are missing.

## Controls and semantics
A collapsed Advanced grains panel exposes per-field Follow MORPH toggle (default true) plus manual value. Proposed IDs grain_size_ms 5–500 default 80; grain_density_hz 1–200 default 30; grain_jitter 0..1 default 0.1; grain_detune_cents 0..100 default 10; grain_pan_spread 0..1 default 0.5. Each has a corresponding grain_*_follow boolean. Values and toggles are host parameters. Defaults are ignored while following, preserving old patches.
Duration and density determine overlap in manual mode; do not offer a third independent overlap control that conflicts mathematically. When only duration is overridden, keep legacy cadence; when density is overridden, it determines spawning. With every Follow on, use the exact legacy scheduler.
UI shows both manual and effective values; a user can copy the current effective value into the manual control before disengaging Follow. Automation of Follow must not cause parameter writes.

## Capacity and DSP
Keep the current bounded grain pool initially. If requested duration*density exceeds available grains, deterministically skip new spawns and display a capacity indicator; do not steal active grains with hard cuts or allocate more from the callback. A larger pool is a separate profiled change, not implied by these ranges.
GrainTargetResolver applies overrides once and supplies both live engine and export. Smooth position/spread/detune inputs 20 ms; latch duration and pan when each grain starts. Never resize an active grain in place. Independent density scheduling uses absolute sample time.

## Tests and UX
Test all Follow combinations, limits, oversubscription, automation and migration. Effective readouts must match DSP snapshots and remain accessible with keyboard navigation. Confirm the collapsed state is visibly expandable at minimum window size. Use a sample with transient markers to verify density and duration numerically. Legacy path must not gain random draws or changed scheduling.
