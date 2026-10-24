# Luma

Open-source non-destructive photo editor for macOS.

Luma is a local-first photo workspace for Apple Silicon, built with C++20,
Qt 6/QML, LibRaw, SQLite and LittleCMS. No account or cloud service is required.
The product requirements originated under the working name Photon; Luma is the
project name used for implementation.

![Luma Develop viewer with a project-generated test DNG](docs/screenshot.png)

## Release: v0.5.1

[Download the latest published release for macOS Apple Silicon](https://github.com/yakut-sekonts/Luma/releases/latest) ·
[0.5.1 release notes / known limitations](docs/releases/v0.5.1.md)

DMG and ZIP contain a standalone application; Qt SDK and Homebrew are not needed.
The build is ad-hoc signed and not notarized. Installation instructions and
checksums accompany the release. Version 0.5.1 adds complete local mask tools and
34 reproduced UX fixes to phase 0.4 work, runtime languages, ordinary images and
XMP preset import. Publication is approved after CI with
[23 explicitly unqualified functional contracts](docs/releases/v0.5.1-quality.md);
this is not a full UX qualification.

Future releases, including 1.0, use the same open-source GitHub distribution:
standalone DMG/ZIP, ad-hoc signing, explicit absence of Apple notarization and
Sparkle-signed updates. Apple Developer Program membership is not required.
The full pre-release QA gate remains required. See
[release and first-launch policy](docs/OPEN_SOURCE_RELEASE.md).

## Included in source: phases 0.4–0.5

The `codex/1.0-qualification` branch contains **0.6–0.9 development undergoing
final qualification**: Metal/caches, local AI/retouch/search, HDR/panorama,
metadata/XMP, backups/recovery, relinking, named versions, session History and
RGB/luminance Histogram with clipping overlays, Auto WB and a neutral eyedropper.
The eight remaining functional source blocks now include Presence, Point Color,
Color Grading, Vignette/Grain, Smart Collections, linked Compare, Auto Adjust and
advanced import with Move, naming, presets and a second backup. See the
[completion ledger](docs/FUNCTIONAL_COMPLETION.md) and [behavior/limits](docs/adr/0030-functional-completion.md).
These features are not in the published
0.5.1 installer. See [final readiness](docs/FINAL_1_0_READINESS.md) before using
or distributing a new build. Final testing and fixes are in progress; this is
not yet a qualified 1.0 release.

- ✅ CMake project and Qt/QML application shell.
- ✅ Real sensor decode through LibRaw, with a project-owned floating-point Bayer pipeline.
- ✅ As Shot white balance, bilinear demosaic and camera-to-linear-sRGB transform.
- ✅ Temperature/Tint, As Shot and WB presets evaluated before the camera color matrix.
- ✅ Exposure, Contrast, Highlights, Shadows, Whites, Blacks, Vibrance and Saturation.
- ✅ Independent correction switches, gesture-based Undo/Redo, reset and original comparison.
- ✅ Full-resolution JPEG, PNG and 16-bit TIFF export with embedded sRGB ICC.
- ✅ Camera, lens information, ISO, shutter, aperture and focal length.
- ✅ SQLite catalog with WAL, foreign keys, migration, transactions and durable autosave.
- ✅ Automatic restoration of the active photo and Exposure after restart.
- ✅ Fit viewer, resizable panels, drag-and-drop and cancellable decoding.
- ✅ ColorSync display profile → LittleCMS display transform; rerender on screen change.
- ✅ Shared core used by desktop, CLI and tests.
- ✅ Nikon D7500 restart acceptance, unit/integration tests and public macOS CI.
- ✅ Add/Copy import of files and folders, duplicate detection and per-file journal checkpoints.
- ✅ Lazy thumbnails, virtualized grid/filmstrip and bounded metadata paging.
- ✅ Single/multi/range selection, ratings, Pick/Reject, color labels and manual collections.
- ✅ Basic search and combined metadata/collection filters.
- ✅ Point/parametric Curves, HSL and targeted photo adjustments in source.
- ✅ User presets, subset Copy/Paste and atomic batch preset/Paste/Sync in source.
- ✅ Detail sharpening and manual luminance/color noise reduction with Option diagnostics in source.
- ✅ Crop, ratios, straighten, flips and guide overlays in source.
- ✅ Calibrated Lens Corrections with automatic/manual profiles in source.
- ✅ Manual, Auto and Guided perspective Transform in source.
- ✅ Eight ready-made Curve/HSL filters with hover and Undo in source.
- ✅ Advanced JPEG/PNG/TIFF8/16/DNG export, color/resize/metadata/watermark settings and sequential queue.
- ✅ Linear/Radial/Brush/Luminance/Color local masks, boolean composition and shared persistent rendering.
- 🗓 Metal image processing.

Supported platform: **macOS 14+ / Apple Silicon / arm64**. Intel, Windows and
Linux are outside the current scope.

### Image support

Luma develops **integer Bayer RAW**, including Nikon NEF and Bayer DNG, and
reimports its own processed Linear RGB DNG output.
LibRaw can decode other Bayer camera formats, but those are not claimed as
verified until they have acceptance coverage. X-Trans, monochrome RAW,
general floating-point/RGB DNG remain planned.
Source 0.4.12 also opens/imports JPEG, PNG, TIFF8/16, HEIC/HEIF, WebP, BMP, GIF
and system-supported AVIF, with ICC color conversion, EXIF orientation and alpha.
See [ordinary image support](docs/IMAGE_SUPPORT.md) for behavior and limits.
Develop never uses the embedded JPEG as its image source.

This is a foundation milestone, not a daily-use Lightroom replacement.
Bilinear demosaic is a reference implementation; advanced demosaic, highlight
reconstruction and production camera profiles remain planned.

## Build and run

```bash
git clone --branch v0.4.13 https://github.com/yakut-sekonts/Luma.git
cd Luma
./scripts/bootstrap.sh
export PATH="$PWD/.tools/venv/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
open build/Luma.app
```

See [BUILDING.md](BUILDING.md) for dependency setup, packaging and private RAW tests.
M0 is in [PR #2](https://github.com/yakut-sekonts/Luma/pull/2), Develop Core in
[PR #4](https://github.com/yakut-sekonts/Luma/pull/4), Library in
[PR #6](https://github.com/yakut-sekonts/Luma/pull/6). the first release includes all three stages.
The commands above select the immutable release tag.
Use `--catalog /path/to/catalog.sqlite` to select an isolated catalog. Without
this option, the app uses its macOS application-data directory. Add keeps original image files
at their original paths; Copy registers verified copies in the chosen destination. The catalog contains absolute paths and editing data;
it is local user data and must not be committed.

```bash
build/bin/luma open /path/to/photo.NEF --catalog /path/to/catalog.sqlite
build/bin/luma exposure 1.25 --catalog /path/to/catalog.sqlite
build/bin/luma develop temperature=5350 tint=12 highlights=-35 vibrance=20 --catalog /path/to/catalog.sqlite
build/bin/luma export /path/to/output.tif --catalog /path/to/catalog.sqlite
build/bin/luma restore --catalog /path/to/catalog.sqlite
build/bin/luma catalog verify --catalog /path/to/catalog.sqlite
```

## Development

[Roadmap](ROADMAP.md) · [Architecture](ARCHITECTURE.md) ·
[Requirements](docs/REQUIREMENTS.md) · [Contributing](CONTRIBUTING.md)

Only project-generated test images and code may be published. Private RAW
acceptance assets are external and are skipped when not supplied.

Licensed under [Apache-2.0](LICENSE). Third-party components retain their own
licenses; see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Develop and export scope

Temperature spans 2000–25000 K and Tint spans −150…150. Manual WB uses a
reference white-point approximation; As Shot uses the RAW camera gains.
The development branch adds Auto WB and a neutral eyedropper that resolve to
Custom values with one-step Undo; see [behavior and limits](docs/PHASE9_DEVELOPMENT.md#auto-white-balance-and-neutral-eyedropper).
Named versions and the session History browser also have source implementations.
These additions remain unbuilt/unqualified; highlight reconstruction, persistent
history and calibrated camera profiles are not claimed. Undo/Redo retains up to
200 operations in the current active-photo session. Double-click resets a
slider; Cmd+Z / Cmd+Shift+Z undo and redo.

Export uses an immutable edit snapshot, the original resolution, sRGB and no
source EXIF/GPS. JPEG quality is selectable; TIFF is 16-bit. A destination is
not overwritten unless explicitly selected, and the RAW/catalog cannot be
replaced. Export is cancellable. Resize, selectable spaces, metadata options,
output sharpening, batch export and DNG are later work.

## Library scope

Use Import to choose files/folders and Add or Copy. Duplicates are detected by
SHA-256 plus size and skipped by default. Copy creates a unique session directory
and a subdirectory per file; it never overwrites a source or existing destination.
Cancel keeps completed checkpoints. Startup recovers only dead-owner sessions,
registering verified complete copies and preserving changed published files.
Per-file failures and their source paths remain in the local import journal.

Double-click opens a sensor-decoded photo in Develop. Cmd-click toggles selection;
Shift-click selects a range; Cmd+A selects the filtered set. Keys 0–5 rate,
P/X/U set Pick/Reject/Unflag. Search covers filename/path/camera/lens; rating, flag,
label and manual collection filters combine before pagination. Collections are
references to assets; deleting one preserves originals and edits.

The model caches at most 16 × 128 metadata rows; grid and filmstrip virtualize
delegates. Lazy original thumbnails use two workers, a 96-request admission limit
and a 512 MiB disk cache in `<catalog>.luma-cache/thumbnails`. Embedded JPEGs are
used only for Library previews; Develop always reads sensor data. The 100k-row
metadata fixture verifies bounded paging and selection, not performance of
100,000 real photos. Edited thumbnails, Move, smart collections, import naming/
backup templates, keywords, photo-sidecar XMP sync and missing-file relinking remain later work.

```bash
build/bin/luma import add /path/to/raw-folder --catalog /path/to/catalog.sqlite
build/bin/luma import copy /path/to/destination /path/to/raw-folder --catalog /path/to/catalog.sqlite
build/bin/luma library --rating 4 --flag 1 --catalog /path/to/catalog.sqlite
build/bin/luma collections --catalog /path/to/catalog.sqlite
```

![Luma Library with project-generated test DNGs](docs/library.png)

## Included: Curves, HSL and macOS updates

The current source includes RGB and per-channel point curves,
parametric Shadows/Darks/Lights/Highlights, an eight-band HSL mixer and targeted
photo gestures. These use the same processing in preview and export, and save
atomically with edit schema 7 / catalog schema 8. Neutral curves and HSL preserve
the preceding Develop output. Sharpening and Noise Reduction are described below; Crop is described below; Perspective Transform is described below.

A bell in the toolbar shows scheduled update reminders. Check manually or opt out
of daily checks in its popover. Install opens Sparkle's native release/install UI;
Sparkle verifies the signed ZIP, replaces the app and relaunches it. Luma waits
for successful autosave and ongoing imports/exports before allowing relaunch.
Update checks send no photo/catalog data or system profiling payload.

The published v0.3.0 binary has no updater and needs one manual installation of
the first updater-enabled release. Future release publication requires the
repository signing secret; see [macOS updates](docs/UPDATES.md).

## Included: user presets / 0.4.1

Develop's left panel saves subsets of Light, Color, Curve and HSL, including each
selected group's enabled state and As Shot/custom WB mode. Hover renders a
preview without changing edits or autosave; click applies it with one Undo. The
menu edits/renames/moves/deletes presets and exports validated Luma JSON. Import
assigns a new local ID. Flat folder names organize the list.

Presets live in `presets.json` beside the selected catalog; catalogs in the same
directory share the collection. Keep this file with catalog backups. Writes are
locked and atomic; corrupt or newer unsupported data is reported and preserved.
Collections are bounded to 512 entries / 1 MiB. Copy selects groups; Paste applies
that in-memory snapshot to one photo. Copy/Paste does not use the system clipboard
and lasts only for the current application session. Batch paste/presets and Sync
Settings are available in Library as described below. See [preset workflow and validation](docs/PRESETS_REVIEW.md).

Presets and the subsequent phase 0.4 features are included in release 0.4.13.

## Included: XMP preset import / 0.4.14

Develop → Presets → Import accepts Lightroom / Camera Raw `.xmp` alongside Luma
JSON. Recognized Light/Color/Curves/HSL/Detail parameters remain sparse: absent
controls keep their existing values. Hover, one-step Undo/Redo, batch, restart and
JSON export/reimport preserve this behavior. Import reports unsupported settings;
Adobe profiles/LUTs/masks and other unsupported effects are skipped. Rendering
can differ from Lightroom. See [XMP compatibility and validation](docs/XMP_PRESETS.md).

## Included: batch settings / 0.4.2

Library's settings row applies presets or copied groups to the selected photos.
Sync Settings uses the open Develop photo as source and lets you select Light,
Color, Curve and HSL. A batch waits for autosave and writes the whole selection
atomically; cancel/failure rolls it back. One Undo batch/Redo batch operation
restores the last successful batch and refuses to overwrite newer stored edits.
Committed settings survive restart; batch Undo is session-local. An affected open
photo gets a new local history baseline. Original thumbnails remain unchanged;
Develop and export use the committed settings.

Close and app updates wait for batch completion/rollback. No RAW decode is needed
for batch settings and no catalog migration is introduced. Large-scale batch
performance is not yet benchmarked. See [batch workflow and validation](docs/BATCH_REVIEW.md).


## Included: Detail sharpening / 0.4.3

Detail provides Amount (0–150), Radius (0.5–3 original-image pixels), Detail
(0–100) and Masking (0–100). Amount defaults to 0; disabling the group retains
its values and restores the preceding output. Option-drag shows temporary
luminance, residual or mask diagnostics. White in Masking marks affected areas.
Modifier/gesture release, app deactivation and export preparation restore the
normal preview. Diagnostics do not change saved edits, history or exported pixels.

Preview, PNG/JPEG and TIFF16 use the same luminance sharpening stage. Preview
scales Radius for its reduced resolution; Fit is not a substitute for 1:1
inspection. Detail adjusts a soft residual threshold; Masking reduces sharpening
in flat areas. This is Luma's own algorithm. Manual noise reduction is described below; true 1:1 viewing and
export-specific output sharpening remain separate work.

Catalog 4→5 upgrades edit schema 3→4 with neutral sharpening while preserving
Curves/HSL, selection, revisions and Library metadata. Older builds refuse the
new catalog schema. The fifth Detail subset is available in presets, Copy/Paste
and Library batch Sync/Undo. Preset/collection schema 2 reads legacy schema 1;
legacy presets retain their original subsets. See [validation and scope](docs/SHARPENING_REVIEW.md).
This feature is included in release 0.4.13.

## Included: manual Noise Reduction / 0.4.4

Detail now includes Luminance Amount/Detail/Contrast and Color Amount/Detail/
Smoothness (0–100), with separate enable switches. Amount defaults to zero;
disabling retains values and restores the preceding processing. A cancellable
CPU filter follows tone/color and precedes sharpening in preview and full-size
PNG/JPEG/TIFF16. Color-only retains luminance; luminance-only retains opponent
color differences. Detail protects smaller differences; Contrast retains local
luminance contrast; Smoothness increases color smoothing radius.

Option-drag displays reduced luminance without color or color on neutral
luminance. Diagnostics are temporary, bypass sharpening and cannot enter exports,
presets, autosave or history. Gesture/modifier release and app deactivation clear
them. Reduced previews scale the spatial radius and cannot replace 1:1 inspection.

Catalog 5→6 upgrades edit schema 4→5 neutrally, preserving previous sharpening,
Curves/HSL, revisions, selection and metadata. Older builds refuse catalog 6.
Detail presets, Copy/Paste, batch Sync and Undo include all six parameters.
Preset/collection schema 3 reads schemas 1/2. Legacy schema-2 Detail presets
initialize absent noise to neutral defaults; applying them resets noise to zero.
Presets without Detail retain the current noise settings.

See [scope and validation](docs/NOISE_REDUCTION_REVIEW.md) and
[ADR 0012](docs/adr/0012-detail-noise-reduction.md). AI denoise, Metal, true 1:1,
representative-photo tuning and 100 MP performance remain later work. This is a
release; the earlier frozen 0.4.0 candidate remains separate.

## Included: Crop and Straighten / 0.4.5

Crop supports a movable frame with eight resize grips, Free, Original, 1:1, 4:3,
3:2, 16:9 and custom locked ratios (1:100–100:1), ratio orientation swap,
quarter-turn rotation, horizontal/vertical flips and Straighten (−45°–45°).
Thirds, Grid, Diagonal and Golden ratio guides are temporary display overlays.
One drag/slider gesture creates one Undo; reset/enable preserve other groups.

The tool displays the full transformed canvas; Done, Original, preset preview,
photo change and export leave this temporary view. Stored crop always applies to
exports. Straighten excludes empty rotated corners and samples processed linear
RGB bilinearly. Neutral/quarter-turn/flip/crop paths retain exact pixels. Full-size
PNG/JPEG and TIFF16 share geometry after Detail and before output conversion.
Targeted controls map coordinates back through the displayed crop to the source.
Output dimensions are shown in the panel. Pixel rounding can differ slightly in
Fit previews; very small crops retain at least one pixel.

Catalog 6→7/edit schema 5→6 initializes neutral Crop and preserves prior settings,
children, selection and metadata; failed migrations roll back. Older builds refuse
catalog 7. Crop is the sixth preset/Copy/Paste/Sync subset. Preset schema 4 reads
schemas 1/2/3 without adding absent Crop groups; batch Undo/conflicts include all
geometry fields. See [scope and checks](docs/CROP_REVIEW.md) and
[ADR 0013](docs/adr/0013-crop-geometry.md).

Higher-quality resampling and
Metal remain separate work. Crop is included in release 0.4.13.

## Included: Lens Corrections / 0.4.6

Optics uses a SHA256-pinned Lensfun v0.3.4 database and Luma's independent
profile layer. Known camera/lens aliases resolve conservatively within
compatible mounts; ambiguous or unknown lenses require manual selection.
A searchable lens list shows profiles compatible with the detected camera.
Unknown cameras cannot provide a sensor scale; no profile is invented.

Enable the profile in Develop, then use Distortion and Vignetting (0–200,
100 is calibrated strength), plus a lateral Chromatic Aberration switch.
Each calibration's availability is visible. Distortion/CA interpolate linearly
within calibrated focal bounds; PA vignetting interpolates aperture then focal
within available bounds, using the farthest calibrated focus distance.
No out-of-range extrapolation. This is not Lensfun interpolation equivalence.
Fisheye projection conversion and longitudinal CA/defringe are not included.

Sensor-linear correction precedes WB/color conversion, Develop and Detail;
Crop follows Detail. Preview and PNG/JPEG/TIFF16 share this path. Bilinear
per-channel sampling and reciprocal PA gain retain signed HDR. Conservative
inward scaling keeps lookup positions inside the sensor, reducing field of view.
Unsafe/non-monotonic calibration fails visibly. Active optics adds one float RGB
buffer; disabled/zero/unavailable corrections retain the exact old pixel path.

Catalog 7→8/edit 6→7 migration preserves old settings and initializes profile
correction off. Older apps refuse catalog 8. Optics is the seventh
preset/Copy/Paste/Sync subset; preset schema 5 reads 1–4 without inventing absent
Optics groups. Selection/amounts/CA persist and participate in batch Undo/conflicts.
Database data retain CC BY-SA 3.0; original XML, generated data and notices ship
in the bundle. See [ADR 0014](docs/adr/0014-lens-profiles.md) and
[Lens review](docs/LENS_REVIEW.md). This development version is not a public release.


## Included: Perspective Transform / 0.4.7

Transform adds Rotate, Horizontal/Vertical, Aspect, Scale, X/Y Offset and Constrain
Crop. Auto detects sufficiently supported perspective lines asynchronously and
refuses uncertain scenes; Guided uses two vertical/horizontal line pairs with
draggable endpoints. Either application is one Undo. Manual controls compose
with the solved matrix; Reset clears both. The effective inward scale is shown.

Transform and Crop compose in upright coordinates before one linear-RGB bilinear
sample. Constrain Crop avoids empty edges; otherwise PNG/TIFF retain transparency
and JPEG uses black. Invalid matrices and inverse poles are refused. Neutral or
disabled Transform retains exact previous pixels. Catalog 8→9/edit 7→8 migrate
neutrally. Geometry is the eighth preset/Copy/Paste/Sync subset; schema-6 presets
read older schemas without introducing absent groups.

See [ADR 0015](docs/adr/0015-perspective-transform.md) and
[Transform review](docs/TRANSFORM_REVIEW.md). Ready-made filters, edited thumbnails and advanced export/queue
were completed in subsequent phase-0.4 substages; Local Adjustments remain phase 0.5.


## Included: Ready-made filters / 0.4.8

Natural, Soft Portrait, Landscape, Warm Film, Cool Cinema, Matte, Vintage and Crisp
are original declarative Curve/HSL presets. Hover previews without saving; click
applies one Undo. They preserve per-photo Exposure, WB, Detail, Crop, Optics and
Geometry, including enable states. Reapplying is idempotent. Every changed value
remains editable; Natural resets only Curve/HSL. There is no hidden LUT or effect.

Built-ins are read-only and stored independently of user presets. Export JSON and
import it for an editable copy, or save your adjusted look as a new user preset.
Batch preset application also accepts built-ins and preserves excluded settings.
Three redistributable photographic fixtures, provenance and reviewed spatial/color
regressions supplement RAW acceptance; see [fixture notes](tests/assets/filters/README.md),
[ADR 0016](docs/adr/0016-ready-made-filters.md) and [review](docs/FILTERS_REVIEW.md).
These are authored starting points, not commercial film/camera emulations.


## Included: Edited thumbnails / 0.4.9

Grid and filmstrip now render saved Develop, Detail, Optics, Transform and Crop
settings through the shared calibrated renderer. Autosave and batch apply/Undo/Redo
refresh affected thumbnails without resetting selection or scrolling. Hover and
unsaved gestures do not enter the saved thumbnail cache. Retry keeps its model
binding, so a prior loading error cannot prevent later edits from appearing.

Each worker reads an atomic saved-edit snapshot. Disk keys include source checksum,
saved revision, full canonical edit fingerprint and pipeline version, preventing
collisions between duplicate sources or reused revisions. New/loaded cache entries
use identical JPEG bytes; corrupt or oversized entries are regenerated. Source
hashes are checked around decoding. Two workers, 96 admitted requests, 384px output
and the existing 512MB atomic disk cache remain; performance at scale is phase 0.6.
See [ADR 0017](docs/adr/0017-edited-thumbnails.md).


## Advanced export and queue / 0.4.10

The Export panel supports JPEG, PNG, TIFF8/16 with None/LZW/ZIP, and processed
Linear RGB DNG; sRGB/Display P3/Adobe RGB/ProPhoto; seven resize modes; output
sharpening; metadata/location policies; Unicode text and image watermarks.
Destination/naming/conflict settings can be saved as reusable profiles.
Active/selected-photo export runs sequentially from immutable saved snapshots,
with progress, filenames, completed/total, errors, pause/resume/cancel and
close/update coordination. Originals and catalog state are protected at atomic
file installation. Default PNG output remains byte-identical to the 0.4.9 app.

DNG has edits baked into 16-bit LinearRaw samples; it is not sensor archival.
General RGB/float DNG ingestion remains unsupported. See [export behavior,
limits and CLI options](docs/EXPORT.md) and [ADR 0018](docs/adr/0018-advanced-export.md).
The macOS bundle is ad-hoc signed, without notarization. Release 0.4.13 includes
these completed features. After the requested roadmap review, phase 0.5 resumed
with local gradients; Brush, ranges and boolean composition remain later increments.


## Language packages / 0.4.11

Settings (gear) switches the UI between English, Russian, Simplified Chinese,
Armenian, German, Spanish, Italian and French without restarting. English is the
new-install default; an explicit choice persists. User photo/preset/collection
names and processing IDs are unchanged. See [localization](docs/LOCALIZATION.md).
This extension preceded phase 0.5; local gradients are now implemented.


## Ordinary images / 0.4.12

RAW and ordinary images share Library Add/Copy, Develop, edited thumbnails, presets,
batch edits, export queue and restart. PNG/TIFF preserve transparency; JPEG/DNG
flatten against black. Native ImageIO handles full-image color-managed decoding;
RAW retains its sensor pipeline. See [supported formats and limits](docs/IMAGE_SUPPORT.md).
These requested pre-phase-0.5 extensions are included in release 0.4.13.
The complete local mask workflow is implemented in the 0.5.1 source; see below.


## macOS installer and version / 0.4.13

The application and mounted DMG use the original Luma half-disc logo. Settings,
window title, status bar, Finder and --version share CMake's release version.
The latest GitHub Release provides a matching DMG, ZIP, SHA256SUMS, BUILD_INFO.json
and signed Sparkle appcast. See [release notes](docs/releases/v0.4.13.md).

## Local adjustments / 0.5.1 source

Local adjustments provides Linear/Radial gradients, Brush with erase and stroke
opacity, Luminance Range and Color Range. Combine selections with ordered
Add/Subtract/Intersect operands; sample ranges on the photo, invert selections
and control feather, mask opacity and layer order. All selections support the
eight local Light/Color controls. The colored overlay is transient.

Undo/Redo, export, restart and edited Library thumbnails retain complete masks.
Global presets, XMP, Paste and Sync preserve existing masks. Catalog 11 upgrades
legacy gradients atomically; older apps refuse this catalog. Back it up before
upgrading if rollback is needed. Depth Range requires a real depth channel and
is not exposed without one; AI/Metal remain later roadmap phases.

See [workflow and limits](docs/LOCAL_ADJUSTMENTS.md) and
[phase acceptance](docs/PHASE5_ACCEPTANCE.md). The download link above always
resolves to the latest published release; 0.5.1 publication follows the CI and
artifact checks with the documented qualification exception.
