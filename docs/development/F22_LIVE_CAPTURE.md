# F22: Live audio capture

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Hard. Dependencies: F05; immutable SampleBuffer publication.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
TEXTURE is an instrument with no capture input bus or rolling audio buffer. This is a format/host integration change, not just a record button.

## Scope and controls
Add an optional disabled-by-default stereo capture input bus to the instrument. Standalone input uses the existing device manager only after explicit selection. Provide Arm, Capture last, Clear, target Layer 1/2 and length 0.25–30 s default 4 s. Arm and capture are transient UI commands, never restored in a preset. Display input level, available recorded duration and a clear armed indicator. Do not route input to output unless a separate monitoring option is explicitly enabled; default monitoring off.
Audio writes to a preallocated stereo ring. Capture last snapshots the most recent requested frames, or the available shorter duration with a visible notice. The snapshot is converted to immutable sample data on a worker, then applied through the existing layer replacement protocol. Existing sound continues until publication succeeds.

## Concurrency and memory
At 192 kHz, a 30-second stereo float ring requires 46,080,000 bytes. Reserve at most two such buffers per instance for capture/snapshot staging (92,160,000 bytes plus bounded metadata). Publish generation-tagged chunks to a worker or rotate ring ownership at a bounded boundary; never let the worker copy cells concurrently being overwritten without a verified protocol. Capture requests while one copy is active return Busy, not a new allocation. If capacity cannot be allocated during preparation/arming off-thread, leave capture disabled.
Save captured media to the managed MediaStore before acknowledging durable preset save. Unsaved captured audio must be clearly indicated. No raw audio in small JSON state.

## Hosts and acceptance
CaptureBusAdapter handles layouts and input availability. Unsupported instrument-input routing displays guidance; a separate effect variant is a later product decision with its own plugin ID, not a silent instrument replacement.
Test ring wrap, exact frame count, no input, mono input duplication, callback partition changes, 192 kHz allocation, device changes, capture during preset load and destruction. Hash a known injected signal against the captured frames. Prove no callback allocation/locks and no input feedback loop. Validate VST3/AU/CLAP routing in named hosts before advertising support.
