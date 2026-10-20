# F01: Audition button and Hold

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Easy–medium. Dependencies: none.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
MainPanel handles editing shortcuts but provides no internal note audition. Existing MIDI note routing and envelope processing are reusable.

## Behaviour and UI
Provide a momentary Audition button, Hold toggle, Stop button, note selector (MIDI 0–127, default 60), velocity (1–127, default 96) and chord selector (Single, Major, Minor, Fifth; default Single). Display MIDI number alongside octave naming. Chord offsets are [0], [0,4,7], [0,3,7], [0,7]; omit notes above 127.
Press starts one owned note set; release stops it unless Hold is active. Turning Hold on while idle starts that set; turning it off sends releases. Changing note/chord while held releases the old owned set before starting the new one at the same event boundary. Stop clears audition only. Panic clears all notes and tails. Repeated clicks cannot accumulate notes.
Editor close, host state restore, preset load, releaseResources and plugin removal cancel audition. Losing keyboard/mouse capture releases a momentary press. Hold does not depend on host transport. No MIDI output is generated.

## Architecture, state and safety
Add AuditionController (message-thread state machine) and an audio-thread audition event adapter. Track origin separately from external MIDI, so audition note-offs cannot stop a physically held note of the same pitch/channel. The adapter uses a reserved internal voice-owner identity, not a MIDI channel stolen from the host. At equal sample offsets apply owned note-offs before note-ons.
Audition configuration is a UI preference; active notes/Hold are transient and never restored. Buttons are not automatable parameters. Queue overload raises the dedicated stop flag and shows an error off-thread. No sample means disabled play controls with an explanation.

## Acceptance
Test every transition Idle/Pressed/Held, rapid changes, queue overflow, repeated note pitches shared with external MIDI, sustain pedal, editor destruction and state recall. No owned voice remains after Stop plus its release, or after Panic. One hundred press/release cycles must leave the voice count at baseline. Keyboard users can operate all controls without global DAW shortcut interception.
