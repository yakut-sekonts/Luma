# ADR 0028 — Preview Histogram and output clipping

Status: accepted for source development; unbuilt and unqualified.

## Context

Requirements §16–18 call for RGB/luminance histograms, clipping indicators, five
tone controls through the histogram and Option preview for Whites/Blacks. The
desktop renderer already composes image processing, geometry, monitor conversion
and transient mask/Detail views with cancellation and a byte-bounded cache.

Computing a histogram from QImage would count monitor-dependent colors, mask
overlays and already quantized/clamped pixels. A second full processing pass would
duplicate expensive work and complicate synchronization with the visible preview.

## Decision

Add an opt-in PreviewAnalysisOptions argument to the shared CPU renderer and an
optional ImageHistogram on its result. Analyze the final processed linear-sRGB
pixel before monitor conversion/overlays. Use up to 512 × 512 evenly spaced cell
midpoints on the current output canvas, weighted by coverage. Map clamped RGB and
linear luminance through the existing sRGB transfer function into 256 bins. Keep
unclamped samples for clipping decisions: all channels <= 0 for shadows, any
channel >= 1 for highlights. Skip zero coverage and reject non-finite statistics.

Report estimates explicitly. The regular sampling grid can miss tiny features or
alias periodic content, and lower-resolution processing changes the distribution.
Luminance uses the same working-space coefficients as Develop. This histogram is
output-referred, not sensor-channel analysis or export-profile soft proof.

Classify every covered rendered pixel for optional clipping overlays, retaining
only one row of flags. Apply red/blue UI colors after monitor/mask composition,
preserving alpha. No extra full-resolution image/mask is retained. Do not analyze
or overlay Detail diagnostic images. Existing non-desktop callers keep the default
disabled options; export does not read the desktop view state.

Cache QImage and its analysis together in RenderedPreview; include the inline
histogram in byte accounting. Update pipeline identity and include clipping mode.
Publish both under the existing render-generation guard, with a pending indicator
and explicit clearing on load/failure. QML subscribes to histogram notifications
instead of repainting/rebuilding bin data on every general editor state change.

Use five equal tone zones and keyboard alternatives. Controller-owned gestures
validate parameter membership/enabled state, finish transient tools, capture the
base value/journal revision and use existing setParameter/Undo/autosave. Late events
after a changed journal or finished gesture are ignored. Ending an external edit,
load, Undo/Redo/reset or application deactivation invalidates the gesture.

Reuse the existing Option-key lifecycle for Whites/Blacks, with temporary clipping
mode overriding session toggles. Restore toggles on release/end/deactivation;
temporarily suppress clipping while Detail diagnostics are active. Nothing in this
feature changes EditDocument, catalog schema, preset/version codecs or export options.

## Deferred qualification

No build, test, static analysis, formatting check, UI launch or benchmark has run.
Final QA must exercise independent numerical fixtures, transparency/geometry,
display ICC invariance, CPU/Metal, sampled-versus-complete limitations, cache and
cancellation races, gesture/keyboard/Option lifecycle, all languages/minimum size,
export/original invariants and resource/latency costs on supported Macs. The older
native row instrumentation keeps analysis disabled by default; extend its coverage
for the new opt-in path during final qualification. No realtime performance or
image-quality acceptance is claimed by the source implementation.
