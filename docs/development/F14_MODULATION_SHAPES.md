# F14: Additional modulation shapes

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: F12 for random shapes.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
MorphLFO supplies a sine LFO, tempo division and depth. Other waveforms, retrigger modes and user curves are absent.

## Parameter contract
Keep existing lfo_rate/depth/sync/division unchanged. Append lfo_shape Sine=0 default, Triangle=1, Step random=2, Smooth random=3, Curve=4; lfo_retrigger Legacy=0 default, Free=1, First note=2, Host=3; lfo_phase 0..1 default 0; lfo_slew_ms 0..100 default 10. Curve data is bounded feature metadata with 2–32 sorted normalized points, x in [0,1], y in [-1,1], endpoints at 0/1 and no duplicate x. One drag is one undo step.
Legacy/Sine preserves current update cadence and host behaviour. New modes use a sample-time phase accumulator; modulation is evaluated for grain events with fixed internal intervals where needed. First note retriggers only when total eligible held-note count goes from zero to nonzero, not every chord note. Host uses PPQ, including negative pre-roll values; represent unavailable PPQ separately from negative positions.

## Algorithms and state
Triangle is continuous. Step random samples one value per cycle from its dedicated stream, with specified slew. Smooth random interpolates consecutive values using cubic smoothstep. Curve uses linear interpolation between points, with wrap crossfade/slew at the boundary. No extrapolation beyond bounds.
Host phase derives from PPQ and division; seek/loop changes realign deterministically. Stopped Host mode freezes at supplied position; unavailable tempo keeps the last finite positive tempo. Reject nonfinite input. Free mode advances while audio callbacks run regardless of transport. Store shape, trigger, curve and phase offset, not runtime phase/history.

## Acceptance
ModulationOscillator is independent from GUI CurveEditor. Test endpoints, cycle period at each rate/division, seek, negative PPQ, stopped/missing clock, curve corruption, random golden sequences and state reload. Verify no audio-thread allocation and no APVTS writes. Compare mixed block sizes against a reference sample timeline for new modes. Default Sine/Legacy must retain existing renders.
