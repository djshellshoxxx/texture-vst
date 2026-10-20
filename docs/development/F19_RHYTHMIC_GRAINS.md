# F19: Rhythmic grain mode

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium–hard. Dependencies: F12; F13.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Tempo sync currently moves MORPH; it does not quantize grain creation to a musical grid.

## Parameters and scheduler
Add grain_clock Legacy=0 default, Rhythmic=1; grain_division choice 1/4,1/8,1/16,1/32,1/8T,1/16T (default 1/16); grain_swing 0..0.75 default 0; grain_probability 0..1 default 1; grain_gate 0.05..1 default 0.5; grain_burst 1..4 default 1.
For unswung grid interval D beats, delay odd grid points by swing*D while even points remain fixed; this preserves the two-step duration. Derive spawn sample offsets from PPQ and current tempo. Use double beat positions and integer sample event offsets, not callback-count timers. At missing PPQ fall back to a sample accumulator; tempo retains last valid value, initially 120. When stopped, do not create new rhythmic grains, while existing grain/envelope/effect releases continue.
One probability draw per logical grid event, keyed by seed/layer/voice/grid event ID. Bursts subdivide the interval after that event; burst duration cannot cross the next event unintentionally. Gate sets duration relative to the subdivided interval, bounded to the engine’s supported grain range. F13 manual duration overrides gate-derived duration when Follow is off.

## Event and resource rules
No catch-up explosion after transport seek or resume: realign to the current grid and generate only events in the current processing span. If a host event lies at block end, process it once at the next block start. A fixed event queue and grain pool bound work; oversubscription drops newest scheduled grains deterministically and increments a diagnostic counter.
Reset/All Sound Off clears scheduling state. MIDI note-on may start at the next grid point; provide a labelled First grain immediate toggle (default true) whose initial grain does not consume the next regular grid event.

## Acceptance
GrainClock tests use synthetic PPQ at multiple tempos, negative pre-roll, tempo changes, swing extrema, seek/loop and partitioned blocks. Assert event timing within one sample and no duplicate boundary events. Verify repeatable probability, bounded bursts, note-off retirement and unchanged Legacy mode. Listening tests cover straight, triplet, swung and sparse patches without misleading tempo labels.
