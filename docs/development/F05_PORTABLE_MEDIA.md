# F05: Portable sample bundles and relinking

Status: Proposed, not implemented. Revision 1, 2026-10-20.
Effort: Medium. Dependencies: shared schema-3 contract.
Binding foundation: [Shared contracts](00_SHARED_CONTRACTS.md).

## Existing coverage and gap
PresetManager writes paths and resolves them; a portable archive, content-addressed media collection and deliberate relink workflow are absent.

## Format and workflow
Keep plain .texture JSON import/export. Add .texturepack ZIP containing manifest.json, preset.texture and media/<sha256>.wav (or original supported file extension). The manifest records schema 1, media byte hash, byte size, original filename, layer role and decoded format metadata. Hashes detect accidental content substitution; they do not authenticate a publisher.
Collect samples copies only referenced media, deduplicated by content hash. Default preserve source bytes; decoded live captures use lossless float WAV. Export staging must complete before replacing a destination. Do not mutate source files or user preset paths. Optional host-project “Collect to folder” rewrites only the newly saved references, with a preview of affected files.

## Import, limits and failures
Validate before extraction/application: max archive 256 MiB, max expanded 512 MiB, at most 16 entries, manifest 64 KiB, preset 256 KiB, max decoded clip 30 s per layer under the existing sample limit. Reject absolute paths, traversal, drive/UNC paths, symlinks, duplicate normalized names, unsupported codecs and hash/size mismatches. Check expanded limits while streaming, not only from ZIP headers. Decode on workers.
Extract into a unique managed staging directory; validate both layers; commit the package and apply sound state only after validation. A bundle import failure keeps the current patch intact. Plain missing-path presets retain the existing empty-layer behaviour.
Relink offers Locate file, Search chosen folder and Keep missing. Match hash first; filename-only matches require audition and explicit confirmation. Never search the whole computer automatically. Cancel leaves state unchanged.

## Modules and acceptance
MediaManifest, BundleReader/Writer, MediaStore and RelinkController are separate from PresetManager’s sound application. Temporary imports are cleaned on failure; collect persistent files before releasing live buffers. Test moving packages between Windows/macOS/Linux, Unicode/case collisions, corrupt/truncated archives, decompression bombs, disk-full, permissions, missing media and reopening with original source removed. Verify shared media is not deleted while another preset references it.
