# F10: Freeze and scan

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: shared position precedence; F12 for repeatable random streams.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Sample starts and MORPH-dependent position jitter exist; a controllable stationary read centre and independent scan speed do not.

## Controls
Add per-layer scan mode Legacy=0 default, Manual=1, Scan=2; scan_position 0..1 default 0; scan_speed -4..4 source-lengths/second default 0; freeze false; freeze_spread 0..1 default 0.1. Prefix each ID with layer_1_ or layer_2_. Modes and floats are host-automatable; the UI distinguishes position from a destructive trim operation.
Legacy mode ignores new controls unless Freeze is enabled. Freeze latches the current effective read centre at the event boundary. Grain windows continue spawning around that centre; envelope and note-offs continue normally. Freeze is not infinite note sustain.
While frozen, editing scan_position moves the centre smoothly over 20 ms; spread controls jitter radius. Unfreezing resumes at the frozen centre, not the original start. Manual keeps scan_position authoritative; Scan advances it by elapsed samples and wraps within the permitted source region. Reverse grain probability remains independent from negative scan direction.

## State and implementation
SamplePositionController computes a normalized centre and jitter bound per voice. Persist mode, requested position, speed, spread and freeze flag, but restore Freeze at saved scan_position; do not attempt to serialize current grain/delay histories. On UI Freeze activation, capture the centre into patch metadata through a safe acknowledgment, without blocking the audio thread.
F18 Journey, when enabled, supplies the unfrozen centre instead of Scan. Freeze overrides that centre; on release Journey resumes at current transport/voice phase. Media replacement resets runtime centres and clamps saved positions.
Do not alter legacy per-grain PRNG draws when disabled. New modes use their own deterministic lane.

## Acceptance
Test position 0/1, one-frame samples, reverse scans, wrap seams, freeze during attack/release and sample-rate changes. Track read indices to prove bounds. Note-off plus release must retire voices even when frozen. Save/load gives the documented frozen region, not stale buffer data. Use impulses near file boundaries to detect unintended jumps or clicks.
