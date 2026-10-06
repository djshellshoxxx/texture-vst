# TEXTURE — manual test sweep

Everything `ctest` cannot see. Work top to bottom; a run is only valid if every
box in a section is ticked in the same sitting, on the same build.

Record the build under test: `git rev-parse --short HEAD`, the preset used, and
the host. A failure that is not written down did not happen.

Legend: **[S]** standalone · **[V]** VST3 · **[A]** AU · **[C]** CLAP

---

## 0. Hosts

The definition-of-done list in `00_MASTER_PROMPT.md`.

| Host | Format | Loads | Plays | Session recall | Notes |
|---|---|---|---|---|---|
| Standalone | S | | | n/a | |
| Ableton Live 11+ | V | | | | |
| FL Studio 21+ | V | | | | |
| Reaper 7 | V | | | | |
| Logic Pro (macOS) | A | | | | |
| Bitwig / Reaper | C | | | | optional build |

---

## 1. First launch, on a machine that has never run TEXTURE

- [ ] The 20 factory samples are generated on first launch, not shipped — the data
      folder fills and the progress toast appears once. Corrupt/missing generated pads self-repair.
- [ ] A second launch does not regenerate them.
- [ ] Deleting the data folder and relaunching regenerates cleanly.

## 2. Sample loading (`05_SAMPLE_LOADER.md`)

- [ ] Drag a WAV onto the drop zone — waveform appears, name shown beneath.
- [ ] Repeat for AIFF, FLAC, MP3, OGG.
- [ ] Drag a `.texture` preset onto the window — preset loads, not treated as audio.
- [ ] Drag a non-audio file (a `.txt`) — an error toast, no crash, previous sample intact.
- [ ] Drag a 0-byte file — error toast, previous sample intact.
- [ ] Drag a file over 100 MB — refused with the size message.
- [ ] Drag a 60-second file — truncated to 30 s, no click at the join.
- [ ] Drag a 4-channel file — downmixed to stereo.
- [ ] Drag a 44.1 kHz file into a 96 kHz session — correct pitch, correct length.
- [ ] Click the drop zone — the file browser opens; cancelling changes nothing.
- [ ] Drag a new sample **while a chord is sustaining** — the swap is seamless, no
      click, no dropout, held notes keep playing.
- [ ] Load 20 different samples back to back into one layer as fast as the UI allows — no crash,
      no leak (watch RSS in Task Manager / Activity Monitor), the last request in that layer wins.
- [ ] Start a Layer 1 load and Layer 2 load nearly simultaneously — both complete into the correct layer.
- [ ] Start a load, immediately clear that layer — the late decode must not repopulate the cleared slot.
- [ ] Delete the sample file on disk, then reload the session — parameters restore,
      the slot reports the sample is missing, no crash.

## 3. MORPH (`06_MORPH_KNOB_BEHAVIOUR.md`)

- [ ] At 0%: clean playback. No tremolo, no grain chatter, no audible windowing.
- [ ] Sweep 0 → 100 slowly over ~20 s: continuous, no steps, no zipper noise, no
      dropouts, no level jump.
- [ ] Sweep 100 → 0 just as slowly: the same in reverse.
- [ ] Slam 0 → 100 in one flick, repeatedly: no click, no runaway, no crash.
- [ ] At 100%: a diffused wash, still recognisably the source sample.
- [ ] Double-click resets to 35%.
- [ ] Shift-drag is coarse/faster; Ctrl/Cmd-drag is ultra-fine/slower.
- [ ] Ctrl/Cmd-click steps 0 / 35 / 70 / 100%.
- [ ] Arrow keys nudge 1%; Shift+arrow 0.1%.
- [ ] Host automation of MORPH at audio rate does not glitch.

## 4. Envelope, reverb, gain

- [ ] A, D, S, R each audibly do the right thing; tooltips show values on hover.
- [ ] Attack at its 1 ms minimum does not click; release at its 8 s maximum does not cut off.
- [ ] Reverb toggle: off is dry and the tail fades rather than truncating.
- [ ] Gain sweep is smooth across its full range; the LED turns red above 0 dBFS.
- [ ] Every knob: double-click resets to default.

## 5. LFO and layer (Pro)

- [ ] In the free tier the whole LFO section is veiled and the badge reads
      "PRO — click to upgrade"; clicking it opens the licence panel.
- [ ] Free tier: LFO depth has no audible effect even if automated by the host.
- [ ] Pro: rate and depth work; the ghost ring on MORPH follows the LFO.
- [ ] Sync on: the division dropdown appears and the LFO locks to host tempo.
- [ ] Sync on: with host PPQ available, stopped phase follows the host position; without PPQ it freezes. Starting, seeking and looping re-lock to host musical position.
- [ ] Change host tempo while playing: the synced rate follows the new tempo and remains locked to host musical position.
- [ ] Layer 2 blend behaves the same way against the gate.

## 6. Licence (`11_FREE_VS_PRO.md`)

- [ ] Empty email + empty key: "Activate" does nothing and says why.
- [ ] Valid email, wrong key: rejected, tier unchanged.
- [ ] Valid pair: activates, badge flips to PRO, Pro controls light up immediately
      with no reload.
- [ ] Close and reopen the host: still PRO.
- [ ] Deactivate: back to FREE, Pro controls veil again, state persists.
- [ ] Activate in one plugin instance: a second instance in the same session picks
      the tier up (on its next open at the latest).

## 7. Presets (`12_PRESET_SYSTEM.md`)

- [ ] The 5 factory presets load and each sounds distinct.
- [ ] Save a user preset, restart the host, load it — both sample layers, both start markers and every parameter match.
- [ ] Category filter and search in the browser return the right subsets.
- [ ] Hand-corrupt a `.texture` file, then load it — error toast, current state
      untouched.
- [ ] A preset whose sample is missing loads its parameters and leaves that referenced slot empty; it must not inherit the previous preset's sample.
- [ ] A/B compare: set A, change things, B, swap — both sides hold.
- [ ] Save/reopen the host session after editing A and B — both snapshots and the active slot restore.

## 8. State saving (`13_STATE_SAVING.md`)

- [ ] Save the session, close the host, reopen: sample, every parameter, window
      size, preset name and A/B slots all restored.
- [ ] Two instances with different samples in one session both restore correctly.
- [ ] Bounce/freeze the track and unfreeze: no state loss.
- [ ] Copy-paste the plugin within the host: the clone matches.

## 9. MIDI (`14_MIDI_SPEC.md`)

- [ ] Velocity maps to amplitude.
- [ ] Pitch bend is ±2 semitones and returns exactly to centre.
- [ ] Sustain pedal holds and releases correctly; pedal down through a sample swap
      does not strand a voice.
- [ ] All Notes Off (CC123) and All Sound Off (CC120) both behave — the latter also
      kills the reverb tail.
- [ ] 16 simultaneous notes: voice count caps, stealing takes the lowest-amplitude/oldest eligible voice and never exceeds 16.
- [ ] Retrigger the same note twice, then send one Note Off — exactly one repeated-note voice remains; the second Note Off releases the other.
- [ ] MIDI learn: right-click a control, move a CC, the mapping sticks; it survives
      a session reload; "clear mappings" clears it.

## 10. GUI (`09_GUI_SPEC.md`)

- [ ] Resize from minimum (540×360) to maximum (1440×960): the 3:2 ratio holds and
      nothing overlaps or clips at either end.
- [ ] The window size is remembered across reopen.
- [ ] Tab order is MORPH → A → D → S → R → Reverb → Gain → LFO.
- [ ] Every control is reachable by keyboard and reports a sensible name to the
      OS screen reader (Narrator / VoiceOver).
- [ ] Toasts: a new one replaces the old one instantly, never stacks, always fades.
- [ ] Tooltips can be turned off in Preferences and stay off.
- [ ] Settings, help, preset browser and debug window each open, close with Escape,
      and do not leave the main panel unresponsive.
- [ ] The easter egg pixel reveals GRIT; closing it hides it again; the hidden
      effect never appears in the host's automation list.

## 11. Performance (`15_PERFORMANCE_TARGETS.md`)

Measure in the host's own CPU meter, at 48 kHz / 128 samples, on the reference machine.

- [ ] Idle, no notes: under the idle target.
- [ ] One note, MORPH 0.
- [ ] One note, MORPH 1.
- [ ] 16 notes, MORPH 1 — the worst case. Must stay under target with no dropouts.
- [ ] Buffer 32 and buffer 2048 both behave.
- [ ] 44.1 / 48 / 96 / 192 kHz all behave.
- [ ] No allocation spikes while playing (the unit test covers the steady state; this
      checks the host's view of it).

## 12. Soak

- [ ] Hold a chord with the sustain pedal for 24 hours. Memory must be flat at the
      end; record RSS at 0 h, 1 h, 8 h and 24 h.
- [ ] Leave the fuzzer running for an hour: `TEXTURE_FUZZ_SECONDS=3600 ctest -R texture_fuzz`.

## 13. Uninstall / recovery

- [ ] "Reset all settings to default" in the debug window really does, and the
      plugin still works afterwards.
- [ ] Delete the data folder while the plugin is open — no crash; it is recreated.
- [ ] Export a troubleshooting file and read it: it names the host, the wrapper,
      the sample rate and the current sample.

---

## What CI covers instead

`ctest` runs the unit suites and the fuzzer on Windows, macOS and Linux on every
push. It deliberately does not attempt any of the above: none of it is meaningful
without a real host and a real audio device.
