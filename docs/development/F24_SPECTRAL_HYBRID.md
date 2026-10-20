# F24: Spectral hybridization

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Very hard; research-gated. Dependencies: F05, F06, F12.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Dual layers currently mix granular audio; there is no analysis-based timbral transfer. This is a separate optional engine.

## Intended outcome and bounded prototype
Combine the spectral envelope of Layer 2 with the fine structure of Layer 1, preserving a controllable portion of Layer 1’s original character. This is not source separation and must not be advertised as extracting individual instruments.
First prototype works offline on loaded samples, creating a derived sample for the ordinary engine. Controls: hybrid_amount 0..1 default 0; envelope_smoothing 1–24 bins default 8; time_alignment Relative duration=0 default, Start aligned=1; energy_match on by default for the generated sample only. Generate/Cancel are explicit worker actions. Amount is generation metadata in v1, not a misleading live automatable knob.
Candidate algorithm: STFT magnitude-envelope transfer, interpolating log envelopes while retaining carrier phase, followed by overlap-add reconstruction. Compare FFT sizes 1024/2048/4096 and 75% overlap at 48 kHz; select a versioned configuration only after reconstruction and listening tests. No unverified “transparent” claim.

## Output and state
Store analysis algorithm/version, both source hashes, parameters and generated media hash in a recipe. Save the rendered derived sample through F05; reopening must not depend on regenerating with a later algorithm. Changing source shows recipe stale and requires Regenerate. Default/off uses the original path with no analysis, added latency or allocation.
Limit sources to the existing 30-second ceiling. Cap worker memory to 256 MiB and temporary disk to 512 MiB per job; reject oversized work. One job per instance, cancellable at frame boundaries. Source missing or analysis failure keeps the current sample.

## Go/no-go criteria
Reconstruction at amount zero must null within a documented numerical tolerance after delay alignment; impulses test overlap/add gain. Test silence, noise, tonal/polyphonic sources, different durations/rates and extreme envelope ratios. Bound gain using envelope floors and a documented ratio cap, avoid division by near-zero bins, and report generated-sample clipping.
Require at least 12 varied source pairs, blind comparison against simple layer mixing, measured CPU/memory/runtime and usable results on a majority of the agreed fixtures before committing to product integration. Real-time spectral processing, formant-preserving transposition and source decomposition are separate future research scopes.
