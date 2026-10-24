# ADR 0029 — Auto and sampled white balance

Status: accepted for source implementation; unbuilt and unqualified.

Requirement §18 adds Auto and a neutral eyedropper to the existing As Shot,
Temperature/Tint and illuminant presets. Keep the EditDocument schema and the
established render/export transform unchanged.

## Decision

Extract the existing manual gain function into `core/color/WhiteBalance` and
use it both in DevelopPipeline and the estimator. Solve for camera-channel gain
ratios that make `cameraToSRGB * gains * neutral` achromatic. Search the supported
reciprocal-temperature/Tint rectangle deterministically, then refine locally.
Store the fitted Temperature/Tint as Custom; Auto is an explicit action rather
than a live operation rerun on open, export, crop changes or preset application.

Use the cached source preview, including optics and retouch before global WB,
tone/detail, local layers and display overlays. Map displayed coordinates with
CropGeometry; interpolate straight RGB with coverage weights. Auto averages a
bounded grid over the current crop under a gray-world assumption. The eyedropper
averages a small patch and requires a usable center. Reject invalid, transparent,
very dark and near-clipped samples using As Shot working RGB. This threshold is
a conservative preview heuristic, not a sensor clipping detector.

Run sampling/fitting in a cancellable worker. Capture image, metadata,
calibration and edits by value/shared immutable ownership; admit completion only
for the same operation, load/edit/history generation and idle history state.
The picker uses Fit and a stable source-preview canvas. Apply both parameters in
one history entry through ordinary autosave, with no changes to source pixels.

## Consequences

The estimator matches Luma's existing reference white-point model; it does not
claim a calibrated camera illuminant measurement, Adobe matching, mixed-light
correction or perceptual neutrality after creative HSL/local adjustments. A
range/model mismatch is visible in the UI. Scene content and source compression
can bias Auto; users can adjust the fitted values or return to As Shot.

No new persistent modes, image mutations, downloads or models are introduced.
The eight language packs include progress, guidance, errors and history labels.

All compilation, numerical/image-quality, race/lifecycle and native UX checks
remain deferred to final 1.0 preparation. See the white-balance sections of
PHASE9_DEVELOPMENT.md and FINAL_1_0_READINESS.md for the pending campaign.
