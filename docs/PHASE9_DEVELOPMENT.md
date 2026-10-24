# Phase 0.9 source development

Status: **initial implementation, unbuilt and unqualified**. Catalog schema is
18. Source versions are development markers, not published releases. Stable
remains 0.5.1; its catalog must be restored from a pre-upgrade snapshot to downgrade.

## Metadata and XMP

Import reads descriptive EXIF/IPTC/XMP through ImageIO alongside the existing
camera/exposure reader. Existing sidecars override the matching embedded fields.
Unsupported metadata does not prevent pixel import: the catalog records a warning
and the Metadata dialog displays it. Existing catalog records are not silently
replaced on reopening a photo.

Library → Photo actions → Photo metadata edits Title, Caption, Copyright,
an ordered list of Creators, Location, Keywords, Rating, Flag and XMP Label. Database revisions reject stale
saves. Keywords are ordered, unique, bounded and searchable; Title/Caption are
also included in Library search, along with creator names and XMP label text. Unicode byte/control limits are enforced in
the core. The first metadata draft of an old record reads the source, but changes
to the catalog require Save or the explicitly confirmed Read original / XMP.

Saved export snapshots include descriptive metadata and organization. All,
Copyright only, Remove location and None policies filter the merged source and
catalog metadata before serialization. A never-initialized record only overrides
rating/flag/label, retaining other embedded descriptive fields.

RAW sidecars use `basename.xmp`; raster sidecars use `filename.ext.xmp` to avoid
RAW/JPEG name collisions. Ambiguous RAW basenames or distinct .xmp/.XMP files
are refused. XMP is UTF-8, bounded to 8 MiB, with no DTD/entities or symbolic links.
ImageIO rewrites descriptive properties while retaining other parsed properties
and localized text variants. It does not promise byte-identical serialization
of unrelated XML. Every existing packet is backed up verbatim by SHA-256 in
`<catalog>.luma-xmp-backups` before replacement.

Sidecar writes use same-folder temporary files, fsync, revision/hash conflict
checks, no-replace installation for new files and atomic rename for known files.
External changes cause an error; catalog edits remain saved. Pending revisions
survive restart and can be retried manually or with opt-in automatic metadata
writing. There is an unavoidable external-writer race between the final filesystem
comparison and rename; the old packet is retained in the backup. This is not an
interprocess transaction with third-party editors. Develop settings are not
exported as Adobe Camera Raw adjustments.

Migration 17 replaces the single Creator field with an ordered table, carrying
the old nonempty value into position zero. Up to 256 creators of 1024 UTF-8 bytes
each retain their order and repeated names. The editor uses one name per line;
empty entries are omitted. `dc:creator` is written as `rdf:Seq`.

XMP labels preserve their text/case separately from the Library's five color
tokens. ASCII case-insensitive Red/Yellow/Green/Blue/Purple also color cards;
other names remain searchable text without an assigned color. Choosing a color
explicitly replaces the XMP label with its standard English name. No label clears
custom text too, including when the card already has no assigned color. Changing
rating/flag, or saving unrelated metadata, preserves the existing label text.

XMP Rating -1 maps to Reject and zero stars on import; 0–5 map to whole-star
ratings and Unflagged. Integral real spellings such as 3.0 are accepted.
Reject writes -1 in sidecars/exports while its existing stars remain in the
catalog. Pick is catalog-only; XMP Rating does not encode it. Fractional ratings
are explicitly refused with a visible warning rather than rounded. Changes to
Library flags now invalidate sidecar revisions, including older rejected assets
during migration. External packet backups and revision guards remain in place.

These interoperability decisions are implemented in source; external XMP round
trips, migration, export privacy and the updated UI still require final qualification.

## Named processing versions

Develop → Processing versions stores the current full EditDocument in the
catalog without copying the original. Create, list, restore, rename and confirmed
delete work on the active photo. Names are trimmed in the UI, bounded to 512
UTF-8 bytes, case-sensitive and unique within that photo. Invalid UTF-8/control
characters are refused. The list loads only names/IDs/revisions/timestamps; a
single selected document is decoded on demand on the I/O worker.

Snapshots include every current global correction and enable flag, curves, HSL,
Detail, crop, optics, transform, complete local masks (including bitmap coverage)
and retouch strokes/donor offsets. They exclude image pixels, original paths,
ratings, flags, keywords, descriptive metadata, UI/tool state and preset hover.
Before/Original display does not change the processing state that is saved.
Creating a version captures current edits even before their autosave finishes;
it does not claim that a previously failed autosave has succeeded.

Migration 18 adds `edit_versions` with an asset foreign key, immutable document,
revision-guarded rename/delete and transactional create/quota/name handling. Each
photo allows up to 100 versions and 256 MiB of encoded version data; an individual
snapshot is bounded to 20 MiB. Limits produce visible errors rather than pruning
versions. Deletion retains the current edit and original. Catalog backups include
versions; original photo files still require separate backups.

The version-1 codec explicitly covers EditDocument schema 8, uses classic-locale
round-trip double precision and strict canonical decoding, and composes the
existing versioned mask/retouch codecs. Unknown schemas, invalid fields, excessive
counts, truncated and noncanonical data are refused. Changes to this layout need
an explicit codec version/reader; these snapshots are not Lightroom presets or
an external interchange format.

Restore closes transient tools, validates the document against photo geometry,
then applies it as one Undo step and schedules ordinary autosave/thumbnail
invalidation. Restoring identical edits is a no-op in history. Version I/O holds
the editor's busy state; close/update waits for completion, and load/edit revision
guards prevent applying a late result to another photo or newer edit. If close
was requested while reading a version, its result is not applied during shutdown.
Database failures leave current edits unchanged and expose an error in the dialog.

This implements requirement §34. The session History browser below connects
historical states to the same persistent versions. All version behavior and
migration are unbuilt and unqualified.

## History browser

Develop → History exposes the existing bounded Undo/Redo journal: up to 200
steps plus the retained baseline, in chronological order. The current state and
later Redo states are marked separately. A slider/brush gesture still commits
one step; no-op edits and gestures returning to their starting values keep the
Redo branch. New edits after Undo discard later states. Descriptions are derived
once on commit from full edit differences; global/local parameter values, mask
names and group-level curve/HSL/geometry/retouch changes are translated for display.
Reset, settings application and version restoration have explicit action labels.

Selecting a row does not change the edit. Go to selected state navigates the
existing journal without creating an extra step, preserving later Redo states.
It validates geometry, leaves Before/Original display and schedules the ordinary
autosave/render path once. Save selected state as version captures that complete
historical document directly on the existing catalog worker, without navigating,
changing the current edit or discarding Redo. It shares named-version limits,
errors, busy/load/close guards and persistence.

Clear history requires confirmation and retains current edits, saved versions,
the original and pending autosave. The clear itself cannot be undone. State IDs
are never reused within a session; generation-scoped string handles reject stale
rows after photo loads, batch boundaries, pruning and branch replacement. Clear
also checks a revision captured before confirmation so later edits are not erased
by an old prompt. QML refreshes rows only on journal revision/language changes,
retains selection by ID and preserves scroll position across those refreshes.

History is session-local and this limitation is visible in the dialog. Loading a
photo again, restarting, or applying a batch change to the active photo establishes
a new baseline. The first row says Opened photo, not Import, because an existing
catalog edit may be the baseline. History does not store ratings or other photo
metadata and is not a durable audit log. Batch Undo remains a separate atomic
multi-photo journal; History's buttons and Cmd+Z/Redo do not fall through to it.
Saved versions are the durable way to retain a historical state.

The source implements the §33 browsing/clear/snapshot workflows with this explicit
session lifetime. No persistence extension or 1.0 qualification is claimed. See
[ADR 0027](adr/0027-session-history-browser.md).

## Histogram and clipping

Develop's Histogram panel implements the §17 source workflow: RGB and luminance
plots, a shared linear/log count scale, approximate shadow/highlight clipping
percentages, red/blue display overlays and Exposure/Highlights/Shadows/Whites/
Blacks editing. A regular grid takes at most 512 × 512 sample positions from
the current rendered canvas. The 256 bins use sRGB encoding of each linear RGB
channel, and sRGB encoding of linear luminance (0.2126 R + 0.7152 G + 0.0722 B).
Counts/percentages are weighted by alpha; fully transparent and uncovered
geometry pixels contribute nothing. An empty/transparent sample set displays
no data instead of inventing a histogram.

Analysis happens after the shared optics/retouch/Develop/Detail/local/crop chain,
before monitor ICC conversion, local-mask overlay and clipping overlay. This
keeps analysis independent of the monitor and UI colors. It follows the current
preview: Original, preset hover and temporary crop/Guided canvases use their
respective rendered content. Detail diagnostic modes deliberately expose no
photo histogram or clipping overlay; their grayscale images are not ordinary
processing output.

Shadow clipping means all three processed linear channels are <= 0; highlight
clipping means at least one is >= 1. Percentages estimate the sampled visible
content. Overlay classification uses every covered output pixel of that preview,
so a small clipped feature may appear in the overlay but miss the sample grid.
Thresholds are output-referred to sRGB, not raw sensor saturation or irreversible
data loss. They are not a soft proof for the selected export color profile.
Progressive/1:1 resolution changes can change the estimate; no exact full-RAW
statistics or realtime latency guarantee is claimed.

Image and histogram travel together in a `RenderedPreview` cache entry with
accounting for both, including on cache hits. A pipeline-v3 key includes clipping
mode. The existing render-generation/cancellation guard publishes only the latest
requested result; the panel marks updates pending and clears failed/no-photo
analysis. No separate analysis render or retained full-resolution float buffer is
introduced. Non-desktop callers default to analysis/overlays disabled.

The five equal-width tone zones run Blacks, Shadows, Exposure, Highlights, Whites.
Dragging right increases the chosen value, within existing parameter bounds; one
drag produces one History step and ordinary autosave. The selector, minus/plus
buttons and Left/Right keys provide keyboard alternatives (Shift gives ten steps).
Disabled corrections cannot be edited through the histogram. Navigation, new edit
gestures, Undo/Redo, reset and application deactivation end/invalidate the active
histogram gesture so late pointer events cannot resume it against a newer state.

Shadow/highlight toggles are session view state. Holding Option while dragging
Whites or Blacks, in either panel, temporarily selects the corresponding clipping
overlay. Releasing Option or ending the gesture restores the toggles' state.
The indicator colors preserve output alpha and are applied after monitor/mask
composition. They never enter EditDocument, catalog, presets, versions or export.
The compact panel, explanations and accessibility labels have eight-language
source translations. All behavior remains unbuilt and unqualified.

## Auto white balance and neutral eyedropper

Requirement §18 now has a source workflow in Develop → White Balance. Auto
estimates the visible crop; Eyedropper switches to Fit and waits for a matching
2560-edge source-preview render before accepting a neutral-area click. Both
produce Custom Temperature/Tint, one labeled Undo step and normal autosave.
As Shot and the existing illuminant presets remain available. The result is
ordinary edit data used by presets, named versions, thumbnails and export; no
Auto operation reruns implicitly on reopening or crop changes.

The sampler operates on camera-space preview pixels after LensCorrectionPass
and RetouchPass. CropGeometry maps EXIF orientation, crop/straighten/flips and
perspective into that source. It excludes current WB, global tone/detail, local
layers, monitor ICC and overlays. Bilinear samples are weighted by straight
coverage, so hidden RGB in transparent pixels does not affect the estimate.
Auto samples at most 192×192 grid positions and averages usable source RGB
(gray-world). Eyedropper samples up to 5×5 output-preview positions and refuses
an unusable center instead of silently choosing a neighboring opaque pixel.

Admission requires positive finite camera channels and As Shot working-sRGB
channels in [0.002, 0.98). Auto requires up to 16 usable grid positions; the
coverage sum must also exceed 10% of that minimum. These are preview heuristics,
not RAW sensor-clipping detection. Black, white, strongly colored or almost
transparent images can yield an explicit insufficient-sample error. Dominant
scene colors, mixed lighting and downsampling may bias the gray-world estimate;
the UI asks users to review the result. Creative HSL/local adjustments can keep
a selected area colored even after its source reference is neutralized.

The solver shares the exact existing manual gain model with DevelopPipeline.
It fits the ratios needed for neutral working RGB, searching reciprocal
Temperature 40…500 mired (25000…2000 K) and Tint −150…150, then refining. Values
are rounded to 1 K / 0.01 Tint. A log gain-ratio residual above 0.03 shows a
closest-supported-result message. This is not a colorimetric error guarantee or
an Adobe matching claim. Existing edit/catalog schemas are unchanged.

A separate worker has bounded grid/search work and cooperative cancellation.
Completion checks operation, load/edit revision, history generation/revision
and gesture state. New edits, tool switches, photo changes, Library, Original,
focus loss, Escape/Cancel and close invalidate work. Successful changes use the
normal Undo/autosave/render path. Failed sampling keeps the picker available
for another point; user cancellation leaves edits unchanged. Worker shutdown
joins before controller destruction. Progress/errors/history labels support all
eight languages. See [ADR 0029](adr/0029-white-balance-estimation.md).

This is source-only implementation; no build, numerical, image-quality or UX
qualification has run.

## Backup and recovery

Settings → Catalog and backups provides Every launch, Daily, Weekly or Manual
snapshots, 1–50 retained automatic snapshots (default 10), backup-folder access
and restore into a **new** file. Daily/weekly intervals are evaluated at startup.
SQLite's online backup includes committed WAL state. A new snapshot must pass
SQLite integrity and foreign-key checks before pruning; migration snapshots are
never included in automatic pruning. Corrupt old snapshots are preserved and
reported. These checks are application implementation, not executed QA evidence.

Restoration validates the source read-only and refuses an existing destination.
The active catalog is not replaced. The restored file can be opened in a new
window from this dialog. Catalog backup contains the SQLite data, not original
photos, generated DNG files, presets, preference files or user-selected models;
the UI explicitly asks for separate photo backups.

Create/Open catalog starts another desktop process with an explicit catalog path.
A per-catalog desktop lock prevents simultaneous desktop owners. CLI/third-party
SQLite connections still rely on SQLite concurrency and must not be treated as
desktop-lock participants. Settings/presets remain shared by catalogs in the same
directory, as in previous releases; separate project folders isolate them.

### Startup recovery

The startup window opens/migrates the selected catalog on a worker before the
editor, import recovery or automatic backups start. Missing/locked/unreadable
catalogs offer Open, Create or Restore backup into a new filename. The old
catalog is never automatically deleted or replaced. Opening an existing catalog
uses SQLite without CREATE, so a disappearing file is not silently recreated.
The first default catalog can be initialized; a missing remembered/explicit
catalog requires the user to choose another file or reconnect its volume.

A per-catalog `.running.json` marker contains only schema, app version, process
ID and UTC start time. It is removed after successful event-loop exit and worker
destruction, while the desktop lock is still held. A retained marker triggers
read-only SQLite integrity/foreign-key inspection before writable opening, then
offers Recover previous session or Continue in Library without decoding the last
photo. This is interrupted-session detection, not a crash dump or proof of a
specific crash cause. Historical saved edits stay in SQLite; unsaved slider
gestures are not invented or reconstructed. Separate local crash capture is
described below; neither the marker nor the crash report modifies saved edits.

The last successfully opened catalog is remembered for ordinary app/update
launches. Acceptance launches bypass the recovery window and do not change this
preference or session markers. No acceptance launch has been executed here.

### Diagnostics

Settings → Diagnostics shows application/library versions, catalog schema,
CPU/GPU, OS, physical memory, current resident memory and three bounded cache
snapshots. Cache accounting measures retained entries, not total process memory;
workers may retain evicted images. The Metal field describes the requested
preview policy and device availability, not proof of a particular GPU dispatch.

Export Diagnostic Report collects a fresh JSON snapshot locally, installing it
under a new filename without replacing a file. It excludes photos, asset names,
catalog/model paths, metadata, credentials, host names and hardware identifiers.
The active catalog path appears only in the local Settings UI. No report is
automatically sent or uploaded. Diagnostics are application features, not a
substitute for the deferred functional/performance qualification.

### Local logs and crash reports

Normal desktop launches create a private process directory beneath the app's
local data `diagnostics/` folder. `DiagnosticSession` records UTC, application
version, level, workflow and typed event codes only. Free-form errors, filenames,
photo content, metadata and credentials never enter this log API. Trace, Debug,
Info, Warning and Error levels are supported; the current callers record startup,
recovery and busy/error transitions in Library, Editor and Export. This is not
a capture of arbitrary Qt/third-party console output.

Writing runs on a dedicated worker with at most 512 queued events. A full queue
drops additional events and increments the diagnostic counter. File failures
disable logging and discard the remaining queue without blocking photo work.
Each process retains the current 256 KiB JSONL file and up to three rotated files.
Startup, Warning and Error records are synced; clean shutdown drains the queue.
Abrupt termination can lose queued or unsynced records, so the counter is not a
guarantee that every pre-crash event reached durable storage.

PLCrashReporter 1.12.2 is compiled from a pinned source archive for local macOS
Mach/signal and uncaught Objective-C exception capture. Each process has its own
reporter base path and an 8 MiB binary report limit. A debugger suppresses capture.
On a later launch, a background worker formats recognized inactive sessions into
bounded `crash.txt` files using same-folder temporary files, fsync and no-replace
installation. Incomplete temporary text is never treated as a completed report.
Raw reports are retained even if formatting fails. No crash callback touches
SQLite or the Qt logger; existing WAL/recovery/backup protocols handle the catalog.

The newest 12 recognized inactive sessions are retained; active sessions are
excluded. Inspection, formatting and deletion require the corresponding
`QLockFile` and matching schema/name marker. Unknown folders and directory
symlinks are skipped. Sessions use mode 0700; event/text files use mode 0600.
Acceptance launches neither enable crash capture nor alter diagnostic history.

Diagnostics exposes logging/capture availability, a dropped-event count and a
button to open the local folder. Raw/text crash reports can contain process/OS
information, register/stack addresses and binary paths. They stay local and are
not included in the exported diagnostic JSON or sent over the network. This is
a bounded crash report, not a full process-memory core dump. Startup failures
before initialization, SIGKILL, power loss, disk failures and some OS terminations
cannot be promised a report. Final builds need retained matching dSYMs for
offline symbolication; production capture deliberately disables symbolication
inside the crashing process.

All of this is source implementation only. No crash has been induced, no report
has been captured, and no logging/retention/permission behavior is qualified.

## Missing files and external changes

Loaded Library/filmstrip pages show Missing from worker-side filesystem checks.
Locate verifies size and SHA-256 before changing a missing asset's path. Locate
Folder maps old relative paths into the selected new root; the optional nearby
fallback checks the same basename immediately under that root. Updates preserve
asset IDs, edits, masks, retouch, organization and collections. Folder updates
checkpoint each metadata page; cancellation retains completed pages.

The watcher observes at most 512 directories associated with loaded metadata
pages, debounces notifications and refreshes their state. It does not poll/hash
all 500k originals, automatically follow a rename, replace changed bytes or
automatically import external XMP. Filesystem changes outside watched pages are
detected when those pages are loaded. Disconnected-volume and filesystem event
behavior remain unqualified; no whole-catalog live-watching claim is made.

## Outstanding qualification and source decisions

- Build every target and update fixtures for schema 21 and the Missing role.
- Exercise multilingual/large metadata, external XMP round trips, unsupported
  namespaces, concurrency, permission/disk failures and pending-write recovery.
  Include ordered/repeated creators, exact custom label text, explicit No label,
  standard-color mapping, Reject/Unflag/Pick changes and stale revision refusal.
- Cover named-version round trips for every edit field, masks/bitmap coverage and
  retouch, restart/backup/restore, Undo/Redo, no-op restores, duplicate/Unicode
  names, quotas, stale revisions, corrupted documents, disk-full failures,
  concurrent writes, photo switching, autosave failures and close/update waits.
  Qualify the new dialog at minimum size, keyboard-only and in all languages.
- Qualify History's 200-step boundary, no-op/changed/cancelled gestures, branching,
  Undo/Redo and arbitrary jumps using independent expected documents. Include
  version creation from Undo/Redo rows without changing the active edit, stale
  handles after prune/clear/load/batch, stale clear confirmation, geometry
  failures, autosave/close failures and original-display handling. Exercise
  keyboard navigation, translated labels, mask names containing markup/percent
  signs and viewport retention during render/autosave notifications. Durable
  history across reopening is not implemented; verify the visible lifetime note.
- Qualify Histogram bins against independent synthetic linear ramps/patches,
  negative/HDR values, alpha weighting, empty/transparent images and crop/transform
  borders. Cover ICC independence, local-mask overlay exclusion, CPU/Metal,
  cache hits/eviction, out-of-order completion, load/cancel/failure, Original and
  preset hover, progressive versus 1:1 and unsupported Detail diagnostics.
  Verify export/original invariants with clipping enabled, overlay thresholds,
  sample-grid aliasing limitations, gesture bounds/Undo/branching, Option release,
  focus loss and interrupted input. Update UI fixtures for the new panel and
  exercise minimum size, keyboard access and all translations. Extend the native
  row-probe coverage to analysis-enabled renders during the final campaign; its
  existing instrumentation currently uses the default disabled-analysis path.
  Measure added preview latency and cache/active memory only in final QA.
- Qualify Auto/picker WB against independent neutral patches and known casts,
  non-identity camera matrices, every supported temperature/tint boundary and
  model mismatch. Include opaque/alpha/HDR/black/white/monochrome/dominant-color
  and tiny images, gray-world bias, crop/orientation/flips/perspective/optics/
  retouch mapping, display-profile/overlay independence and existing manual/As
  Shot render equivalence after extracting the shared gain helper. Cover one-step
  Undo/Redo, no-op estimates, catalog/preset/version/export consistency, stale
  completions across load/edit/history/close, cancellation, failed samples/retry,
  worker submission/shutdown and native Fit/keyboard/focus/all-language layout.
  Build and fix all app/CLI/test targets before claiming readiness.
- Cover backup retention, migration/restore, derived journals and original
  invariants under fault injection; originals/DNGs need independent backup.
- Cover startup cancellation, missing volumes, multiple windows, stale markers,
  newer/corrupt catalogs, skipped photo recovery, lock lifetime and clean exit.
- Exercise Diagnostics snapshots/export, no-replace failures and privacy of the
  actual JSON output; wire the Diagnostics dependency into isolated UI fixtures.
- Qualify PLCrashReporter linkage, supported crash signals/exceptions, next-launch
  text conversion, matching binary UUID/dSYM symbolication and catalog recovery.
  Exercise queue saturation, rotation/retention, concurrent catalog processes,
  debugger/acceptance opt-outs, corrupt reports, disk/permission failures and
  interruptions while formatting. Inspect actual logs/reports for privacy.
- Cover moved roots, duplicates, disconnected volumes, checksums, selected-photo
  reopening and preservation of viewport/selection. LUMA-BUG-035 remains open.
- Run 10k/100k/500k catalog and 8/16 GB resource tests only during final QA.
- Convert and qualify the pinned denoise/Sky models. The user selected installer
  delivery; source integration now bundles both offline models. See
  [model delivery](BUNDLED_MODELS.md); conversion and model QA remain unexecuted.
- Full final functional, image-quality, native UX, recovery, load and stress
  qualification is pending. None of these checks is marked PASS.

Platform references: [ImageIO metadata](https://developer.apple.com/documentation/imageio/cgimagemetadata),
[XMP string access](https://developer.apple.com/documentation/imageio/cgimagemetadatacopystringvaluewithpath(_:_:_:)).
Interoperability definitions: [Adobe XMP Rating and Label](https://developer.adobe.com/xmp/docs/xmp-namespaces/xmp/),
[Dublin Core creator sequence](https://developer.adobe.com/xmp/docs/xmp-namespaces/dc/).
Crash capture: [PLCrashReporter source and integration](https://github.com/microsoft/plcrashreporter/tree/1.12.2).

## Remaining functional source blocks

The eight-block implementation request is complete in source. The detailed
[completion ledger](FUNCTIONAL_COMPLETION.md) lists Presence, Point Color,
Color Grading, Effects, Smart Collections, Compare, Auto Adjust and advanced import.
[ADR 0030](adr/0030-functional-completion.md) records the CPU algorithms, schema
19–21 extensions, compatibility, linked comparison and durable import ordering.

All UI labels were added to the eight language packs. Runtime error details retain
the existing source-text fallback when a translation is unavailable. The current
XMP preset importer retains its explicit supported-field mapping; adding a new
native Develop control does not imply Adobe algorithm or XMP field equivalence.
There were no builds, QA/static-analysis/formatting checks or releases for this
work. Final source integration, image-quality, UX, load/recovery and regression
qualification remain mandatory before publication.
