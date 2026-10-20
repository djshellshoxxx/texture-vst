# F15: Drag rendered audio to the DAW

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: F06.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
Export produces a file and Show file action; there is no rendered-file drag source in the plugin.

## Workflow
After successful export, display a draggable WAV tile with duration, format and a small waveform. Also provide Render for drag, reusing the current export configuration and immutable snapshot. Do not initiate an operating-system drag until the file is completely written and closed. While rendering, show progress and cancel; keep previous completed audio usable.
Dragging transfers an absolute file path through the platform’s supported file-drag mechanism. It is audio export, not MIDI transfer or host project insertion. Unsupported hosts retain Save as and Show file actions. Do not claim every DAW supports the operation.

## Lifetime and storage
RenderedAudioCache owns completed files in an application-managed directory, never a stack-local temporary file. Hold an active lease during drag. Do not delete solely on drag end because a DAW may reference the original rather than copying it. Default retention is persistent until explicit cleanup, with size display and warning that referenced projects may rely on the files. A user can choose a persistent export directory.
Use unique sanitized filenames plus a short content ID; repeated renders cannot overwrite a file already leased or referenced externally. Clear cache lists files and requires user action. No external project scanning or network operations. Enforce a configurable 2 GiB cache cap; when full ask the user to clean or choose another directory rather than silently deleting.

## Modules and acceptance
RenderedAudioCache manages leases and persistence, DragExportController owns platform UI integration, F06 owns rendering. Test file existence after editor/plugin destruction, cancelled OS drags, Unicode paths, full cache, duplicate names and worker completion after editor closure. Manually verify supported Windows/macOS/Linux DAWs with screenshots and imported audio duration/channel count. Compare dragged file hash to the completed render. Host-specific limitations are documented in Help.
