# Photo and camera RAW support

Open photo, drag-and-drop and Library Add/Copy accept JPEG/JPG, PNG, TIFF/TIF
(8/16 bit), HEIC/HEIF, WebP, BMP and GIF alongside supported Bayer RAW formats.
AVIF is available when macOS ImageIO advertises its decoder. Folder discovery
checks extensions case-insensitively against installed decoder capabilities;
explicitly selected invalid files fail individually through the import journal.
No image codec download, account or new runtime dependency is required.

ImageIO decodes full pixels, converts embedded ICC profiles to the linear-sRGB
working space and retains source precision before Develop. Grayscale and CMYK
sources use native color conversion. Images without a profile use the native
format decoder's default color interpretation. HEIF uses its primary image;
Adaptive HDR gain maps and ISO HDR expand into scene-linear working pixels.
GIF uses the first frame, TIFF the first page. Animation editing, multi-page
editing and auxiliary depth/matte data are outside this change.
When the source contains HDR samples, the default preview-only tone mapping
compresses highlights into the display range through the shared CPU/Metal output
transform. It can be disabled in Settings. Histogram, clipping warnings and
exports continue to use the unmodified scene-linear values.
EXIF orientation is applied exactly once by the shared geometry, including mirrors.
Available photographic EXIF fields populate capture data; missing fields stay empty.
As Shot preserves an ordinary image's existing source white balance. Explicit WB,
Curves/HSL, filters, Detail, Crop, Optics and Transform use the existing pipeline.

PNG/TIFF/WebP transparency is retained in previews and the working image.
Downsampling, spatial adjustments and geometry normalize by coverage to avoid
transparent-edge halos. PNG and TIFF export preserve alpha; JPEG, processed
Linear RGB DNG and Adaptive HDR HEIC flatten against black. DNG remains processed output, not a way to
recover sensor RAW or the original editing latitude from a JPEG.

Library selection, edited thumbnails, presets, batch edits, queue and restoration
use the same decoder as the editor/CLI. Originals remain immutable: Add references
them, Copy verifies a separate byte-identical copy, and export refuses original
paths/hardlinks. Existing catalog/edit schemas are unchanged. Image headers are
checked before raster allocation: up to 20,000 px per side, 100 MP, and 512 MB
encoded input. Corrupt/truncated data fail explicitly; cancellation is cooperative
around native decode and during pixel conversion.

RAW sensor development stays in the pinned LibRaw decoder. Import discovery and
XMP ownership use one case-insensitive registry:

`3FR, ARW, CR2, CR3, CRW, DCR, DCS, DNG, ERF, FFF, GPR, IIQ, K25, KDC, MEF,
MOS, MRW, NEF, NRW, ORF, PEF, PTX, RAF, RAW, RW2, RWL, SR2, SRF, SRW`.

This covers common Bayer cameras from Canon, Sony, Nikon, Pentax/Ricoh,
OM System/Olympus, Panasonic/Leica, Samsung, Hasselblad, Phase One and several
legacy families. A registered extension means Apertone routes the original sensor
container to LibRaw; final compatibility still depends on that camera model,
compression and sensor layout. Recognized RAW TIFF containers always use the same
path and never substitute an embedded preview.

The Develop source panel reports the active decoder, decoder version, source
kind, reconstruction method, CFA when applicable, color path and HDR/headroom
handling. The typed decode result also retains active/raw dimensions, sensor
margins, black/white levels and the number of samples at or above sensor white.
Diagnostics contain no source path or image content and are regenerated from the
current original when the decode cache changes.

Supported sensor layouts remain three-channel integer Bayer plus Apertone's own
identified processed Linear RGB DNG. X-Trans RAF, monochrome RAW, Foveon/layered
RAW and unrecognized RGB/float DNG fail with an explicit error. RAF remains in the
registry for Fujifilm cameras that use a Bayer CFA.

Tests include owned synthetic fixtures, a vendor-independent Bayer routing matrix,
all eight EXIF orientations, ICC reference
tones, grayscale/CMYK, 16-bit ramps, alpha edges through Detail/Optics/Transform,
invalid inputs/cancellation and all output formats. Mixed-format acceptance covers
Add/Copy/duplicates, original hashes, native Library/thumbnails, edit/export/restart,
queue and RAW TIFF routing. The generated Bayer fixtures verify shared routing,
metadata, recursive import and XMP behavior without pretending to reproduce every
proprietary camera compression. Real-body qualification can be added without
shipping private photographs.

See [ADR 0020](adr/0020-common-image-import.md) and [export options](EXPORT.md).
