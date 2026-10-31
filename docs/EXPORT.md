# Advanced export and queue — phase 0.4.10

The Export panel exports the active photo or the Library selection to a local
folder. Settings and named reusable profiles are versioned in `export-settings.json`
next to the catalog, using a lock, atomic replacement and conflict detection between
application instances. Startup does not create a settings file. Profiles include
all settings; changing them never changes a running queue.

## Files and color

JPEG quality 1–100, PNG, TIFF 8/16 bit with None/LZW/ZIP compression, processed
Linear RGB DNG, and Adaptive HDR HEIC are available. Output uses embedded sRGB, Display P3, Adobe RGB
(1998), or ProPhoto RGB profiles from the operating system. DNG stores the selected
linear primaries with D50 calibration and an sRGB rendered preview. JPEG and DNG
flatten transparent Transform corners and source-image transparency against black;
PNG/TIFF preserve coverage. These exports also accept ordinary image sources in
0.4.12; see [image support](IMAGE_SUPPORT.md).
DNG is uncompressed 16-bit LinearRaw with edits baked into samples. It is not a
copy of camera sensor data, does not retain an editable Apertone adjustment stack,
and is not an archival replacement for the original. Apertone reimports its own
identified DNG calibration; general RGB/float or multi-illuminant DNG remains
unsupported. DNG requires at least 22 px on each side for the LibRaw roundtrip,
and its unpacked dimensions must fit 512 MB including LibRaw's allocation border.
Integer output clips values outside the selected space's representable range.
Adaptive HDR HEIC requires macOS 15 or later and uses the linear sRGB working
space. ImageIO creates an SDR-compatible base and ISO gain map from the rendered
float image; the exporter verifies the completed gain-map auxiliary data before
atomic installation. Transparent areas flatten against black. This lossy format
retains HDR headroom but does not promise exact scene-linear sample round trips.

Resize preserves the post-Crop/Transform aspect ratio: original, width, height,
long/short edge, megapixels or percentage. Dimensions round to the nearest pixel;
maximum 20,000 px per side and 100 MP, with the lower DNG memory bound above.
Downscaling uses coverage-aware area sampling, upscaling uses bilinear sampling.
Output sharpening follows resizing and precedes watermark/color encoding: Screen,
Matte, Glossy, each Low/Standard/High. Off is an exact bypass.

## Metadata and watermark

All copies available photographic EXIF/IPTC/XMP fields. Geometry, color and file
encoding declarations are regenerated for the output. Opaque MakerNotes, embedded
previews, sensor-layout fields and camera/Apertone processing instructions are excluded.
Copyright Only uses an attribution allowlist. Remove Location removes GPS and
location-bearing IPTC/XMP fields, including structured LocationCreated/LocationShown.
It does not inspect free-text captions for locations. None transfers no source
metadata; format/color information needed to interpret pixels remains present.
Unavailable or unreadable source metadata produces an explicit error.

Text supports Unicode in a white Helvetica Neue watermark. Image watermark accepts
ImageIO-readable local images, respects their ICC/orientation/alpha, and limits
input to 64 MB and intermediate raster to 4096 px. Opacity 0–100%, size 1–100%
of output long edge, margin 0–25% of short edge, and nine anchors are available.
The queue copies image watermarks once during planning. Raster watermark and
output image composite in linear RGB; no source image is modified.

## Naming, conflicts and queue

Templates accept `{filename}` (source stem), `{sequence}` (four digits in filename/
asset-ID order), and `{date}` (capture timestamp as UTC yyyy-MM-dd, or `undated`).
The selected format adds its extension. Paths, unknown tokens and overlong results
are rejected. Skip preserves existing files, Unique appends `-2`, `-3`, etc., and
Replace explicitly allows replacement. Duplicate planned destinations under Skip/
Replace fail planning; add `{sequence}`. Planned identities conservatively fold
Unicode/case for macOS. A newly created conflicting destination is never silently
overwritten by Unique; its job reports an error and subsequent jobs continue.

Planning waits for active-photo autosave and takes a consistent saved asset/edit
snapshot per photo. It releases the editor after planning. Jobs run sequentially
through the shared SessionService renderer. The queue shows current filename,
per-file progress, completed/total, success/skip counts and per-file errors.
Pause completes the active file, then waits; Resume continues. Cancel interrupts
active work cooperatively and drops pending jobs, preserving committed files.
Close and updater restart cancel and wait for cleanup. Queue state is session-local.
Completed counts include failures/skips; an interrupted unfinished job is not counted.

Every output is fsynced in a temporary file and installed atomically. No-overwrite
installation uses `link`, so a concurrent destination writer cannot be overwritten.
The final installation holds a short catalog writer transaction and rechecks all
registered originals, including filesystem aliases; catalog/state and watermark
inputs are protected. Application state, originals and finished outputs are never
removed by cancellation. External modifications to RAW sources fail checksum/stat
verification. This is local export, without cloud publishing or later phase masks.

## CLI

```sh
build/bin/apertone export /absolute/output.tif --catalog /absolute/catalog.sqlite \
  --format tiff16 --compression zip --color-space prophoto \
  --resize long --size 3000 --output-sharpen matte --output-amount standard \
  --metadata remove-location --watermark-text '© Author' --watermark-anchor 8
```

Format: jpeg/png/tiff16/tiff8/dng/heic-hdr (inferred from extension by default).
Color: srgb/p3/adobe-rgb/prophoto. Resize: original/width/height/long/short/mp/percent.
Metadata: none/all/copyright/remove-location. Watermark: `--watermark-text` or
`--watermark-image`, plus `--watermark-opacity`, `--watermark-size`,
`--watermark-margin`, `--watermark-anchor` 0–8 in row-major order.
`--quality` affects JPEG and HEIC; `--overwrite` explicitly permits replacement.
Reusable profiles and multi-photo queue controls belong to the desktop interface.
