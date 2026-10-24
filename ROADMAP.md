# Roadmap

This follows the supplied requirements, renamed from Photon to Luma. Each stage
implements an end-to-end workflow with persistent data and error handling.
No dates are promised before measuring scope and performance.

## Validation schedule — user direction, 2026-10-20

Continue implementation toward 1.0.0 without intermediate builds, test runs,
static-analysis or QA campaigns. All functional, image-quality, data-integrity,
recovery, UX, load, performance and stress qualification is deferred to the
final pre-release campaign. Fix and retest its findings before publishing 1.0.0.
Do not publish intermediate development builds or count deferred checks as PASS.
The published stable version remains 0.5.1. Its accepted qualification gaps
remain open for that final campaign. Track new reports in
[the development backlog](docs/DEVELOPMENT_BACKLOG.md).

## M0 — RAW Vertical Slice / 0.1 Foundation

[Issue #1](https://github.com/yakut-sekonts/Luma/issues/1) ·
[Implementation PR #2](https://github.com/yakut-sekonts/Luma/pull/2)

✅ Implemented; PR ready for review. Public repository, Apache-2.0, pinned dependencies, macOS CI,
Qt/QML shell, SQLite migration, real Nikon NEF sensor decode, EXIF, CPU Exposure,
autosave and recovery of the selected photo/exposure after restart.

Acceptance: clean clone → build → run → open Nikon D7500 NEF → change Exposure
→ close → reopen → same photo and Exposure. Original checksum must not change.
Public synthetic tests alone do not satisfy the Nikon acceptance criterion.

## 0.2 — Develop Core

✅ Implemented; local acceptance passed. WB controls, exposure/tone, highlight/shadow handling, basic color,
edit schema evolution, undo/redo, disable individual corrections, image export.
Acceptance: edit and export a RAW; restart and reproduce the same render.
Implemented export scope: full-resolution JPEG/PNG/16-bit TIFF, embedded sRGB,
quality and explicit overwrite; no source metadata. History is session-local.
Auto/eyedropper WB, persistent history, advanced export options and DNG remain later work.

## 0.3 — Library

[Issue #5](https://github.com/yakut-sekonts/Luma/issues/5) ·
[Implementation PR #6](https://github.com/yakut-sekonts/Luma/pull/6)

✅ Implemented. Add/Copy import of files/folders, per-file transactional checkpoints,
SHA-256/size duplicate detection, cancellation and dead-owner journal recovery;
lazy original thumbnails, bounded paged metadata, virtualized grid and filmstrip.
Single/multi/range selection, ratings, Pick/Reject/Unflag, color labels, manual
collections and combined basic filters are included. Migration 2→3 preserves edits.
Acceptance covers generated RAWs, GUI organization/restart, Copy integrity and a
100k-row metadata/model fixture. Real 100k-image performance remains unverified.
Move, smart collections, advanced import templates, keywords and XMP remain later work.

## 0.4 — Professional Develop and presets

✅ Implemented and locally validated through 0.4.10. The first substage implements point/parametric Curves, HSL and
targeted tools with atomic persistence, gesture Undo/Redo and reproducible exports.
The user-preset substage implements versioned presets for Light/Color/Curve/HSL,
flat folder organization, create/edit/rename/delete, JSON import/export,
non-destructive hover preview and single-photo subset Copy/Paste. Preset/paste
application is one Undo; edit/catalog schemas remain unchanged.
The batch substage implements multi-photo preset/Paste and subset Sync Settings,
with atomic writes, cancellation, autosave coordination and conflict-aware
session Undo/Redo. The first Detail substage implements Amount/Radius/Detail/
Masking sharpening with transient Option diagnostics, shared preview/export,
neutral catalog migration and preset/batch integration.
The second Detail substage implements manual Luminance Amount/Detail/Contrast
and Color Amount/Detail/Smoothness, independent enable states, Option diagnostics
and noise→sharpening ordering with neutral migration and preset/batch integration.
The Crop substage implements an interactive frame, free/original/fixed/custom
ratios, rotate/straighten/flip and transient composition guides, shared export
geometry, targeted inverse mapping and preset/batch integration.
The Optics substage adds calibrated automatic/manual lens profiles, distortion/
vignetting amounts and lateral CA before Develop/Detail, shared preview/export,
neutral catalog migration and preset/batch integration.
The Transform substage adds manual projective controls, asynchronous Auto with
confidence refusal, Guided vertical/horizontal line pairs and shared inverse
geometry/coverage, with neutral migration and a Geometry settings subset.
The ready-made filter substage adds eight immutable declarative Curve/HSL looks,
transient hover, one Undo, batch application and redistributable photographic QA.
The edited-thumbnail substage renders saved settings with atomic snapshots,
revision/fingerprint disk identities and grid/filmstrip invalidation after saved
and batch changes, preserving selection and isolating late results.
The final substage adds reusable advanced export settings: JPEG/PNG/TIFF8/16
with compression and real processed Linear RGB DNG; four named color spaces,
seven resize modes, output sharpening, metadata/location policies, text/image
watermark, destination/naming/conflicts. A sequential immutable snapshot queue
coordinates autosave, progress/counts/errors, pause/resume/cancel and close/update.
All 29 local tests, private Nikon full-resolution export, previous-app default
compatibility and standalone bundle acceptance passed. See
[phase-0.4 acceptance](docs/PHASE4_ACCEPTANCE.md). Integration requires exact-head
macOS CI. Phase 0.4 implementation is complete. The human resumed phase 0.5 after review.
Ready-to-use filters must have representative licensed image tests and no
hidden irreversible effects. Preset application is one Undo operation.

## Pre-0.5 extensions requested by the human

- ✅ 0.4.11: English-first runtime language packages: en, ru, zh_CN, hy, de, es, it, fr;
  persisted Settings choice, embedded catalogs and UI/data separation.
- ✅ 0.4.12: JPEG/PNG/TIFF8/16/HEIC/HEIF/WebP/BMP/GIF and system-supported AVIF
  alongside RAW; shared Develop, alpha/ICC/orientation, Library and export acceptance.

Both extensions are implemented separately; validation and exact-head CI gate
integration. The human has resumed phase 0.5.

## 0.5 — Local Adjustments

✅ Implemented in source 0.5.1. Persistent ordered Linear, Radial, Brush,
Luminance Range and Color Range selections support eight local Light/Color
controls. Brush has size, feather, stroke opacity and erase; continuous strokes
remain anchored through Crop/Transform/EXIF. Ordered Add/Subtract/Intersect
components have independent enable/invert and order controls. Range sampling
uses the image before local layers and refuses stale asynchronous results.

Full-document Undo/Redo, atomic catalog 10→11 migration, restart, shared
preview/export/thumbnail rendering and all eight language packs include the
complete mask document. Global presets and batch operations preserve target
masks. See [phase acceptance](docs/PHASE5_ACCEPTANCE.md),
[workflow and limits](docs/LOCAL_ADJUSTMENTS.md),
[ADR 0024](docs/adr/0024-complete-local-masks.md) and
[Issue #44](https://github.com/yakut-sekonts/Luma/issues/44).

Depth Range is conditional on an actual source depth channel; the current
pipeline has none, so no synthetic Depth control is exposed. Metal and AI remain
0.6/0.7. Version 0.5.1 was published after CI with the 23 functional qualification
gaps explicitly accepted by the user; those gaps remain open.

## 0.6 — Performance

In development. Metal processing backend, CPU/GPU tolerance tests, bounded LRU,
progressive quality, true 1:1 rendering, memory pressure handling and latency
benchmarks on 8/16 GB Apple Silicon machines. All validation and benchmarks
wait for the final 1.0.0 campaign under the schedule above.

Initial implementation (unbuilt and unqualified): byte-bounded decoded/rendered
LRU caches, 1024→2560 progressive previews, on-demand full-resolution 100% view,
cache preferences, memory-pressure eviction and a cancellable Metal global
Develop pass (including Curves/HSL) with CPU fallback. Detail, masks, optics,
geometry and ICC conversion still use the CPU reference. The final
qualification remain open; see [implementation status](docs/PHASE6_DEVELOPMENT.md).

## 0.7 — Advanced

In development, unbuilt and unqualified. Native Vision Subject/Background/Object
bitmap selections, persisted Clone/Heal/Remove strokes, local Core ML denoise
integration with new Float32 DNG output, and opt-in local English semantic search
over recognized image categories are implemented in source. Catalog 11→14 adds
bitmap storage, retouch and embeddings; a pre-migration SQLite backup is required.
A Core ML Sky adapter is also present. The user selected installer-bundled models:
NAFNet SIDD denoise and U²-Net Sky have pinned weight sources/licenses, a build
recipe and automatic lookup in the app. Both models travel with Sparkle updates;
conversion, photographic quality and performance remain unqualified. See
[model delivery](docs/BUNDLED_MODELS.md). No model is downloaded at runtime and
no photo is sent to a cloud service.
See [implementation and remaining scope](docs/PHASE7_DEVELOPMENT.md).

## 0.8 — Merge

Initial source workflows implemented, unbuilt and unqualified: HDR with native
alignment, exposure normalization and deghosting; panorama with three projections,
overlap exposure matching, feathered seams, auto crop and bounded boundary warp.
Both create editable Float DNGs with durable catalog registration. This is not
a claim of 360° stitching, optimized seams or generative fill. See
[implementation and limits](docs/PHASE8_DEVELOPMENT.md).

## 0.9 — Stabilization

Initial source workflows implemented, unbuilt and unqualified: descriptive
metadata/XMP and export integration, backup scheduling/retention/restore,
catalog create/open, Missing/Locate/Locate Folder and bounded folder watching.
Metadata preserves ordered creators and custom labels, maps XMP -1 to Reject,
and schedules sidecar updates after flag changes; catalog schema is now 21.
Startup recovery handles unavailable catalogs and interrupted sessions; local
Diagnostics exposes system/cache information and a report without photo data.
Named processing versions persist full edits, masks and retouch without copying
originals; restoration is one Undo step. The session History browser lists retained
steps, supports arbitrary navigation/clear and creates a persistent named version
from any retained state. Its 200-step journal resets at photo/batch boundaries;
it is not persistent history across reopening.
The §17 Histogram source workflow adds sampled RGB/luminance distributions,
output-referred clipping indicators/overlays and five tone controls with gesture
Undo. It follows the visible preview, excludes transparency and display overlays,
and does not change export settings. Runtime/image-quality qualification is pending.
The §18 Auto WB and neutral eyedropper source workflows now resolve source
samples to ordinary Custom Temperature/Tint, with cancellable work, stale-result
guards and one-step Undo. Gray-world estimation and reference white-point fitting
remain subject to final photographic/numerical and UX qualification.
Per-process rotating event logs and local crash capture are also implemented in
source; reports stay local and are excluded from the shareable JSON.
Large catalogs, fault injection, UI/performance regression and all QA are
deferred to final preparation. See [implementation and limits](docs/PHASE9_DEVELOPMENT.md).

## Functional completion before 1.0 — source only

All eight requested functional blocks have source workflows: Presence, Point Color,
Color Grading, Post Crop Vignette/Grain, Smart Collections with nested AND/OR/NOT,
Select/Candidate Compare with linked zoom/pan, local Auto Adjust, and advanced
Add/Copy/Move import with date folders, naming, metadata/develop presets, keywords
and optional second backup. New creative values participate in Undo/autosave,
presets, batch edits, named versions, thumbnails and export. The import journal
preserves options, per-item metadata and pending Move cleanup across a crash.

See the [completion ledger](docs/FUNCTIONAL_COMPLETION.md) and
[ADR 0030](docs/adr/0030-functional-completion.md) for contracts and limits. These
items are implemented in source, **not built or qualified**. Earlier milestone
sections describe historical delivery and do not imply these new blocks shipped.

## 1.0 — Stable daily-use release

Not qualified. All workflows in requirement §100 must pass on supported hardware,
including local masks, presets, batch sync, export and restart. Signing,
notarization, release checksums and known issues accompany the release.
[Final readiness](docs/FINAL_1_0_READINESS.md) records remaining decisions and
qualification. There is no 1.0 completion or publication claim.


## After 1.0 — Frontend redesign

Planned after the qualified 1.0 release, as requested on 2026-10-17. Use
[Impeccable](https://github.com/pbakaus/impeccable) and
[Taste Skill](https://github.com/Leonxlnx/taste-skill) as design guidance for the
Qt/QML macOS interface. Begin with an audit of the existing product, then choose
the visual direction and validate the revised workflows before publication.
See [redesign direction](docs/POST_1_0_REDESIGN.md). Existing phase order and
qualification-before-release policy continue to apply.
