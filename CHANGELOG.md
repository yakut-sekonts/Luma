# Changelog

## Unreleased / 0.6–0.9 source development

No builds, tests or QA have been run for this implementation. Stable remains 0.5.1.

- Completed eight additional source workflows: Presence (Texture/Clarity/Dehaze),
  sampled Point Color with range preview, four-way Color Grading, Post Crop
  Vignette/Grain, nested Smart Collections, Select/Candidate Compare with linked
  zoom/pan, local Auto Adjust, and advanced Add/Copy/Move import with date folders,
  naming, metadata/develop presets, keywords and second backup.
  Creative edits join presets/batch/history/versions/export; catalog schema 21,
  preset codecs 8/9 and snapshot codec 2. New controls have eight-language labels.
  [Source completion and pending qualification](docs/FUNCTIONAL_COMPLETION.md).
- Bounded preview/source caches, progressive resolution, full-resolution view,
  memory-pressure handling and Metal Light/Color/Curves/HSL.
- RGB/luminance Histogram with bounded, alpha-weighted preview sampling before
  monitor ICC conversion, approximate clipping percentages and red/blue overlays.
  Histogram tone gestures use Undo/autosave; Option on Whites/Blacks provides a
  temporary clipping overlay. Eight-language UI; analysis/overlays are view-only.
- Auto white balance and neutral eyedropper: source-space, alpha-aware sampling
  through crop/transform/optics/retouch, bounded Temperature/Tint fitting using
  the existing render model, one-step Undo/autosave and cancellable worker with
  stale-result guards. Eight-language guidance; Custom results persist normally.
- Native local AI Subject/Background/Object selections stored as editable masks.
- Local Core ML Sky mask adapter, sharing bitmap persistence and Undo.
- Parametric Clone/Heal/Remove, source repositioning, persistence and Undo.
- Local Core ML denoise adapter and managed Float32 Linear DNG derivatives.
- Offline model delivery source integration: pinned NAFNet SIDD and U²-Net Sky
  weights/licenses, Core ML conversion recipe, automatic bundled defaults with
  custom override/reset and model manifests checked before release publication.
- Opt-in English semantic search based on local image classification and embeddings.
- HDR exposure merge/deghosting and panorama registration, three projections,
  feather blending, auto crop/boundary warp and managed Float32 DNG outputs.
- Durable generated-image registration and dead-owner recovery.
- Descriptive metadata, keywords/search, revision-aware XMP sidecars, source
  packet backups and metadata snapshots/policies in export.
- XMP interoperability: ordered creators, exact custom label text, Library color
  mapping, Reject/-1 import/export and flag-aware pending writes. Metadata UI
  edits these fields; creator/label text is searchable. Fractional ratings are
  refused explicitly instead of rounded.
- Catalog backup scheduling/retention/restore, create/open catalog windows,
  missing-original relinking and bounded folder watching.
- Startup catalog recovery, interrupted-session choice and remembered catalog;
  no automatic replacement of missing or unreadable catalogs.
- Local Diagnostics with system/cache information and a privacy-limited JSON
  export that refuses to replace existing files.
- Local rotating event logs and pinned PLCrashReporter crash capture, isolated
  per process. Inactive reports are formatted on the next launch; local folder
  access and capability status are available in all eight languages. No upload.
- Named processing versions: full edits/masks/retouch stored in the catalog,
  restore as one Undo step, rename/delete with stale-revision guards and bounded
  storage; eight-language desktop UI. Originals and photo metadata stay separate.
- Session History browser: labeled steps, arbitrary Undo/Redo navigation, guarded
  clear and direct named-version creation from a retained state. Stable handles,
  eight-language UI and explicit 200-step/session lifetime; original photos and
  current edits are retained when clearing history. Still unbuilt/unqualified.
- SQLite pre-migration backups; catalog 11→18 migrations.
- Source-only Developer ID/hardened-runtime/notarization packaging with isolated
  Actions setup and refusal of unsigned/non-notarized 1.x publication.
- Candidate fix for LUMA-BUG-035: avoid resetting loaded Library pages on photo
  opening and bind card focus to the clicked item before activating the view.

Model conversion/qualification and final acceptance remain open. Source version
0.9.0 is not a released or tested application. See docs/FINAL_1_0_READINESS.md.

## 0.5.1 / Complete local masks and UX fixes

- Brush with size, feather, per-stroke opacity, continuous painting and erase.
- Luminance/Color Range with asynchronous on-photo sampling and stable pre-local references.
- Ordered Add/Subtract/Intersect components with independent enable/invert and reordering.
- Full-document gesture Undo/Redo, shared preview/export/thumbnail rendering and eight languages.
- Atomic catalog 10→11 migration, bounded version-2 mask data and legacy gradient compatibility.
- Depth Range requires a source depth channel; no synthetic depth or AI masks are claimed.
- 34 reproduced UX defects fixed across preview, import, keyboard access, masks,
  localization, scrolling and compact dialogs.
- Publication approved after CI with 23 explicitly unqualified functional
  contracts; see `docs/releases/v0.5.1-quality.md`. Full UX qualification remains open.

## 0.5.0 / Local gradient adjustments

- Persistent ordered linear/radial masks with eight local Light/Color controls.
- On-photo geometry, feather, opacity, inversion, enable/reorder and one-gesture Undo/Redo.
- Transient colored overlay; shared preview, export and saved thumbnail rendering.
- Atomic catalog 9→10 migration and strict bounded mask storage; global presets preserve masks.
- Local panel translated in all eight language packages. Brush/ranges/boolean remain later 0.5 work.

## 0.4.14 / XMP preset import

- Offline Lightroom / Camera Raw XMP import with explicit compatibility report.
- Sparse Light/Color/Curves/HSL/Detail settings preserve absent controls.
- Preview, Undo/Redo, batch, durable storage and JSON round-trip keep sparse behavior.
- Bounded XML validation, atomic failures and preset schema 7; no catalog migration.

## 0.4.13 / Branded macOS release

- Original Luma ICNS application and DMG volume icons.
- One CMake version used by binary, Settings/title/status bar, Finder and installer.
- Public release packages all completed 0.4 work, languages and ordinary images.
- Installer/standalone/version/icon verification and signed Sparkle release feed.

## 0.4.12 / Source extension before Local Adjustments

- Ordinary image Open/Add/Copy alongside RAW: JPEG, PNG, TIFF8/16, HEIC/HEIF,
  WebP, BMP, GIF and native-codec AVIF.
- ICC-aware full-image decode, EXIF orientation, metadata and alpha through Develop/export.
- Shared Library, thumbnails, presets, batch/queue and durable restart; immutable originals.

## 0.4.11 / Source extension before Local Adjustments

- English default with runtime Russian, Simplified Chinese, Armenian, German,
  Spanish, Italian and French language packages.
- Atomic persisted preference and full retranslation without changing user data.

These extensions are included in the 0.4.13 public release.

## 0.3.0 — 2026-10-12 / First macOS release and Library

- Standalone macOS Apple Silicon DMG/ZIP, checksums, build provenance and release notes.
- Verified tag-driven release workflow with repository-scoped publishing permissions.
- Catalog schema 3: manual collections, membership indexes and per-file import journal.
- Add/Copy import, recursive folder scan, SHA-256/size duplicate detection and cancellation.
- No-replace Copy installation, complete-item checkpoints and dead-owner recovery.
- Bounded paged metadata model; virtualized grid and Develop filmstrip.
- Lazy original thumbnail cache with embedded-preview/RAW fallback.
- Single/multi/range/select-all; rating, flags, labels, collections and combined filters.
- Concurrent catalog initialization, rollback/recovery and 100k metadata fixture coverage.

Known limitations: original thumbnails; no Move, smart collections, import naming/
backup templates, keywords, XMP or real 100k-image performance certification.

## 0.2.0 / Develop Core

- Version 2 edit documents and transactional M0 catalog migration.
- Manual Temperature/Tint, As Shot and named WB presets; camera-space preview.
- CPU Exposure/tone/Vibrance/Saturation with per-correction enable switches.
- Gesture-based session Undo/Redo, reset and Cmd+Z/Cmd+Shift+Z.
- Full-resolution JPEG/PNG/16-bit TIFF export with deterministic embedded sRGB ICC.
- Atomic destination install, overwrite policy, RAW/catalog protection and cancellation.
- CLI Develop/export commands and restart-identical GUI export acceptance.

Known limitations: reference WB/tone processing; no Auto/eyedropper WB, persistent
history, camera profile calibration, advanced output settings or export queue.

## 0.1.0 / M0

- Initial C++20 / Qt 6 / QML macOS Apple Silicon application.
- Real integer Bayer RAW decode and independent Float32 CPU pipeline.
- Exposure, As Shot white balance, camera color matrix and ColorSync/LittleCMS display output.
- SQLite catalog, durable autosave, selected-photo and exposure restoration.
- CLI, generated public DNG tests, optional Nikon D7500 acceptance and macOS CI.

Known limitations: this milestone is not the first daily-use version. Bayer only;
bilinear demosaic; Fit viewer; one current photo; no export, manual WB, undo, masks,
preset filters, library grid or Metal pixel processing. Missing and modified
originals retain their edits but need manual source restoration.
