# F03: Gentle Mutate

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Easy–medium. Dependencies: F02; F12 for reproducible operation seeds.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
randomiseParameters resets sound controls before choosing new values. There is no bounded variation around the current patch.

## Controls and algorithm
Provide Mutate, Strength 0–100% (default 15%) and affected-group checkboxes. Default eligible groups: Grain and Motion; Envelope and Tone/Space opt in. Exclude output gain, source calibration, media, voice mode, capabilities and enable/bypass switches unless explicitly selected through an Advanced option. Respect F02 locks unconditionally.
Operate on a captured baseline, not parameter-by-parameter live reads. For each eligible normalized continuous parameter u, draw delta from a triangular distribution on [-1,1] using a dedicated mutation stream; set u' = reflect(u + 0.25*s*delta, 0, 1), with s in [0,1]. Normalization follows the declared parameter mapping, so frequency/time parameters use their musical/log mapping. Quantize stepped values after mapping back. Strength zero is a strict no-op with no undo entry or history node.
Categorical changes are off by default. When enabled, probability is 0.2*s and the next value is sampled from the permitted alternatives. One Mutate press produces one validated change set, one undo transaction and one history node. No resetting to defaults.

## Integration and failure behaviour
Add MutationPlanner -> validated ParameterChangeSet -> processor apply. Planner is pure and receives parameter descriptors, lock snapshot and seed. APVTS host notifications occur on the appropriate non-audio thread. Seed belongs to the operation/history, not an automatable numeric float. Cancellation or invalid planning data changes nothing. Concurrent host edits after snapshot cause a revision conflict: reject the stale plan with a retry message instead of overwriting them silently.
Expose a concise result such as “Changed 5 controls; 3 locked”. New tonal controls and effects register mutation ranges explicitly; default exclusion is safer than broad automatic discovery.

## Acceptance
Known seeds produce exact change sets; every value is valid; locked and excluded fields remain identical. Test all strengths, boundaries, discrete choices, all-locked no-op, undo and stale revision rejection. Compare baseline and generated preview for nonfinite output, runaway gain and unexpectedly silent results. Musical usefulness requires auditioning representative percussion, voice, tonal and noise samples, not just numeric tests.
