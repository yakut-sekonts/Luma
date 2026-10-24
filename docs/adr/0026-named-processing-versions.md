# ADR 0026 — Named processing versions in the catalog

Status: implemented in source; unbuilt and unqualified.

## Context

Requirement §34 calls for named versions of a photo's processing without
copying its original. Presets are sparse transferable settings; they intentionally
preserve target-specific masks/retouch and cannot represent a complete version.
The existing Undo/Redo history is session-local and is not a persistent version
store. Large bitmap selections must not all be loaded when listing versions.

## Decision

Add catalog schema 18 with a separate `edit_versions` table referencing assets.
Store immutable full-document snapshots, with revision-guarded rename/delete and
per-photo unique names. Create and quota checks share an immediate transaction.
Keep 100 versions / 256 MiB per photo and 20 MiB per encoded document as explicit
limits. Do not automatically delete a user's named versions to free space.

Use an explicit versioned core text codec for global EditDocument schema 8 and
the existing versioned local-mask/retouch serializers. Use classic-locale double
round-trip precision, bounded decoding and canonical representations. Future
layout changes require new codec versions with explicit legacy readers.

Run serialization, SQLite and decoding on the editor I/O worker. Lists return
metadata only. The editor's busy state prevents simultaneous edit/batch/load
mutation; generation checks reject stale restoration, and requested photo opens
are deferred. Close/update waits for the operation. Restore enters the existing
history/autosave/render path as one Undo step; an identical document adds none.

## Consequences

Versions travel with catalog backups and never duplicate the original image.
They contain processing state, including mask coverage, but no descriptive
metadata, ratings or UI/tool state. They are not an external interchange format.
A full snapshot can use substantially more space than a preset; explicit limits
keep growth bounded per photo without silently pruning named work.

This does not implement the requirement §33 History browser, persistent Undo
history or direct snapshot creation from a historical row. Migration, round trips,
UI, autosave/close races and failure behavior still require final qualification.
No test/build/QA execution accompanies this source implementation.

Follow-up: [ADR 0027](0027-session-history-browser.md) adds the session browser
and creation from a historical row using this storage. Durable Undo history
remains outside these changes; all qualification is still deferred.
