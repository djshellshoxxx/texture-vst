# F23: MPE expression

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Hard. Dependencies: F04, F11, F13.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Current pitch wheel applies globally through engines and MIDI is omni. Per-note pressure/slide/bend ownership is absent.

## Controls and interpretation
Add mpe_on false; mpe_zone Lower=0 default, Upper=1; mpe_members 1–15 default 15; mpe_bend_semitones 1–96 default 48; mpe_pressure_depth 0..1 default 0; mpe_slide_depth 0..1 default 0. Sound parameters persist; live note ownership and expression values do not.
Use MPE-aware note identities including channel and note lifetime; do not key solely by pitch. Accept supported zone/pitch-range configuration messages through a validated MPE adapter. Channel pressure drives per-note grain density/intensity; CC74 drives per-note grain position offset; pitch wheel drives that note’s pitch. Manager-channel bend is added to member bend within documented limits. Smooth pressure/slide 10 ms and pitch 5 ms without altering note event timing.
MPE-reserved expression CCs must not enter ordinary MIDI Learn while MPE mode is on. Non-MPE mode retains current omni, global ±2-semitone behaviour.

## Interaction and limits
Pressure/slide modify per-voice targets after global targets; they never write host parameters or move shared base knobs. Show effective expression only as optional readouts. Mono mode selects one eligible MPE note using F11 priority and follows only its member expression; do not sum expression from released notes. Audition uses neutral expression and its separate ownership.
Changing MPE mode/zone sends an internal 10 ms release and clears expression ownership so recycled channels cannot inherit bends. Sustain, all-notes-off and all-sound-off must match zone semantics. Voice stealing retires the old identity before accepting reused channels.

## Modules and tests
MpeEventRouter translates protocol to engine-independent NoteExpression events; VoiceExpressionState is bounded per voice. JUCE MPEInstrument is a candidate adapter, subject to checks against the repository’s pinned JUCE version.
Test two same-pitch notes on different member channels, independent bends, pressure/slide, channel reuse, zone changes, malformed configuration and sustain. Verify a bent note cannot bend another held note. Measure tuning error and expression timing against fixtures. Test supported controllers/hosts manually; passing MIDI fixtures alone is not a hardware compatibility claim.
