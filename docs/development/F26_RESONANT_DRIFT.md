# F26: Resonant Drift effect

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium–hard. Dependencies: F04; F12; shared effect routing.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
SHIMMER adds octave-up grains and GRIT bitcrushes/downsamples. Neither provides a tuned, slowly moving bank of material-like resonances.

## Musical purpose
Excite tuned resonances with the granular sound, moving gradually between harmonic and inharmonic relationships. Use it for glassy drones, bowed-metal pads and a subtle pitched body underneath noisy samples. The design combines established modal-filter techniques with explicit MORPH control and deterministic drift; uniqueness is product positioning, not a first-invention claim.

## Parameters and UI
| ID | Range / default | Meaning |
|---|---|---|
| drift_on | false | Exact disabled path |
| drift_mix | 0..1 / 0.2 | Linear dry/wet mix |
| drift_root | MIDI 24..96 / 60 | Manual resonance root |
| drift_keytrack | false | Follow last external MIDI note-on |
| drift_material | 0..1 / 0 | Harmonic -> metallic ratios |
| drift_decay_s | 0.05..8 / 1.5 | Mode amplitude T60 |
| drift_brightness | 0..1 / 0.5 | High-mode weighting |
| drift_motion_cents | 0..30 / 4 | Slow bounded detuning |
| drift_motion_hz | 0.01..1 / 0.08 | Drift speed |
| drift_width | 0..1 / 0.5 | Mode stereo distribution |
| drift_morph_depth | 0..1 / 0 | MORPH adds material and motion |

All parameters are saved and automatable. Show a small eight-mode frequency display, not an editable spectrum. Manual root remains available when key tracking is off. Key tracking follows the last received external note-on, ignores audition, and holds the last root after note-off; reset starts from drift_root. Per-note MPE pitch does not retune a global effect. Root changes smooth logarithmically over 50 ms.
MORPH depth adds up to depth*0.5 to material and depth*10 cents to motion, clamped. It never writes base parameters or automatically enables the effect.

## DSP definition
Use eight complex resonators per stereo channel, no output-to-input feedback network:
z[k,n] = r[k]*exp(j*w[k,n])*z[k,n-1] + (1-r[k])*x[n].
r[k]=exp(ln(0.001)/(sampleRate*T60[k])). Output mode is 2*Re(z[k]). T60[k]=decay/(1+0.15*k), k=0..7. Frequencies are baseFrequency times ratio, detuned by smooth deterministic motion.
Harmonic ratios H=[1,2,3,4,5,6,7,8]; metallic ratios M=[1,1.41421356,2.173,2.756,3.607,4.811,6.283,8.137]. Interpolate log ratios by material. These are designed timbral ratios, not a measured physical object. Suppress modes outside 20 Hz..0.45*sampleRate using a 20 ms fade, not frequency wrapping.
Weights before normalization are exp(-k*(1.8-1.6*brightness)); normalize the sum of absolute weights to <=0.5 per channel, including pan gains. With the normalized complex recurrence this supplies a conservative bounded-input gain contract. Implement with double state or prove float precision adequate at the maximum decay; flush denormals. Changing coefficients interpolates log radius/frequency, keeping radius strictly below one.
Drift uses one independent slow sinusoidal phase per mode with seed-derived starting phases; no phase discontinuity on a new host block. Width scales fixed alternating pan positions. Mix is linear; smooth 20 ms.
Routing: optional tone filter -> Loom -> Resonant Drift -> existing saturator/chorus/reverb/GRIT/shelf/DC/gain. Reset and Panic clear mode states. Bypass fades 20 ms then clears states. Conservative reported tail is 16 seconds after excitation ends at maximum decay; verify below -100 dBFS for normalized reference impulses, and revise bound if measured ringing violates it.

## Implementation and acceptance
ResonantDrift owns ModalBank, MaterialRatioMap and DriftOscillators. Prototype the filter bank as a standalone DSP module before UI integration. No dynamic coefficient-object creation per sample or block. Persist controls and algorithm version, not resonator states or live tracked notes. Offline render uses the same event root selection and seed.
Tests: analytic impulse decay, spectral peak frequency within 1 cent for static modes, weight/gain bound, frequencies near Nyquist, invalid rates and parameters, maximum decay at 192 kHz, rapid root changes, bypass/reset and simultaneous Loom feedback. Reject nonfinite output and unstable radii. Compare harmonic/metallic settings with noise, sine, impulse and recorded sources. Deliver at least three presets demonstrating subtle body, glass cloud and metallic motion; assess at matched loudness. Benchmark eight modes per channel under the shared callback budget.
