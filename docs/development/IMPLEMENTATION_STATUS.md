# Implementation status (v0.1.0)

Evidence: Linux/gcc Release build with `-Werror`, all three CTest entries pass (unit, 60 s fuzz, waveform math). Windows and macOS are verified by CI only.

| Feature | Status | Notes / deviations from spec |
|---|---|---|
| F01 Audition/Hold, F04 core, F12 seed | Implemented (earlier) | — |
| F11 Mono / legato / glide | Implemented | `MonoNoteStack` + `GlideEnvelope` + engine wiring. Params `voice_mode`, `note_priority`, `glide_ms`. Glide applies on legato overlap only (`glide_mode` omitted: "Overlap only" is the spec default). Glide is stepped per audio block. Legato does not update velocity; WAV export renders poly. |
| F07 Tone and space | Partial | HP/LP 12 dB/oct filter before the saturator (`tone_filter_on`, `tone_hp_hz`, `tone_lp_hz`) and stereo width (`space_width`) after GRIT. All transparent at defaults, bypass is bit-exact. Included in WAV export. NOT done: Follow/Independent mode, independent reverb/chorus/drive/tilt. |
| F02 Parameter locks | Implemented (session state) | Eight groups; locks constrain Randomize and Mutate, never automation or restore. Toggled in File menu > Lock groups. Not persisted in host state. |
| F03 Gentle Mutate | Implemented | Ctrl/Cmd+M or File menu. Seeded planner, max 12 % normalised step, calibration/mode/choice params never mutate. One undo step. |
| Play tab | Added | Settings > Play: voice mode, priority, glide, tone filter, HP, LP, width. |
| F05, F06, F08-F10, F13-F26 | Not implemented | Remain proposals; see README in this folder. |

Release: run the `build` workflow manually (`workflow_dispatch`) with `release_version` set; it builds Windows/macOS/Linux, runs tests and publishes a pre-release with zip/tar.gz assets.
