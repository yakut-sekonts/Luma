# Phase 0.6 implementation

Status: **in progress — source only, no build, tests or QA executed**.
Development branch: `codex/1.0-development`. The source has advanced to 0.7.0;
the published stable release remains 0.5.1. No release tag or installer is
created from this implementation checkpoint.

Qualification update, 2026-10-23: the historical source-only checkpoint above
has entered final QA. The application builds; the bounded Metal numerical
oracle, initial cache contract and actual backend-preference UX checks now pass.
Most phase 0.6 workflows/load/hardware cases remain unqualified. Current results
and limits are recorded in [final readiness](FINAL_1_0_READINESS.md), not inferred
from the source-availability capability manifest.

The subsequent zoom/export follow-up fixes Fit-to-100% centering after content
size bindings settle, with backing-pixel alignment. Three new official adapters
pass 9/9 local offscreen runs on a 3072×2048 chart: pixel mapping, center-preserving
round trips and full-resolution PNG export after a real interaction preview.
A subsequent authorized native Cocoa/Metal Retina DPR2 run passes all three.
Pan/physical-input, external-display, latency and memory-pressure gates remain open.


The subsequent completion-ordering/photo-switch follow-up adds three adapters:
9/9 offscreen repetitions and 3/3 authorized Cocoa/Metal Retina DPR2 cases pass.
The actual obsolete render callback is rejected after a newer image and while
the next photo load is pending. Entering 100%, panning and switching photos
resets zoom/pan to Fit and preserves saved edits. The interception is UX-only
and absent from shipping; image rendering is explicitly CPU, with the native
Metal Qt scene. These regressions now run in CI/release workflows. Phase 0.6
had seven official UX adapters at that checkpoint. PRG007/008 add both directions
of Original/Processed stale-result rejection and current/obsolete preset-hover
final delivery. Their initial six repeated offscreen checks and the later
42-case affected preset/WB/transient regression pass. A subsequent quiet interval
passes both additions plus PRS017/SLD016/017 on Cocoa/Metal Retina DPR2.
Hosted CI later exposes a synchronous CPU oracle stall in ZOM001. Moving only
the independent test reference to a worker preserves the watchdog and exact
pixels; all eight affected cases pass 24 offscreen repetitions. Replacement
hosted CI remains required. See [native/worker evidence](qualification/2026-10-23/native-preview-zoom-worker.json). Phase 0.6 now has nine official adapters of 58.

The Viewer source audit confirms GAP049: Fill, 2:1, arbitrary scale, wheel/pinch
zoom and double-click Fit/1:1 from requirements §16 are not implemented by the
current boolean Fit/100% interface. This is missing functionality, separate from
unadapted tests. See [the backlog](DEVELOPMENT_BACKLOG.md#luma-gap-049--viewer-zoom-modes-and-input-remain-incomplete)
and [evidence](qualification/2026-10-23/transient-preview-preset-oracle.json).
This is not complete phase qualification.

## Source changes

- `ByteLruCache` retains immutable values under a byte budget. Eviction releases
  only cache references, not values still owned by active jobs or the viewport.
  Generation tokens prevent work started before a purge from repopulating it.
- `SessionService` caches decoded 2560/1024 previews by source checksum and
  decoder version. Each open still hashes the source and reads current catalog
  edits. A separate bounded cache supports on-demand full-resolution decoding.
- Rendered preview identity includes asset/checksum, serialized settings and
  masks, requested resolution, display ICC profile, diagnostic/overlay state,
  backend choice and pipeline version.
  The later Histogram integration stores image/analysis together, accounts for
  histogram storage and adds clipping mode to pipeline-v3 identities. See
  [Histogram design](adr/0028-preview-histogram.md); no latency claim is made.
- Ordinary interactions first request a 1024 preview and refine after settling.
  Releasing a gesture requests final quality. Geometry and sampling tools retain
  their existing stable canvas resolution.
- The 100% viewport requests full decoded pixels and maps one output pixel to
  one display pixel using the screen DPR. It supports panning and scrollbars.
  Unsupported full-resolution geometry or render failure returns to Fit with
  an explicit error. Full-frame decode/render still uses the existing 100 MP
  guard; viewport-tiled processing is not implemented.
- Preview preferences persist a 64–2048 MiB cache budget, default 256 MiB, and
  Metal enablement. Source caches receive 75% and rendered output 25%.
  This is a retained-cache budget, **not a cap on total process RSS**.
- A macOS memory-pressure dispatch source evicts optional caches, rejects
  pending cache insertions, exits 100% and replaces it with a Fit preview.
  Cache retention resumes when the OS reports normal pressure.
- Metal accelerates WB, camera-to-sRGB, Exposure, Tone, Saturation, Vibrance,
  parametric/point Curves or all eight HSL bands. The point-curve LUTs and
  extrapolation slopes come from the CPU pipeline's PCHIP tables.
  It uses precise math and bounded 262144-pixel staging chunks with cancellation
  between submissions. Initialization/allocation failures and command failure
  use the CPU reference. Qualification on 2026-10-22 found FP32 rounding amplified
  beyond the fixed linear-light error budget when active Curves and HSL are
  combined on HDR input; that combination now uses the CPU reference. The HSL
  shader also uses stable saturation/chroma expressions near black/white.
  This preserves processing behavior at the cost of acceleration for the combined
  controls; it does not waive numerical limits. Detail, local masks, geometry,
  optics and ICC conversion remain shared CPU stages. Export remains CPU.

## Remaining work

- Further independent GPU passes remain optional extensions; current GPU
  coverage includes global Light/Color and independent Curve/HSL processing;
  the combined Curve/HSL path remains on CPU.
- Assess whether full-resolution tiled processing is required on 8 GB machines
  during the final campaign; do not claim a total memory ceiling or latency.
- At pre-1.0 qualification: compile all targets, verify image equivalence with
  CPU including HDR/negative values, alpha, masks, disabled corrections and ICC;
  exercise progressive cancellation, photo/profile switches, 100%/Fit, cache
  eviction, pressure recovery and all language packages.
- Include 8/16 GB latency/resource and large-catalog campaigns at final QA.
  No equivalence, performance, memory or UX results are claimed at this checkpoint.

The dedicated development branch is excluded from intermediate macOS CI.
Qualification must use a separate candidate branch so the existing mandatory
CI and release workflows run before publication. Historical 0.5.1 gaps and
new reports remain in [the development backlog](DEVELOPMENT_BACKLOG.md).

Metal implementation reference:
[Apple — Performing calculations on a GPU](https://developer.apple.com/documentation/metal/performing-calculations-on-a-gpu).
