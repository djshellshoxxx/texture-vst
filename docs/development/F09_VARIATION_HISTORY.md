# F09: Variation history

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: F03; shared immutable patch snapshot.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Parameter Undo/Redo and A/B exist; a navigable history of generated sound alternatives does not.

## UX and scope
Provide a compact Previous/Next variation strip and expandable list with timestamp, operation, short name and star action. Store up to 32 variations in memory per instance; defaults start empty. Add entries after successful Randomize, Mutate and accepted Family candidate. Never add entries for every automation update.
Each entry contains the complete sound snapshot: parameters, sample identities/starts, feature data and saved seed. Media is shared by immutable reference, not duplicated per entry. Current sound must be recoverable before a preview; selecting an entry performs a preview that can be accepted or cancelled. Accept creates one undo transaction. Navigation through history must not recursively add history entries.

## Ownership and persistence
VariationHistory is a bounded model; HistoryController applies snapshots through the same validator as preset load. Star exports a normal user preset through existing Save UI; it does not imply permanent retention in the history ring. Pinning up to eight entries prevents their eviction; when all eviction candidates are pinned, reject the new entry with guidance instead of growing memory.
History is session-local and not embedded in host state. The currently accepted patch is saved normally. Restoring a host project clears history. Closing/reopening the editor preserves history while the processor lives.
Retain sample buffers safely across snapshots; eviction queues large destruction off the audio thread. Missing external files do not break an in-memory entry while its buffer exists; saving a portable preset must collect media explicitly.

## Acceptance
Test ring eviction, pinned limits, undo interaction, cancelled previews, sample replacement and failure during save. Verify parameter plus non-parameter state matches the selected entry, and cancelled preview restores the exact previous state. Keep metadata below 8 MiB excluding shared sample buffers, show retained-media usage and allow Clear history. Test processor destruction while previewing and host restore with no spontaneous notes.
