# F12: Saved seed variation

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: none.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Voice seeds randomness from MIDI note and noteStamp. That supplies local repeatability but no user-visible patch seed or repeat policy.

## Modes and controls
random_mode: Legacy=0 default, Repeat=1, Evolve=2. Persist as a host choice parameter. Store seed as an unsigned 64-bit integer encoded as exactly 16 hexadecimal characters in feature metadata; do not store it in a normalized float. UI offers editable Seed, Copy and New seed. New seed is one undoable message-thread action. Invalid text changes nothing.
Legacy keeps current PRNG and draw ordering exactly. Repeat derives each new note stream from seed, layer, MIDI note and stream-role ID, omitting note occurrence; repeated equal notes start the same grain sequence. Evolve additionally uses a per-instance monotonically increasing note occurrence ID. Reset this counter on prepare and explicit Reset evolution, not every host block. Host state saves seed/mode, not in-flight counters; recall restarts the documented evolution.
Mutation planning and Family generation have separate operation seeds; they must not advance live-audio randomness.

## Deterministic contract
Create VersionedRandomStreams with algorithm version 1 and fixed-width integer arithmetic. Specify and publish golden integer output vectors in tests before use; never use std::hash, wall time or system RNG in DSP. Counter-based mixing of seed/role/event index prevents an unrelated new feature changing existing lanes. Define lane IDs centrally for grain position, pan, reverse, shimmer, modulation and effects.
For identical sample bytes, version, state and event/transport timeline, Repeat renders must reproduce on the same build. Across platforms require recorded numeric tolerances, not an unsupported bit-identical claim. Different host block partitions must produce identical new random sequences; whole audio can still differ due to documented legacy block-rate modulation.
Reset from GUI is a queued audio command at a boundary. No allocation or reseeding of global/shared random state.

## Acceptance
Golden vectors, state round-trip, invalid/maximum/zero seeds, changing one lane without affecting another, repeat notes, evolving notes and simultaneous layers. Render A twice then B then A: A must reproduce in Repeat mode after reset. Verify exporter captures seed and algorithm version. Incrementing PRNG version requires migration/legacy implementation, never silent sound changes.
