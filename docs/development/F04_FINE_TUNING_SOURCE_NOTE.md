# F04: Fine tuning and source-note controls

Status: Core implementation complete in the current development branch; user-preference layer linking and optional note detection remain deferred. Automated pitch arithmetic and state coverage is included; manual tuning validation remains pending. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: none.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Voice uses fixed rootMidiNote 60 and the existing tune parameter spans whole semitones. There is no per-layer root/cents calibration.

## Controls and pitch contract
Keep tune unchanged. Add layer_1_root and layer_2_root: MIDI integer 0–127, default 60; layer_1_fine_cents and layer_2_fine_cents: -100..100 cents, default 0, 0.1-cent display. Root applies at new note-on; fine tuning is smoothed over 20 ms for active voices. Both are host parameters and persist in presets/A-B/export.
For layer l, grain pitch exponent in semitones is: playedNote - root[l] + tune + fineCents[l]/100 + pitchBend + glideOffset + grainDetune/100 + shimmerOffset. Convert with 2^(semitones/12); include source/engine sample-rate ratio exactly once, following the current loader/engine contract. A positive fine value raises pitch. Root changes never rewrite tune.
Place controls beside each sample’s name. Show note number plus named pitch. Provide independent Reset calibration, and a Link layers editing toggle (off, user preference). Linking copies explicit edits, not automation; it never merges their saved values.

## Optional analysis stage
Add Detect note as a later worker task for predominantly monophonic material. Analyze a user-selected stable region, show estimated MIDI note, cents deviation and confidence; require Apply. For ambiguous/noisy/polyphonic samples report “No reliable note”. Never automatically retune on sample load. Define estimator validation against synthetic tones plus annotated instrument recordings before selecting an algorithm. Apply root estimate and negative measured cents deviation in one undoable operation.

## Modules and acceptance
SourcePitchModel owns pitch arithmetic; SourcePitchPanel owns controls; PitchAnalysisWorker is optional and cancellable. Tests: roots 0/60/127, cents ±100, octave ratios, sample-rate conversion, pitch wheel and later MPE/glide interaction. With defaults, legacy render remains unchanged. A 440 Hz reference mapped to MIDI 69 must produce 440 Hz at note 69 within 1 cent in a stable playback test; preserve default aliasing expectations rather than claiming a new resampler. Randomize and Mutate preserve calibration unless explicitly opted in.
