# F11: Mono, legato and glide

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: F04 pitch model.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
GranularEngine supports polyphonic voice allocation and sustain; explicit monophonic priority, legato transitions and glide are absent.

## Parameters and state machine
voice_mode: Poly=0 default, Mono retrigger=1, Mono legato=2.
note_priority: Last=0 default, High=1, Low=2.
glide_ms: 0..2000, default 0, logarithmic above zero.
glide_mode: Always=0, Overlap only=1 default.
All are host parameters and persist in sound state.
Maintain a bounded held-note stack including channel, pitch, origin and press sequence. Repeated note-ons for a key use a saturating count with documented matching note-off behaviour; note-off may never underflow. Sustain retains released keys until pedal-up; priority chooses among physically held or pedal-retained notes. Last priority uses newest eligible note.
Mono retrigger restarts envelope and source position on a new selected note. Mono legato preserves envelope/source progression while eligible notes overlap; after silence it starts normally. When the highest-priority note releases, return to the next eligible note. Update velocity on a newly selected note, smoothed 5 ms; a fallback note uses its captured velocity.

## Glide and transitions
Interpolate pitch linearly in semitones from current rendered pitch to target over round(glide_ms*sampleRate/1000) samples. This is fixed-time glide independent of interval. A new target during glide begins from the instantaneous pitch; zero time is immediate. Pitch bend is additive and does not restart glide.
Changing voice mode releases old voices over 10 ms and rebuilds eligible-note ownership without duplicate note-ons. Panic clears stack, sustain, audition ownership and glide. Poly mode preserves existing behaviour and ignores mono controls. MPE mono interaction is defined in F23.

## Modules and tests
MonoNoteStack is pure event logic; GlideEnvelope is sample-time DSP; Voice receives pitch trajectory without resetting grains on legato. Test overlapping C/E/G releases in every order, repeated pitches, sustain, external/audition overlap, zero-velocity note-ons and channel-mode messages. Frequency-ratio tests verify endpoints and interval-independent duration within one sample. Host recall clears live notes but preserves settings. Default Poly/zero glide must match archived render fixtures.
