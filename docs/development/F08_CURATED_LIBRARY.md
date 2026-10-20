# F08: Curated factory library

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Easy engineering; substantial sound design. Dependencies: F05 for optional sample packs.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
FactoryContent generates samples and PresetManager installs a small starter set. Search, tags and favourites already exist and must be reused.

## Deliverable and scope
Ship at least 10 distinct, production-usable presets in the first increment; target 40 after audition review. Cover clean recognizable source, ambient cloud, dark drone, rhythmic pulse, short key, evolving pad, reverse texture, shimmer texture, stereo motion and restrained mix-friendly sound. Only include presets for implemented features; roadmap names must not appear as working categories.
Provide a starter collection using existing generated media so installation remains lightweight. Optional recorded-source packs are separately installable through explicit file import, never automatic downloads. Keep the existing compact installer budget for the starter edition; document optional pack size separately.

## Content contract
Each preset includes schema, stable factory ID/revision, name, author, tags, required capability IDs, source references, calibration and a short playing tip. A catalog maps IDs to file hashes and feature requirements. Add license/provenance metadata for every recorded source: creator, origin, permitted redistribution and any attribution. Do not bundle third-party content without documented redistribution rights.
Factory installation must be idempotent and preserve user edits/copies. Updated factory revisions install under managed identity, with user saves remaining in User. Existing factory presets remain addressable. Missing optional content shows Install/import pack guidance.

## Quality and tests
Audition every preset with a standard MIDI sequence, chords, repeated notes and silence. Record peak/RMS, CPU and voice count; choose conservative gain so the reference sequence does not clip. These are reference checks, not a guarantee that every possible chord will remain below full scale.
At least three listeners or, initially, an explicitly recorded developer listening pass must judge obvious clicks, unusable loudness jumps, sameness and misleading names. Mark the evidence honestly.
CI validates schema, paths, source licenses, hashes, duplicate IDs, minimum count and supported parameter IDs; renders representative presets and rejects nonfinite audio. Test clean/offline install, upgrade, user-copy preservation and missing-media repair. Presets using future effects are an additional pack after those effects pass their gates.
