# F06: Configurable background WAV export

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: shared RenderPatch; F01 chord definitions.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
WavExporter renders a fixed two-hit pattern synchronously from MainPanel. It supports format selection but no job/cancel workflow or phrase configuration.

## User contract
Keep Legacy two-hit as an export mode. Add Single note, Chord and Repeated phrase. Controls: note 0–127 default 60; velocity 1–127 default 96; hold 0.1–30 s default 2; tail 0–30 s default 4; repeats 1–16 default 1; gap 0–5 s default 0.5. Chords use F01 definitions. Total duration capped at 120 s with calculated duration displayed; reject rather than silently truncate. Rates retain 44.1/48/88.2/96 kHz; integer depths retain 16/24/32. No implicit normalization.
Render starts from an immutable RenderPatch including samples, starts, calibration, seed and all enabled sound modules. Editing live controls afterwards cannot alter the job. Start each render from reset DSP; fixed-seed mode is reproducible. Preserve legacy export behaviour when Legacy mode is chosen.

## Threading and files
ExportJob owns private engines/effects and media references on a worker. One active job per instance; a second request offers cancel/restart. Poll progress at <=20 Hz; no per-block GUI posting. Cancellation is checked at least every 512 render samples and must acknowledge within 250 ms under normal I/O. Closing the editor hides progress without invalidating the job; destroying the processor cancels/joins safely off the callback.
Write to a sibling temporary file. Check writer creation, each write, flush and close outcome, then replace the destination only after success. Failure/cancel removes temporary output and preserves any existing destination. Space/permission errors show a useful path and reason. Render audio can clip if the user chooses excessive gain; report clipping count without silently adding a limiter.

## Tests
ExportPlanner computes exact sample boundaries for every mode. Test event offsets, rate changes, duration limits, sample-layer combinations and seed recall. Compare live and offline DSP with identical synthetic event/transport timelines, excluding documented legacy block-rate differences. Run export while automating the live plugin and verify snapshot isolation. Inject disk failures, cancellation and plugin destruction; no partial file is reported successful.
