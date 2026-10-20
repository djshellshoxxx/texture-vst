# F17: Attack-to-cloud

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium–hard. Dependencies: F04; shared RenderPatch.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Existing grains share an ADSR but there is no separate preserved attack stream crossfading into the granular sustain.

## Controls and sound
Add attack_cloud_on false; attack_cloud_hold_ms 0–500 default 40; attack_cloud_transition_ms 5–2000 default 250; attack_cloud_original 0..1 default 1. All are automatable and saved. Use the same source layer, source start and calibrated pitch for a direct attack reader plus the granular voice.
At note-on, launch both paths on the same sample. Keep direct attack at requested amount for hold time, then transition to the granular cloud. Let t run 0..1 over transition duration; choose direct gain=(1-t)*original and cloud gain=t to avoid equal-power gain inflation for strongly correlated material. Use a smoothstep t curve. After completion retire the direct reader.
Direct attack reads forward once from the source start and ends at sample end; it does not loop or inherit reverse/shimmer. Granular path retains those controls. If source ends early, fade direct output over the final available frames while cloud continues. Overall voice ADSR and velocity apply to the combined result exactly once.

## Event semantics and architecture
AttackCloudVoice owns a bounded direct read-head alongside existing voice grains. Times are latched at note-on; amount can smooth over 20 ms. Mono legato preserves an existing transition rather than retriggering it; Mono retrigger restarts. A note-off during the transition releases both paths through the common ADSR. No path may outlive its source buffer.
The first version uses an explicit hold time, not automatic transient recognition. A later transient detector needs its own accuracy fixtures and must remain optional. Add disabled-by-default processing branch to preserve legacy audio.

## Acceptance
Render a click followed by a sine and check the initial source transient, transition duration and final cloud contribution. Test sources shorter than hold/transition, one-frame clips, notes released early, high/low pitches, dual layers, maximum voices and stolen voices. Reject clicks introduced by path switching; measure peak gain on identical direct/granular signals. Audition piano, pluck, metallic hit and voice samples. Record CPU overhead relative to ordinary granular playback.
