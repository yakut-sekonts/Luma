# Phase 0.8 source development

Status: **implemented initial workflows; unbuilt and unqualified** on
`codex/1.0-development`. No intermediate binary or release is published.

## HDR

Library → Photo actions → HDR merge accepts 2–9 distinct bracketed originals.
Exposure normalization uses shutter/ISO/aperture; absent or invalid exposure
metadata is refused. The median exposure is the reference. Native Vision
homographic registration uses bounded previews, then the core merges full-size
linear RGB frames sequentially. Bright/dark confidence and reference-relative
motion rejection control contributions. Deghosting is adjustable.

The output uses reference bounds and a new Float32 Linear RGB DNG. It does not
bake the individual photos' Develop edits. Provenance lists source catalog IDs
and checksums without their original filesystem paths. Cancellation/revision
guards prevent stale UI activation; no source is overwritten.

## Panorama

Library → Photo actions → Panorama merge accepts 2–24 overlapping originals,
ordered by capture time, then filename/ID. Adjacent frames are registered and
their transforms composed into the reference plane. Overlap luminance ratios
provide bounded exposure compensation. Perspective, Cylindrical and Spherical
projections use an explicit per-frame horizontal field of view.

The current seam treatment is edge feather blending. Default output is the
largest fully covered rectangle. Optional boundary warp stretches contiguous
row boundaries, limits stretching to 2×, and refuses interior holes. It still
crops sparse top/bottom coverage. It does not synthesize pixels with generative
fill, optimize a graph-cut seam, perform multiband blending, or support 360°
wraparound. Homography poles and unsafe canvas dimensions are refused.

Registration currently requires equal upright input dimensions. Canvas limits
are the existing 100 MP / 20000-pixel guards; these do not establish peak memory
usage. Only one additional full frame is loaded at a time, but accumulation,
coverage and output buffers coexist. Wide-angle/large-parallax quality and the
projection convention need final photographic qualification. More advanced
stitching is contingent on that evidence; no commercial-quality equivalence is
claimed.

HDR results can be used as panorama inputs through the same Float DNG path.
There is no automatic bracket grouping or one-click HDR panorama orchestration.

## Durable derivative registration

Catalog migration 15 adds `derived_images`. A journal entry containing the
destination/checksum/dimensions precedes no-replace file installation. Registration
and removal of the journal entry commit together after installation, including
when the UI cancels or closes at that point. Startup recovers completed outputs
of dead owners, refuses changed files and preserves unresolved evidence.

Pre-installation temporary-file cleanup after process termination is still
separate housekeeping; it must not guess ownership from a filename prefix.

## Final qualification

No compile, runtime, image comparison, unit/UX test, benchmark or static-analysis
run has been performed for this source checkpoint. Final coverage must include
real brackets, moving subjects, negative/highlight float round trips, registration
direction/EXIF, overlap/order/focal cases, seams/warp, cancellation, failure and
restart across each file/DB commit boundary, large canvases and memory pressure.

Reference: [Apple — Aligning similar images](https://developer.apple.com/documentation/vision/aligning-similar-images).
