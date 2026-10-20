# F16: MORPH personalities

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: F13; F19 for Pulse.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
deriveMorphTargets contains one fixed mapping from MORPH to grain/effect targets. Different transformation styles are not selectable.

## Product and parameter contract
Add morph_personality Legacy=0 default, Cloud=1, Pulse=2, Fracture=3, Swarm=4, plus personality_intensity 0..1 default 0.5. Legacy ignores intensity. Preserve the existing MORPH knob, automation ID and ring identity. A compact selector changes character; Help provides one concrete musical example per style.
The following are proposed tuning endpoints at intensity=1, MORPH from 0 to 1, and require listening validation within these bounds:
- Cloud: grain duration 30->250 ms, jitter 0->0.15, detune 0->15 cents, pan spread 0->0.8; prioritize smooth density within pool limits.
- Pulse: grain interval 1/4->1/32 note in discrete labelled regions, gate 0.9->0.3, jitter 0->0.05; requires F19 scheduler.
- Fracture: duration 60->8 ms, reverse probability offset 0->0.5, jitter 0->0.4, pan 0->0.7.
- Swarm: duration 40->100 ms, detune 0->40 cents, jitter 0->0.3, pan 0->1.
Blend a style’s target changes toward Legacy by intensity, except discrete scheduler selection which uses an explicit 20 ms transition. Preserve user TUNE and source calibration. Offsets to reverse/shimmer clamp after adding the existing base values; never replace their saved parameters.

## Integration
PersonalityMap is a pure function returning targets and scheduler intent; it does not own effects, voices or UI. F13 overrides follow mapping, and F07 independent controls override effect targets. Styles never silently enable paid/unavailable modules. If Pulse is not implemented, do not publish its host choice early; when introduced, retain the stated stable enumeration using a migration-reviewed plan rather than reordering shipped choices.
Switching styles during notes crossfades or smooths DSP targets for at least 20 ms; only new grains receive new duration/reverse choices. No file changes, resampling jobs or allocations.

## Acceptance
Sample every mapping at 1001 MORPH positions: finite bounded targets, intentional discrete boundaries and no uncontrolled gain jumps. Verify neutral compatibility, style switching under automation, overrides, undo and export parity. Deliver at least two convincing presets per implemented personality and gain-matched demonstration clips. Final endpoint adjustments must be versioned before release because they change saved-patch sound.
