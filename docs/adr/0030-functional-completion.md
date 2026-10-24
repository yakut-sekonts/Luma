# ADR 0030 — Creative controls, Library workflows and advanced import

Status: accepted for source implementation; unbuilt and unqualified.

The user requested continuous completion of the eight remaining functional
blocks. Builds, tests, static analysis and QA remain deferred until preparation
of 1.0.0. This decision does not waive the release gates.

## Creative data and rendering

Use a bounded registry for Presence, Point Color, Color Grading, Vignette and
Grain: names, defaults, ranges, preset groups and enable bits live together.
Keep the existing schema-8 scalar edit table and flags intact. Catalog migration
19 adds a mandatory one-to-one `edit_creative` document, initialized neutrally for
existing and newly inserted assets. Creative values participate in autosave,
Undo/Redo, batch conflict snapshots, presets and thumbnail/render identities.
Preset codecs 8/9 add the four groups and retain readers for codecs 1–7.
Full processing snapshots now write codec 2 and still read codec 1 with neutral
creative defaults. A database opened by the new source cannot be downgraded;
use the pre-migration backup with the matching older application.

Point Color and Grading run in the shared CPU Develop path. Active creative
color bypasses the current Metal kernel rather than dropping unsupported edits.
Presence follows noise/sharpening and precedes local layers. Vignette and Grain
operate in post-crop output coordinates before histogram analysis and ICC.
Grain is deterministic, source-coordinate anchored, with preview attenuation.
These are Luma algorithms, with no claim of numerical Adobe matching.

The Point Color picker and range view use the pre-Point/Grading reference color,
including optics/retouch, WB, tone, curves and HSL. Display ICC, grain and local
layers do not enter that reference. H/S/L ranges have soft boundaries; neutral
samples do not depend on an undefined hue. The overlay is view state only.
Auto Adjust implements `IAutoAdjustProvider` with a bounded local histogram
estimate. It resolves Exposure/Contrast/Highlights/Shadows/Whites/Blacks/Vibrance
to ordinary values and commits one Undo operation. It does not download a model.

## Smart Collections and Compare

Migration 20 stores versioned rule trees with revision-guarded updates. AND/OR/NOT
groups compile to whitelisted SQL columns/operators and bound values. Rules are
bounded to 64 nodes, depth 8 and 16 children per group; NOT has one child. Keyword
inequality means no matching keyword, rather than existence of any other keyword.
Dynamic membership uses the existing paged Library queries. Rating/flag changes
refresh the filtered view and collection counts while preserving its viewport.
Manual membership actions exclude Smart Collections.

Compare freezes Select/Candidate edits, including current unsaved active edits,
without changing the active catalog asset. Two cancellable renders share the
existing display pipeline. Fit uses bounded previews; 100% requests originals.
The panes share zoom and normalized pan center across different aspect ratios.
Closing the view releases both auxiliary images and invalidates pending work.

## Advanced import

Migration 21 extends the import journal with immutable options and per-item
plans. The old mode column remains Add/Copy; Move is represented by a Copy plus
an explicit Move plan. Legacy unfinished sessions retain their old recovery path.

Copy/Move destinations use an optional UTC `YYYY/MM/DD` hierarchy and the tokens
`{name}`, `{date}`, `{seq}`. Missing capture timestamps use the import timestamp
frozen once per session. Extensions are retained; bounded numeric suffixes resolve
existing filenames. Installation uses an exclusive hard link from a checksum-
verified, fsynced temporary file. Existing destinations are never overwritten.

Metadata presets retain nonempty text fields and add unique keywords; empty
fields preserve source metadata. Named presets are bounded, persisted in app
settings and serialized under a cross-process lock. Develop preset selection is
resolved before import to an immutable ordinary edit document. A per-item snapshot
freezes descriptive metadata, including source XMP, before file work. Catalog
registration and Develop application share a transaction. Applied metadata is
marked pending for the existing optional sidecar-writing workflow.

An optional second backup stores original image bytes, not processed pixels.
Both files must complete before registration. Move then rechecks the primary,
backup and source, refuses sources already referenced by this catalog, and uses
an exclusive same-directory rename into a journaled hold path. It rechecks the
held source before unlinking it. Source file symlinks are refused for Move.
Original sidecar files are retained at their source paths; their descriptive
values are in the catalog snapshot. A catalog backup is still separate from a
backup of image files and sidecar packets.

A crash after registration leaves a pending Move record for dead-owner recovery.
Recovery reconstructs missing copies from a verified source, uses frozen settings
and does not remove published files with ambiguous ownership. Changed/unavailable
files are preserved and reported. A failed Move cleanup retains the imported asset
and reports the original/hold path; it contributes an error as well as an imported
count. Duplicate sources are never deleted. Cancellation completes the current
post-registration cleanup before ending, so it cannot strand a source rename
without its durable journal. Retained sources and recovery temporaries may need
explicit later cleanup; do not silently remove them.

## Qualification

All implementations are source only. The final campaign must establish image
quality, zero-setting equivalence, scale/alpha/HDR behavior, serialization and
migration fidelity, preset/batch/version consistency, picker cancellation,
Compare focus/scroll behavior, rule semantics and import fault recovery. Source
completion is not evidence of compilation, successful execution or release safety.
