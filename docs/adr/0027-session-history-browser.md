# ADR 0027 — Session History browser and snapshots

Status: accepted for source development; unbuilt and unqualified.

## Context

Requirement §33 calls for a visible change history, Undo/Redo, arbitrary-state
navigation, clear and version creation from a historical state. The existing
editor already groups gestures into bounded session Undo/Redo. ADR 0026 provides
durable full-document named versions, independent of sparse presets.

## Decision

Extend the existing two-stack EditHistory instead of introducing a second undo
mechanism. Keep its 200-step capacity, gesture grouping, no-op and branch semantics.
Each committed state receives a monotonic ID, optional action label and structured
change descriptions. Generate descriptions only at commit, including gesture end;
expose metadata without copying/diffing historical documents on UI reads. Values
remain numeric and source labels stay untranslated until the desktop formats them.
User mask names are plain text and never interpreted as translation templates.

Seek by stable ID through the retained Undo/Redo stacks. Move states rather than
copying full documents for each crossed step. The desktop validates target geometry
and triggers autosave/render once after navigation; it adds no synthetic history
step. Original/Before display is disabled when navigating. An unchanged selection
does not create an edit. Transient tools finish before resolving a state handle.

Use an editor generation plus state ID, serialized as strings for QML, so a stale
row cannot silently address another photo or batch baseline. IDs are not reused
after clear, capacity pruning or branching. Clear requires confirmation with the
generation/revision captured when it opens. It drops Undo and Redo while retaining
current edits and pending writes; it does not reset the document or delete named
versions. Batch history remains independent and atomic across photos. History
dialog shortcuts/buttons operate only on the single-photo journal.

Saving a historical version copies the resolved complete document into the existing
named-version I/O action. It does not seek or alter the current edit, and preserves
Redo. Storage bounds, version-name validation, migration, busy state, deferred
photo opens and close/update coordination remain those of ADR 0026. No new catalog
migration is required.

Keep history session-local with a visible 200-step/lifetime explanation. Loading
a photo, restarting or an external batch result establishes a new baseline.
Use Opened photo / After batch change rather than falsely calling that baseline
Import. Persistent versions retain selected states across sessions; this journal
does not claim to be a durable audit trail. Ratings/keywords/metadata stay outside
the processing journal.

## Consequences and deferred qualification

The UI can browse all retained states, retain later Redo states during navigation
and persist a selected state without changing the displayed edit. QML updates
rows only on revision/language changes, retains selection by ID and restores the
viewport rather than reacting to every renderer/autosave notification.

The cap counts entries, not bytes. Large masks/retouch still require the final
resource campaign; bitmap coverage keeps the existing immutable shared storage.
Descriptions summarize multi-field curves/HSL/geometry/retouch at group level.
They are display metadata, not a serialized operation stream or interchange format.

Final qualification must cover the independent history oracle, capacity/branch
and gesture boundaries, stale state/clear guards, complete historical snapshot
fidelity, geometry failures, pending/failed autosave, load/close/update races,
keyboard-only use, all languages, long mask names and scrolling. No builds,
tests, formatting/static-analysis runs, application launch or QA were performed
for this implementation, following the user's development-only direction.
