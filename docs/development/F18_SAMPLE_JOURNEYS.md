# F18: Sample journeys

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium–hard. Dependencies: F10; F14 curve validation.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Sample start exists but cannot trace a saved path over note or musical time.

## Editing and state
Add an expandable Journey lane over the waveform. A journey contains 2–32 nodes {time, position, morphOffset}, time normalized 0..1, position 0..1, morphOffset -1..1. Nodes must be ordered with distinct time and endpoints 0/1. Linear segments in v1; no arbitrary scripting.
Parameters: journey_on false; journey_clock Voice seconds=0 default, Host beats=1; journey_seconds 0.1–60 default 8; journey_beats 0.25–64 default 4; journey_loop Once=0 default, Loop=1, Ping-pong=2; journey_morph_depth 0..1 default 0. Curves are per-layer feature metadata; enabled/clock/duration/depth are host controls. Dragging a node is one undo transaction.

## Evaluation
Voice clock begins at note-on, independent of host transport. Host clock follows valid PPQ including negative positions and loops. No PPQ uses accumulated samples at the last valid positive BPM, initially 120; stopped host clock freezes. Once mode retains the last node after completion. Ping-pong reverses path time continuously.
Journey supplies the source centre to F10; Freeze overrides it. Apply morphOffset*depth to the base modulation result before per-voice personality mapping, clamped to [0,1]. This offset affects that voice’s grain targets only; shared effects continue to use the unmodified global MORPH result. Both layers have independent per-voice journeys, with no voice writing a shared macro value.
Position changes affect newly spawned grains, not active read-head jumps. Smooth centres over 10 ms for edited paths. Publish a validated immutable curve with an epoch; retire old curves off-thread. No curve parsing in DSP.

## Tests and limits
JourneyEvaluator is pure sample/beat-time math; JourneyEditor handles gestures. Test endpoints, wrap/ping-pong reversals, malformed/duplicate nodes, seeks, clock loss, Freeze release and new notes at different phases. Compare outputs across callback partitions for the evaluator. State round-trip must retain nodes and units exactly; old presets leave journey off. Confirm editing and export use the same curve version and a cancelled edit restores the previous curve.
