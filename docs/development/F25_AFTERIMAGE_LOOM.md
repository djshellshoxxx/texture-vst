# F25: Afterimage Loom effect

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium–hard. Dependencies: F12; shared effect routing and RenderPatch.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Existing effects are saturator, chorus, reverb, shelf/DC filtering and hidden GRIT. None reorganizes recent output into a controlled pattern of short pitched/reversed echoes.

## Musical purpose and differentiation
Turn a plain pad or transient into a repeating trail whose fragments have alternating age, pitch, direction and stereo placement. Unlike ordinary delay, the user selects a small fragment pattern; unlike the main granular engine, the source is recent mixed instrument output. This proposed combination is intended to distinguish TEXTURE’s workflow, not a claim of unprecedented DSP or patent novelty.
Example: a struck glass sound leaves alternating forward/unison and reverse/octave fragments while MORPH broadens their spacing and stereo reach. Include restrained presets as well as extreme examples.

## Controls
| ID | Range / default | Meaning |
|---|---|---|
| loom_on | false | Exact bypass after transition |
| loom_mix | 0..1 / 0.25 | Linear dry/wet blend |
| loom_memory_ms | 100..4000 / 1000 | Maximum sampled history age |
| loom_fragment_ms | 10..250 / 70 | Windowed fragment duration |
| loom_interval_ms | 25..1000 / 125 | Free-running event interval |
| loom_sync | false | Use beat division instead |
| loom_division | 1/4,1/8,1/16,1/32 / 1/8 | Synced interval |
| loom_pattern | Weave=0, Orbit=1, Fracture=2 / Weave | Fixed event table |
| loom_feedback | 0..0.85 / 0.2 | Bounded internal recirculation |
| loom_decay | 0..1 / 0.35 | Age-dependent attenuation |
| loom_width | 0..1 / 0.5 | Stereo placement extent |
| loom_morph_depth | 0..1 / 0 | Explicit MORPH linkage |

All are saved host parameters. Smooth mix/feedback/width/depth over 20 ms; latch fragment settings at spawn. Memory edits crossfade old/new read centres over 20 ms. No hidden randomization.
Version-1 four-event tables specify (age fraction, semitones, reversed, pan):
Weave [(0.25,0,false,-0.5),(0.5,12,true,0.5),(0.75,0,false,0.5),(1,-12,true,-0.5)].
Orbit [(0.25,0,false,-1),(0.5,7,false,-0.33),(0.75,12,false,0.33),(1,7,false,1)].
Fracture [(0.125,0,true,-1),(0.75,12,false,1),(0.375,-12,true,0.5),(1,0,false,-0.5)].
Depth increases age spread by up to 25% and pan width by up to 0.25 at MORPH=1, clamped to limits. Default zero means full independence.

## Signal path and bounded DSP
Insert Loom after the optional tone filter and before Resonant Drift and the legacy saturator. Capture input at this location only; never feed downstream output back across the chain.
Preallocate a 4-second stereo float history plus interpolation guard frames: about 6.15 MiB at 192 kHz. Use a pool of 32 active fragments total. Events use absolute sample scheduling. Pool exhaustion drops newest spawns and increments a diagnostic; it never allocates or truncates an active fragment.
Apply a Hann window to each fragment and interpolation suitable for pitched reads. Keep reads at least two frames behind the writer; do not spawn a fragment until all of its source span exists. Reverse reads must remain inside available history. During startup, unavailable events are skipped without reading stale memory.
Normalize overlapping fragment contributions by max(1, sum of absolute window/gain weights) per sample. Apply age gain exp(-3*decay*ageFraction). Feedback writes w=tanh(input + feedback*wetHistory), with no gain-increasing process inside the feedback loop. The sum-normalized feedback signal and feedback<=0.85 bound regeneration. A finite-value fault clears affected history and reports a counter off-thread.
Output is (1-mix)*input + mix*wet. Enable transitions crossfade over 20 ms. Bypassed mode does not write history and clears it after fading; enabling starts from empty history. Panic clears immediately. Report a conservative maximum 320-second tail when feedback is enabled, but stop processing earlier below the measured silence threshold. No infinite freeze mode in v1.

## Modules, persistence and tests
AfterimageLoom comprises HistoryRing, FragmentPool, PatternClock and the effect facade. No GUI or preset dependencies in DSP. Store parameters, seed-lane identity and pattern algorithm version; never save ring contents. Offline render starts empty with the same event timeline.
Tests: tagged impulses verify every table’s delay, direction and pitch; startup/wrap/short history; 32-fragment overload; sync/seek; feedback maximum for sustained full-scale input and subsequent silence; NaN/Inf; sample-rate changes; long bypass/enable cycles. Track finite output, bounded internal magnitude, tail retirement and callback allocations. Listening tests compare plain delay to Loom at matched level, checking intelligibility, excessive repetition and clicks. Acceptance requires three musically distinct demonstration presets and measured CPU/memory under the shared budget.
