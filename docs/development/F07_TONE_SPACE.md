# F07: Independent tone and space controls

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: shared target precedence.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
EffectsChain derives drive, chorus, reverb and high-shelf tilt from MORPH. Only reverb enable and output gain are independent public controls.

## Parameters and routing
Add tone_space_mode (Follow MORPH=0 default, Independent=1). Add tone_hp_hz 20–2000 default 20, tone_lp_hz 1000–20000 default 20000, tone_filter_on false, tone_tilt_db -12..12 default 0, space_width 0..2 default 1, space_reverb 0..1 default 0.3, space_chorus 0..1 default 0.2, tone_drive 0..1 default 0.1. Continuous values are host-automatable, smoothed 20 ms; frequency mappings are logarithmic.
Follow MORPH preserves the current chain exactly and ignores independent values. Switching to Independent through the UI can offer “Start from current sound”, copying current derived effect values before changing mode; automation changes only mode, using saved independent values. Never write those captures from audio processing.
Keep the legacy saturator -> chorus -> reverb -> GRIT -> shelf -> DC -> gain path. Independent filtering is inserted before the saturator; width is after GRIT and before shelf/DC. Shelf uses tilt control. Existing reverb_on remains the master reverb gate. Do not apply both MORPH wet gain and independent wet gain.

## DSP and UI
Use stable 12 dB/oct high/low cuts, cutoff bounded to <0.45*sampleRate. Smooth coefficients with a proven stable strategy or crossfade filter states; test resonance/transient behaviour. Width uses M=(L+R)/2, S=(L-R)/2, output M±width*S, with mono width ignored. Width may raise peaks; do not add undocumented gain compensation.
Tone and Space appear as separate collapsible groups with clear Follow labels. Zero-width preview must work without rewriting sample channels. Mode transitions use a 20 ms output crossfade; bypass paths stay bit-identical after transition completion.

## Acceptance
Test impulse/sine sweeps, cutoff clamping at low sample rates, automation, DC, mono sum and finite output at extremes. Measure shelf response and mono cancellation. Old presets remain identical in Follow mode. Independent settings survive MORPH movement, undo, A/B, host recall and export. Test that reverb tails continue or clear only according to the explicit gate/Panic contract.
