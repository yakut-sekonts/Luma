# Development backlog toward 1.0.0

Implementation continues without intermediate QA by user direction on
2026-10-20. Open reports are not independently reproduced or qualified.
All deferred checks and fixes must be covered in the final pre-release campaign.
Final qualification started on 2026-10-21; newly reproduced failures below retain
their actual execution status rather than the earlier development-only status.
The 23 accepted 0.5.1 qualification gaps remain tracked in
[Issue #48](https://github.com/yakut-sekonts/Luma/issues/48) and
[the release record](releases/v0.5.1-quality.md).

## LUMA-BUG-035 — Filmstrip/Library scroll returns on photo selection

- GitHub: [Issue #49](https://github.com/yakut-sekonts/Luma/issues/49).
- Status: **bounded grid and filmstrip regressions pass offscreen/native; broader qualification remains open**.
- Reported: 2026-10-20, after installation of the 0.5.1 release.
  The recording displays 0.5.1; the exact executable identity is unavailable.
- Reproduction supplied by the user: import photographs, scroll down through
  the Library, then select a photograph.
- Actual: the list jumps back to the beginning after every selection.
- Expected: selecting a photograph preserves the visible asset anchor and its
  offset; opening it and returning from Develop also preserves that viewport.
- Recording reviewed on 2026-10-22: the affected surface is the horizontal
  Develop filmstrip. Two photo switches return it to the initial filename range.
  The earlier Library grid scenario alone did not cover this evidence. See
  [recording observations and hashes](qualification/2026-10-22/scroll-recording.json).
  Original media and extracted frames remain local.
- Source change: opening an asset already present in a loaded metadata page
  updates Active/Selected roles without resetting the model. Card clicks set
  the view's current index before transferring focus; the grid starts with no
  implicit current item. Final qualification additionally reproduced a 10 px
  shift while the grid was hidden after opening a photo. Explicitly retaining
  `contentY` after mouse focus cancels Qt's deferred current-cell reveal without
  disabling arrow-key navigation.
- 2026-10-22: three local offscreen repetitions pass with 176 generated PNGs,
  a metadata-page boundary, two selections, open/return, filename filtering and
  clearing the filter. The oracle checks the actual visible asset/offset,
  transient click motion, absence of model resets and keyboard reveal. Two
  targeted QML mutations are rejected by the expected scroll/reset assertions.
  See [retained failures and retests](qualification/2026-10-22/library-scroll-and-cancel.json).
- Native follow-up on 2026-10-22 passes 3/3 Library runs on Cocoa/Metal, Retina
  DPR 2; neighboring keyboard checks pass 6/6. Source/build provenance and
  originals remain intact, with no QML warnings. See
  [native evidence](qualification/2026-10-22/native-scroll-cancel.json).
- A separate filmstrip regression reproduced a 24.82 px partial-card shift.
  Mouse clicks now retain horizontal position; explicit Left/Right navigation
  reveals the current card after mouse positioning. Final offscreen checks pass
  grid 3/3, filmstrip 3/3, keyboard 6/6 and neighboring selection/scroll 9/9.
  The prior positioning still fails the final anchor oracle. Frame-synchronized
  positions distinguish visible motion from synchronous intermediate property
  changes. See [failures, controls and retests](qualification/2026-10-22/filmstrip-keyboard-followup.json).
- The final native follow-up passes grid 3/3, filmstrip 3/3 and keyboard 6/6 on
  Cocoa/Metal, Retina DPR 2, with intact originals and no QML warnings.
- Physical trackpad/wheel input, other window sizes,
  sort/filter combinations and large catalogs remain part of final qualification. This does
  not establish reproduction of the user's exact installed build or close 1.0 QA.

## LUMA-BUG-036 — Compiled Denoise differs from its source network

- Status: **reproduced synthetic failures resolved by v3 replacement; cross-platform CI retest pending**.
- Environment: Apple Silicon, macOS 27.0.1, Xcode 27.1; pinned source weights,
  coremltools 9.0 conversion, Float32 ML Program and native CPU_ONLY prediction.
- Three synthetic cases (noisy gradient, black, negative/HDR gradient) exceed
  the fixed max-absolute-error 0.0005 / RMSE 0.00005 limits. Maximum observed
  error is 0.2002. Source Float32 versus Float64 on black differs by at most
  2.87e-5, so ordinary source precision does not explain the Core ML discrepancy.
- Disabling layer-norm fusion did not fix the black-input discrepancy. Early
  encoder-prefix comparisons locate the first large deviation at the stride-2
  downsampling convolution. Disabling all conversion passes also failed.
  An exploratory equivalent split/1×1 convolution reduced the black-input error
  to 0.000757 but still exceeded the fixed limits; no experimental adaptation was
  added to the production model recipe. Switching to the neural-network backend
  also remained outside limits (black max 0.000735 / RMSE 0.0000649). Equivalent
  odd-kernel and pointwise-linear experiments did not meet the gate either.
  These diagnostics do not justify changing weights or tolerances.
- Evidence: [numerical report](qualification/2026-10-21/model-numerics.json), `final-denoise-float64.log`,
  `final-denoise-diagnostic-1.log`, and `final-denoise-layers-*.log`.
- Sky passes the same synthetic CPU comparison. This does not establish its
  photographic quality, no-sky behavior, GPU/ANE results or memory limits.

### Follow-up — 2026-10-21

- The same v1 recipe on macOS 15.7.9 / Xcode 16.4 passed the ordinary gradient
  and HDR cases, but still failed black. Evidence: [CI run](https://github.com/yakut-sekonts/Luma/actions/runs/39608476171)
  and [its report](qualification/2026-10-21/model-numerics-ci-v1.json).
- On macOS 27, an isolated 32→64 channel 2×2/stride-2 convolution reproduced the
  boundary error (max 0.03495), independently of the rest of NAFNet. Adaptation
  v2 uses four spatial slices and a 1×1 convolution with the same weights/bias.
  Float64 algebraic, channel/phase-order and odd-size tests pass; the original
  checkpoint layout is preserved. The normal-gradient error fell to 2.45e-6.
- Uniform RGB tiles now pass through exactly. This prevents source-network
  artifacts on zero-variation colors and has no darkness threshold. The tests
  include almost-black noise and a single nonzero pixel so this guard cannot
  hide the remaining nonuniform dark failures.
- [v2 CPU](qualification/2026-10-21/model-numerics-v2-cpu.json): 9/11 cases pass;
  dark noise and black with an impulse still fail. [v2 ALL](qualification/2026-10-21/model-numerics-v2-all.json):
  10/11 pass; dark noise still fails. Max error limits remain 0.0005 / RMSE 0.00005.
  ALL denotes the requested policy, not proof of GPU/ANE execution.
- Selectively disabling convolution/arithmetic fusion did not pass the dark
  impulse comparison. Those experimental converter changes are not retained.

## LUMA-BUG-037 — Source Denoise invents bright structure on nonuniform dark input

- Status: **reproduced synthetic failures resolved by v3 replacement; photographic acceptance remains open**.
- This occurs in the pinned source network before Core ML conversion. A linear
  dark ramp plus noise, whose maximum is 0.00109, produces values up to 0.642.
  A black tile with one 0.001 impulse produces values up to 1.0 and mean 0.0651.
- Evidence: [source-output metrics](qualification/2026-10-21/source-dark-sanity-v2.json),
  generated from the same deterministic inputs as the retained numerical checker.
- The exact-uniform guard in v2 only protects tiles with no spatial variation.
  It deliberately does not skip these nonuniform cases or redefine them as PASS.
- The bundled denoiser remains unqualified. Resolve the source-model behavior
  and verify photographic shadow/detail/noise quality before accepting numerical
  agreement as evidence for release. Matching an incorrect source output is not
  sufficient.
- Follow-up: `check-denoise-quality.py` now makes this a mandatory CI/release
  gate against analytical clean images. The v2 source passes 5/7 inputs and
  fails both nonuniform dark cases, in the full image and the runtime crop.
  [Retained report](qualification/2026-10-21/denoise-quality-v2.json).
  Replicate/reflect padding experiments also produced artifacts and were not
  adopted. Weights, conversion recipe and numerical limits remain unchanged.

## LUMA-BUG-038 — Diagnostic writer prevents the supported CI build

- Status: **closed for the reported build failure — local and Xcode 16.4 CI PASS**.
- [CI run](https://github.com/yakut-sekonts/Luma/actions/runs/39612524379) fails
  compiling `DiagnosticSession`: Xcode 16.4's libc++ has no `std::jthread` or
  `std::stop_token`. Local Xcode 27 builds did not expose this incompatibility.
- Replaced the writer with `std::thread` and a stop flag protected by the same
  mutex as its queue. The destructor still wakes, drains and joins the writer.
- A new test exercises immediate shutdown with queued events, event order,
  clean/unclean final markers, disabled logging and symlink-root refusal.
  It uses the production writer and stubs only crash capture/formatting;
  PLCrashReporter, rotation, load and all diagnostic failure modes are not
  qualified by this test.

### DRUNet and CI follow-up — 2026-10-21

- BUG-036/037: NAFNet was replaced with hash-pinned MIT DRUNet color and a
  per-tile Haar noise estimate. No darkness cutoff, input suppression or relaxed
  quality/numerical threshold was introduced. The fixed source-quality cases
  pass 7/7; native CPU and ALL comparisons pass 11/11 each. The native outputs
  additionally pass the same analytical-clean quality oracle. See
  [source quality](qualification/2026-10-21/denoise-quality-v3.json),
  [CPU](qualification/2026-10-21/model-numerics-v3-cpu.json) and
  [ALL](qualification/2026-10-21/model-numerics-v3-all.json).
- The replacement resolves these exact synthetic reproductions only. Real
  camera noise, fine texture, color and per-tile estimate consistency still
  require photographic/runtime qualification; ALL does not prove device placement.
- BUG-038: [CI run 39616268298](https://github.com/yakut-sekonts/Luma/actions/runs/39616268298)
  built successfully with Xcode 16.4 and passed the diagnostic lifecycle test
  as part of unit/integration. This does not qualify all crash-reporter paths.

## LUMA-BUG-039 — UX import fixtures target the retired LibraryPane properties

- Status: **test harness corrected; four affected offscreen scenarios PASS; full CI rerun pending**.
- CI after the build fix reports 180 UX failures, frequently at unavailable
  `library.importStart`. Since advanced import, `sources` and `destination`
  belong to `library.importDialog`; old fixtures wrote unused dynamic properties
  on `library.pane`, leaving Start disabled.
- Chooser-result fixtures and selection snapshots now use the actual dialog.
  The shared import helpers assert successful property assignment. Subsequent
  import, selection, edits and export continue through production UI controls.
  Native file-panel selection remains an explicit untested fixture boundary.
- The previous failing CI record is retained. This cause does not establish
  that every one of its 180 failures will pass after the fixture correction.

## LUMA-BUG-040 — UX preset fixtures omit new groups and DiagnosticsController

- Status: **source fixtures corrected; affected offscreen cases PASS; full/native retest pending**.
- The old Light-only selection unchecked seven groups but left the four new
  creative groups enabled. Shared fixture selection now covers the current
  group mask, scrolls controls into view, clicks them and asserts the exact mask.
  Localization coverage includes all 12 labels; its native candidate has not
  been rerun here.
- The preset metadata scenario additionally clicked the recapture checkbox
  below the clipped viewport. Actual scrolling and checked-state acknowledgement
  fix the setup without changing the save/recapture/UUID invariants. Its initial
  failure is retained and its affected retest passes.
- The UX context now supplies the real DiagnosticsController with a disabled
  DiagnosticSession, matching unattended startup. This removes the observed
  undefined-context QML warnings; it does not qualify crash capture or diagnostics UI.
- Evidence: [follow-up](qualification/2026-10-21/drunet-ux-followup.json).

## LUMA-BUG-041 — Per-tile noise estimate mistakes texture for Gaussian noise

- Status: **exact bounded reproductions fixed and retested; full photographic acceptance remains open**.
- With seeded sRGB sigma 0.01, bundle v3 over-smooths the portrait and landscape
  fixtures. The portrait's blue error exceeds the noisy input; the landscape
  also worsens gradient and tile-seam errors. Four of six noisy photographic
  cases pass; [the original FAIL report](qualification/2026-10-21/denoise-photographs-v3-failure.json)
  is retained. All nine source/native comparisons already passed, isolating a
  source-quality defect rather than a Core ML numerical conversion error.
- Bundle v4 uses the lower median of absolute Haar HH coefficients divided by
  the Gaussian calibration constant. The estimator is resistant to sparse
  texture/edge outliers and uses exportable topk/amax. Weights, halo, domain
  preservation and all quality/numerical criteria remain unchanged.
- [Native photographic retest](qualification/2026-10-21/denoise-photographs-v4.json):
  **6/6 noisy cases PASS**, **9/9 tiled source comparisons PASS**. Three unchanged
  photographs are observational controls, not additional quality passes.
  Amount 0/0.5/1, alpha, unchanged input, progress, cancellation before/during
  processing and rejected invalid amounts pass with the actual provider.
- The synthetic source gate remains 7/7 PASS; CPU and ALL comparisons remain
  11/11 PASS each. Thirteen adaptation tests, seven synthetic-oracle tests and
  six photographic-oracle tests pass. Eight affected CTest regressions and
  the application build pass. CI and release now require the photographic gate.
- This does not qualify real sensor noise, full-size RAW, every texture, Sky,
  fallback device placement, resource/load behavior or signed installation.
  See [the follow-up evidence](qualification/2026-10-21/denoise-photographic-followup.json).

## LUMA-BUG-042 — Add rejects valid existing long filenames

- Status: **bounded reproductions fixed and retested; native retest open**.
- Add without backup incorrectly applied the generated-destination length rule
  to existing source names, rejecting legal long filenames used by VIS001/003.
  It now uses the source path directly. Copy/Move and backup destinations retain
  their length validation. ASCII/UTF-8 boundary unit cases preserve source hashes
  and reopened catalog paths; affected UX scenarios pass three repetitions.
- File descriptor errors now preserve errno and identify the actual failed path;
  the read-only import destination case passes three repetitions.

## LUMA-BUG-043 — Source directory events reset the Library viewport

- Status: **actual watcher reset fixed; bounded unit/offscreen retests pass**.
- Passive model-reset traces isolate delayed source-directory notifications as
  another reset cause after selection/editing. The old reload emptied and rebuilt
  the model, destroying cards and moving the current viewport (related BUG-035).
- A cancellable background check updates only changed Missing roles on cached
  pages, with revision/closing guards. Directory events do not change catalog
  membership or auto-import external edits. Real removal/restoration verifies
  both role changes and zero resets; four affected UX races pass 3/3 each.

## LUMA-BUG-044 — Metal HSL precision near black and after HDR curves

- Status: **near-black calculation fixed; unsupported combined path uses CPU**.
- The independent native oracle reproduced a near-black saturation error and
  excessive Curve/HSL HDR rounding. Stable black/white formulas fix the first.
  Combined Curve/HSL remains outside the fixed FP32 budget and explicitly falls
  back to the unchanged CPU reference, including direct accelerator calls.
- Seven actual Metal cases and two exact fallback cases pass. Numerical limits,
  alpha preservation and exact preset/removable-source UX pixel checks were not
  relaxed. The fallback may reduce preview speed; full load testing remains open.

## LUMA-BUG-045 — Intermittent PhotoCard localization state invariant

- Status: **open; per-field diagnostics added, cause not yet established**.
- After correcting the obsolete cache-key fixture, one of three local runs
  failed the full editor/history/selection/preview invariant. The original
  aggregate failure is retained; it does not identify which field differed.
- Subsequent diagnostic runs pass 10/10 and 20/20, including a run alongside
  clang-tidy. This does not close the failure. CI now retains the field-level
  failure evidence if it occurs again; native and independent CI retests remain.
- A separate [deterministic control](qualification/2026-10-23/photocard-final-readiness.json)
  establishes an insufficient test-readiness predicate: small interactive and
  final images can have identical pixels while the real final callback is still
  pending. The helper now waits for that completion before pinning the preview
  URL. The complete two-image/eight-language workflow passes 3/3 with the actual
  final delivery retained/released once. This is a harness fix; it does not
  identify the missing field from the historical aggregate failure, which stays
  open. All document/history/catalog/source assertions remain unchanged.

Evidence for BUG-042–045, all original failures, source/binary hashes and exact
scope: [functional release fixes](qualification/2026-10-23/functional-release-fixes.json).

## LUMA-BUG-046 — Targeted cancellation re-enters preview visibility

- Status: **fixed; offscreen regression passes, native follow-up open**.
- ADV009's photo switch produced four QML `visible` binding-loop warnings.
  Loading already ended the targeted edit before hiding the Image; its cancelled
  MouseArea then called `endTargeted` again and re-entered `stateChanged`.
- `endTargeted` now ends only a live targeted gesture. The UX scenario rejects
  binding loops; controller coverage checks no redundant notification and no
  splitting of an unrelated gesture's Undo. Three UX repeats and affected
  controller/integration checks pass.

## LUMA-QA-047 — WB/numeric pixel oracle predates Metal rounding budget

- Status: **oracle corrected with explicit limits; affected offscreen retests pass**.
- SLD016/017 failed bit-exact CPU comparison at one RGB step in 48/one channels.
  Actual Metal replay agrees with those retained UI differences and passes the
  pre-existing linear error limit. Six additional extreme WB unit cases pass.
- The UI oracle now applies the existing RGB <= 1, alpha-exact Metal budget
  only to eligible documents with Metal enabled. CPU-only/incompatible paths
  remain exact; negative controls reject two-step RGB/alpha/geometry errors.
  This is an explicit expectation correction; no product pixel math changed.
- SLD015/016/017 pass three repetitions each. New native follow-up remains open.

The same interval passed eight earlier native regressions and three native
PhotoCard repeats; BUG-045 is still open. See [retained follow-up evidence](qualification/2026-10-23/wb-targeted-native-followup.json).

The later three-minute native interval passed SLD016/017 and ADV009 on
Cocoa/Metal DPR 2 without warnings, completing the bounded native follow-up for
BUG-046/QA-047. It also passed the first phase 0.6 Metal preference workflow.
This does not close BUG-045 or full release qualification; see
[the native/cache evidence](qualification/2026-10-23/phase6-native-preview-followup.json).

## LUMA-BUG-048 — Fit to 100% opens the top-left corner

- Reproduced 2026-10-23 by both new P06-ZOM-001/002 adapters on a 3072×2048 chart.
  The initial viewport retained contentX/contentY=0; its visible center was source
  (383,233), instead of (1536,1024). Both original failures are retained.
- Cause: `onPixelModeChanged` centered before the new content-size bindings had
  settled, so Flickable clamped the requested offset to the previous Fit bounds.
- Fix: defer centering with `Qt.callLater` and align the offset to backing pixels.
  Exiting pixel mode still resets Fit offsets and cancels kinetic scrolling.
- Verified locally offscreen: ZOM001/002 pass 3/3 each at even and odd window
  sizes. Actual scene pixels preserve single-pixel detail, and full CPU provider
  pixels, document/revision, selection and original hashes remain unchanged.
  Related PRG002 export passes 3/3, including actual 1024 interaction preview,
  2560 final Fit preview, exact 3072×2048 sRGB PNG and single-gesture Undo/Redo.
- Status: fixed, bounded offscreen and Cocoa/Metal Retina DPR2 confirmation complete.
  The approved native interval passed all three new scenarios on clean `6056dd2`,
  with no QML warnings and intact original hashes/catalogs.
  Full pan/trackpad, external display and phase 0.6 QA remain open.
- Evidence: [zoom/progressive follow-up](qualification/2026-10-23/zoom-progressive-followup.json).

## LUMA-GAP-049 — Viewer zoom modes and input qualification

- Status: **modes/input implemented; complete Viewer qualification still open**.
- Requirements §16 explicitly requires Fill, 2:1, arbitrary zoom, mouse-wheel
  zoom, pinch zoom and double-click 1:1/Fit. The initial audit found only Fit/100%
  and pan. The follow-up adds Fill, manual 1–1600%, scene-anchored wheel/pinch,
  smooth pan and double-click Fit/100%, preserving rendered pixels and edits.
- New input tests exposed scrolled-content anchor errors and two watchdog exits
  at 1600%. Scene-coordinate conversion and bounded inactive tool Canvas sizes
  fix the reproductions. Smoothing changes did not fix the failure and were
  reverted. Exact/full pixel, history/catalog and watchdog checks remain intact.
- [Evidence](qualification/2026-10-23/viewer-scale-and-input.json): 51/51 bounded
  offscreen regressions, full application build, 9 affected CTest checks and
  runner/policy tests pass. All four input adapters now also pass the fresh
  Cocoa/Metal Retina interval; synthetic gesture events do not qualify physical
  trackpad behavior. See [native follow-up](qualification/2026-10-23/ci-memory-pressure-recovery.json).
- PRV011 and ZOM006–009 overlay mapping at arbitrary zoom remain unqualified;
  tools still operate in Fit. These and other Viewer requirements need separate
  qualification before closing this gap. No 1.0 release gate is waived.

## LUMA-QA-050 — Preset pixel oracle predates the Metal rounding budget

- Status: **cause reproduced; affected bounded offscreen and native retests PASS**.
- The full functional baseline's PRS017 fails after a valid Light-only paste.
  Three fresh runs retain one differing RGB byte at one level, with exact alpha.
  Captured UI bytes equal fresh actual Metal replay; expected bytes equal fresh
  CPU replay. The unchanged linear budget passes (maximum error 2.38419e-7).
- Presets and WB now share the established RGB <= 1, alpha-exact allowance only
  for Metal-enabled, GPU-compatible documents. CPU-only/incompatible paths remain
  exact. Negative controls additionally reject format, color-space and DPR
  mismatches even when image bytes coincide. Full settings/history/persistence
  checks remain unchanged. Failure pixel capture stays enabled.
- Evidence: [transient preview and preset oracle](qualification/2026-10-23/transient-preview-preset-oracle.json).

The subsequent quiet interval passes PRS017 and SLD016/017 on Cocoa/Metal Retina
DPR2, with intact originals/catalog and no warnings; see
[native follow-up](qualification/2026-10-23/native-preview-zoom-worker.json).

## LUMA-QA-051 — Full CPU test oracle blocks GUI watchdog

- Status: **test oracle moved off GUI; 24 offscreen regressions and hosted ZOM001 3/3 PASS**.
- Hosted bdea5bd CI records 203 PASS / 1 FAIL. ZOM001 repeat 1 is killed after
  over five seconds without a main-loop heartbeat. Its sample contains one LCMS
  frame; passing sibling samples show test `reference -> renderCPU ->
  DisplayTransform` on the GUI thread, with 2678/3573 ms stalls. Source/action
  ordering supports oracle blocking, but the failed caller stack is incomplete.
- Independent zoom references now use `std::launch::async`; ICC is captured on
  the GUI thread and only immutable fixture/settings data reaches the worker.
  Timeout cancels and joins, and failures propagate. Real event-loop heartbeats
  continue; no heartbeat is fabricated and no watchdog or pixel limit changes.
- All eight affected cases pass three repetitions offscreen. The existing
  adversarial watchdog control still kills a fake hung process despite PASS.
  This is a harness correction, not a production rendering performance claim.
- Evidence: [native and worker follow-up](qualification/2026-10-23/native-preview-zoom-worker.json).

The replacement hosted `21b39ac` run has 204 executed UX repetitions PASS,
including ZOM001 3/3. Its job remains red because applicable coverage is incomplete;
see [CI evidence](qualification/2026-10-23/ci-memory-pressure-recovery.json).

## LUMA-BUG-052 — Zoom remains disabled after memory pressure recovers

- Status: **fixed; 6 offscreen and 2 native pressure repetitions PASS**.
- Warning/critical correctly reset full-resolution zoom and release cache-owned
  references. On normal recovery, `zoomAvailable` became true but the controller
  emitted no `stateChanged`; Fill/100%/percentage controls stayed disabled.
- Both prescribed pre-fix transitions reproduce controller=true/button=false
  and zero notifications. Notify both pressure directions without rerendering
  the valid Fit preview. Actual recovered 100% clicks render the exact full image.
- Tests retain document/revision/history/selection/original checks, verify three
  reclaimed cache budgets and restore the configured budgets. Injection replaces
  OS notifications only for an empty owned UX controller; no production hook is
  exposed to QML. Real OS pressure, RSS, stress and AI cancellation remain open.
- Evidence: [CI and memory recovery](qualification/2026-10-23/ci-memory-pressure-recovery.json).

## LUMA-QA-053 — Incomplete blocking coverage suppresses independent CI evidence

- Status: **workflow corrected; independent hosted UX execution confirmed**.
- Hosted `21b39ac` executes all 204 implemented bounded blocking repetitions
  successfully. Its 58 missing adapters correctly fail the coverage gate, but
  default step conditions then skip every supplemental regression and static check.
- Independent checks now require successful build prerequisites and a noncancelled
  run. Failures still fail the job; no acceptance criteria or publication gate
  is relaxed. The two new memory adapters reduce that missing-adapter set by two,
  but this does not qualify the remaining phase or full-release scope.
- Evidence: [CI and memory recovery](qualification/2026-10-23/ci-memory-pressure-recovery.json).

Hosted `e5850c1` confirms eight subsequent UX steps PASS after the blocking
step fails, including PhotoCard localization and thumbnail response/away-back.
The job is still in progress at the recorded snapshot; full logs and exact
blocking-case counts are not yet available. This is not a full CI/static PASS.
New workflows upload the small blocking summary immediately, before the longer
supplemental sequence, while retaining the complete later evidence archive.

## LUMA-BUG-054 — Cancel Import disables later catalog maintenance

- Status: **fixed; LibraryModel integration and bounded manual-backup UX PASS**.
- `cancelImport()` cancelled a token later reused by backup/restore, XMP and
  relink. Backup worked before Cancel Import and then failed with
  `Operation cancelled.` until another import reset the source or Luma restarted.
- Separate import and maintenance cancellation lifetimes. Request Close and
  destruction still cancel both; finishing/cancelling import does not stop later
  maintenance. Existing transaction, atomic-file and original protections remain.
- Regression checks backup/restore, explicit/pending/automatic XMP, readback,
  file/folder relink, saved edits/revisions/identity/metadata/collections/hashes,
  and deterministic cancellation of pending maintenance during close.
- UX-P09-BKP-001 enters the official blocking inventory: **3/3 offscreen and
  1/1 Cocoa/Metal DPR2**, actual Settings/Back up now clicks, readable snapshots,
  previous copies preserved, no document/history/original mutation or QML warning.
  Other maintenance UX, crash/ENOSPC and scale scopes remain unqualified.
- Full build, 7 affected CTest checks, runner/policy tests, formatting and
  affected blocking clang-tidy pass. Full hosted CI remains pending.
- Evidence: [import cancellation and maintenance](qualification/2026-10-23/import-cancel-maintenance.json).

## LUMA-BUG-055 — ImageIO XMP lists reject valid creator and keyword entries

- Status: **fixed; native unit regression and six bounded UX scenarios PASS**.
- Valid external XMP and Luma-written sidecars fail with `XMP list must contain
  text.` because ImageIO array entries are string-valued `CGImageMetadataTag`
  objects. The decoder assumed bare `CFString` values. An actual native type
  probe and pre-fix unit/UX failures retain this evidence.
- Unwrap string tags only; preserve creator order/repeats, keyword uniqueness,
  Unicode and original hashes. Nested structures/arrays and over 256 items still
  fail. ImageIO-normalized scalar list spelling reads as one exact text entry.
- UX metadata/XMP cases pass **18/18 offscreen and 6/6 Cocoa/Metal DPR2**, with
  saved-form readback, explicit write/import/cancel/conflict/malformed paths and
  unchanged Develop edits/history/selection. No integrity failures or warnings.
- Full application build, **62 CTest PASS / 1 private RAW SKIP**, formatting and
  affected blocking clang-tidy pass. Timing remains MEASURE_ONLY; recovery/load
  and complete hosted qualification remain open.
- Evidence: [metadata and native XMP lists](qualification/2026-10-23/metadata-xmp-lists.json).

## LUMA-BUG-056 — macOS 15 XMP XML and language interoperability

- Status: **fixed in source; affected hosted unit/integration + UX and native UX PASS; full CI pending**.
- Hosted ImageIO 15 rejects legal leading whitespace/BOM and XML declarations.
  Its language-alternative parser also replaces a non-default language with
  x-default; writing a parsed packet therefore loses another application's text.
- Parse validated UTF-8 XML without external entities, pass its document element
  to ImageIO, and merge supported properties by expanded XML name. Preserve foreign
  properties and non-default language nodes. Serialize without a synthesized
  declaration while retaining packet instructions/comments and CR references.
- Independent XML oracles cover language preservation, namespace aliases,
  repeated writes, Unicode/list order, prologs and ambiguous-input rejection.
  Hosted macOS 15 XMP + Library integration pass 2/2; local full CTest has
  62 PASS / one private Nikon SKIP, with 18 affected offscreen UX repetitions
  and six Cocoa/Metal DPR2 checks PASS.
- [Evidence](qualification/2026-10-23/xmp-macos15-portability.json).

### BUG-045 follow-up — 2026-10-23

A retained local run now identifies the differing field: preview URL changed
from v3 to v4; exact pixels, full document, history, catalog, selection and sources
remain unchanged. Passive pressure observation and per-condition diagnostics are
added; ten subsequent runs pass with normal pressure throughout. No cause is
inferred from those passes. The held-final fixture is now armed after the edited
interactive result, so it cannot capture an unrelated initial-load refinement.
One native Cocoa/Metal DPR2 repeat and three hosted macOS 15 repeats also pass.
The defect remains open; no root cause is inferred from passing repeats.
[Evidence](qualification/2026-10-23/ci-b404ad0-followup.json).

## LUMA-BUG-057 — New sidecar can contain XML-forbidden Unicode

- Status: **fixed; local unit/integration and affected offscreen checks PASS**.
- Valid UTF-8 U+FFFE/U+FFFF was accepted by metadata validation and emitted into
  a new XMP packet without XML validation. An independent parser rejects those
  bytes; the regression fails before the fix.
- Validate generated XML for both fresh and existing sidecars before any file
  publication. On rejection, catalog draft and acknowledgement, source and
  Develop state, prior sidecar and directory contents remain unchanged.
- A real MetadataService regression covers no-existing-file and preserved-file
  paths, with a valid Unicode publication control. Three affected CTest checks
  and 12 offscreen XMP repetitions pass. Full build/format/affected tidy pass.
- [Evidence](qualification/2026-10-23/xmp-generated-xml-validation.json).

## LUMA-BUG-058 — Moving an original's parent folder leaves availability stale

- Status: **fixed; affected unit/integration, 18 offscreen UX repetitions and
  six Cocoa/Metal DPR2 scenarios PASS**.
- A real parent-tree rename did not produce the Missing state while only leaf
  source directories were watched. Observe existing ancestors within the existing
  512-watch limit, reconcile watches after events, and retain model delegates,
  selection and original/catalog bytes.
- Repeated events no longer restart an already pending 500 ms availability check.
  Unit tests cover parent move/restore and subsequent file move/restore with
  ongoing unrelated directory events. The pre-change unit test failed once and
  passed once; that alone does not establish the cause of its individual timeout.
- Six official relink scenarios verify file/tree identity, checksum refusal and
  retry, complete saved state and regenerated edited pixels. All original hashes
  and catalog checks pass without QML warnings. Native picker interaction, volume
  mount lifecycle, network filesystems and crash/load qualification remain open.
- [Evidence](qualification/2026-10-23/relink-missing-originals.json).

## LUMA-BUG-059 — Persisted thumbnail cache exceeds its cap on cache-only reopening

- Status: **fixed; 3/3 offscreen UX, 1/1 Cocoa/Metal DPR2 and six affected
  CTest entries PASS; hosted confirmation pending**.
- A newly created genuine provider returned a valid cached JPEG while retaining
  536,882,181 bytes above the 512 MiB cap. New writes trim, but warm reads previously
  skipped it. The first successful cache hit now validates the cap under the
  existing mutex; subsequent hits avoid directory scans. New writes still trim.
- The two-photo test requires a genuine successful worker/texture response with
  the owned original absent, explicit sparse ballast, deterministic oldest-entry
  eviction, preserved unrelated file and JPEG bytes, actual UI pixels and complete
  document/source integrity. Initial fixtures that could reuse Qt pixmaps are
  retained as harness failures, not accepted as causal evidence.
- RSS/VRAM, large navigation, physical disk pressure, concurrent application
  processes and external enlargement of an active provider's cache remain separate.
- [Evidence](qualification/2026-10-23/thumbnail-cache-bound.json).

## LUMA-BUG-060 — Cancel during derived journal staging installs a hidden result

- Status: **bounded offscreen/native fix passes; hosted and crash recovery remain open**.
- Reproduced by creating HDR through actual UI and holding the real owned SQLite
  write before installation. Cancel cleared the UI, but after the worker finished
  the catalog had four assets instead of three and a new `.dng` remained.
- Recheck cancellation after `stageDerived`, discard the uninstalled journal and
  let temporary-file cleanup run. Installation remains the commit point; a cancel
  after installation must still retain/recover the published result.
- MRG004/005 pass three offscreen repetitions each and one native each, waiting
  for the actual JobHandle to terminate before checking file/journal/catalog absence.
- Evidence and retained failure: [merge workflows](qualification/2026-10-23/merge-workflows.json).

## LUMA-BUG-061 — Per-row merge progress floods the GUI event queue

- Status: **bounded offscreen/native fix passes; large-job performance remains open**.
- The first HDR cancellation run exceeded the existing five-second heartbeat
  deadline. The retained thread sample shows the GUI reevaluating QML bindings
  while the merge worker waits at the controlled storage boundary.
- Progress updates now share their latest value and permit at most one pending
  GUI dispatch per merge job, retaining the existing revision guard. The Library
  also shows alignment, merging and Float DNG writing phases in eight languages.
- Subsequent merge workflows and refusals pass 42 offscreen and 14 native runs.
  No claim is made about unexecuted large-sequence latency or stress workloads.
- Evidence: [merge workflows](qualification/2026-10-23/merge-workflows.json).

## LUMA-BUG-062 — Panorama accepts incorrect finite registration

- Status: **owned horizontal/vertical and no-overlap checks pass; broader photographs remain open**.
- Known 64-pixel horizontal / 48-pixel vertical crops produced 256×197 / 256×192
  output instead of the expected 320×192 / 256×240. Vision returned finite but
  incorrect homographies. The original failure artifacts remain retained.
- Panorama compares content correspondence for the Vision candidate and bounded
  coarse-to-subpixel translations. Acceptance requires sufficient sampled overlap,
  texture and correlation; unrelated crops are rejected before output publication.
  HDR retains its original registration mode because clipped bracket regions are
  unsuitable for this panorama photometric check.
- Both geometries and independently reconstructed scene pixels pass three
  offscreen repetitions and one native each. Maximum measured linear RGB error
  is about .00012 under the fixed .001 budget. Two refusal/recovery adapters pass
  six offscreen and two native checks with no partial output or catalog entry.
- Evidence: [merge workflows](qualification/2026-10-23/merge-workflows.json).

## LUMA-GAP-063 — HDR alignment cannot be disabled through the dialog

- Status: **implemented; bounded offscreen/native and affected CTest PASS**.
- HDR always aligned frames despite the specified option. Add default-on Align
  frames in all eight languages, pass the choice through controller/service/core,
  and retain alignment/deghost options in the derived XMP provenance.
- Real shifted and moving-object Bayer brackets exercise actual controls and
  jobs. Independent Float32 pixels establish measurable option differences and
  lower reference error; originals/source documents remain unchanged.
- Four new blocking adapters and three regressions pass 21 offscreen and seven
  Cocoa/Metal DPR2 runs. Full build, eight CTest checks and mandatory static checks
  pass. Photographic breadth and performance remain separate.
- [Evidence](qualification/2026-10-23/hdr-options-alignment.json).

## LUMA-GAP-064 — Editing tools force Fit instead of supporting zoom/pan

- Status: **implemented; four Cocoa/Metal scenarios and 12 Fit regressions pass;
  local repetitions under memory pressure and hosted confirmation remain open**.
- Four new baseline adapters reproduce the unavailable zoom controls. Crop,
  Guided Transform, Local Adjustments and targeted tone/HSL now retain zoom.
  The viewport measures the exact rendered tool canvas; stable tool renders use
  full source pixels. Flickable/TapHandler do not steal editing gestures.
- Only the visible intersection receives a Canvas backing store; 1600% checks
  cover active and inactive overlays. Pointer mapping, real scene handles,
  stored geometry, independent targeted sampling, Undo/Redo and source hashes
  are checked. Full build, 11 CTest and mandatory static checks pass.
- Large offscreen series: three Crop PASS, nine retained FAIL; eight availability
  failures explicitly record actual memory warning. All four native cases and
  three isolated offscreen cases pass without suppressing OS notifications.
  A further repeat was not started because the actual kernel warning persisted.
- [Evidence](qualification/2026-10-23/zoom-tool-mapping.json).

### LUMA-GAP-065 — Bundled AI Denoise UI and clean restart

Implemented DNS001/002/008 adapters. Actual bundled CoreML inference, visible
processing, Cancel before derived installation, source preservation, Float32
publication, edits/Undo and fresh-process persistence pass 9 repeated offscreen
and 3 Cocoa/Metal DPR2 runs. Full build and six affected CTest entries pass.
Evidence: `qualification/2026-10-23/denoise-ui-workflow.json`. This closes these
bounded local workflows only; model photographs, load, other cancellation
boundaries and full hosted/release qualification remain separate.

### LUMA-GAP-066 — Fresh-process catalog scale and populated virtualization

CAT001/006/011 now open 10k/100k/500k metadata catalogs in a fresh process and
traverse nine actual scrollbar positions, requiring populated visible cards and
bounded QML delegates/model pages. Nine final Cocoa/Metal DPR2 runs pass.
Independent full SQLite integrity checks moved to a test worker after a retained
500k thread sample identified audit work blocking the test GUI. The five-second
watchdog is unchanged. Initial close-predicate failures and weaker placeholder
runs remain in `qualification/2026-10-23/catalog-scale-open.json`. Six affected
UX regressions and seven CTest entries pass. Actual-photo load, sustained use,
latency/resource budgets and complete release qualification remain separate.

### LUMA-GAP-067 — actual AI subject-mask workflow coverage

Added MSK001/003 adapters using the real Apple Vision provider and SHA-pinned
public-domain portrait. Six repeated offscreen and two Cocoa/Metal runs pass,
including exact bitmap Undo/Redo and normal Local Adjustments controls. Five
affected CTest entries, full build and static checks pass. See
[retained evidence](qualification/2026-10-23/ai-mask-workflow.json). Photographic
boundary quality, other provider modes and stress remain outside this result.

### LUMA-GAP-068 — real Remove and semantic-search workflow coverage

REM001/002/003 and SEM001 now execute actual local processing through QML controls.
Final 18 offscreen and four Cocoa/Metal checks pass, including late cancelled-result
rejection, exact Undo/Redo and expected semantic ranking/index deletion. Test-only
RetouchResultProbe exceptions are contained and reported explicitly; shipping
has no observer symbols. Full build, four CTest entries and mandatory static checks
pass. [Evidence](qualification/2026-10-23/remove-semantic-workflows.json) retains
initial outcomes and scope limits. Broad image quality, stress/privacy and final
release qualification remain open; ten applicable blocking IDs lack adapters.

### LUMA-BUG-069 — Hosted neutral Metal comparison intermittently fails

OPEN. Run 39999961136 / a7b30d4: GPU002 repeats 2/3 fail the combined actual Metal
completion/numeric assertion; first repeat passes. 286 PASS / 2 FAIL overall
executed blocking report, with 59 skipped and 31 unimplemented. Ten local
diagnostic repeats plus three final repeats pass. Passive per-result counters
are added; cancellation, empty fallback or numeric difference are not yet proven.
Keep original numeric and completion requirements. [Evidence](qualification/2026-10-23/xmp-atomic-crash.json).

### LUMA-BUG-070 — XMP staging orphan survives process kill before installation

OPEN. Real SIGKILL before rename leaves .luma-xmp-* in the owned original folder;
fresh-process retry preserves original/edits/draft and installs a complete XMP,
but does not remove the old stage. After-rename kill has no stage orphan. Existing
names do not encode durable ownership: do not broadly delete matching files.
Implement ownership-aware cleanup and retain actual-kill evidence. Atomic/retry
contracts pass 9 offscreen + 3 Cocoa/Metal; cleanup is not counted as passed.

### BUG070 fix and persistence qualification — 2026-10-23

Fixed for fully recorded new stages. `StagingFile` holds a separate ownership-record
lock and matches exact pathname/device/inode before recovery removes a stage.
Legacy/unmarked, incomplete records, symlinks and replaced inodes are preserved.
Actual-kill XMP, backup and HDR tests require cleanup; unit tests preserve active
same/other-process workers, replacement files and installed hard-linked bytes.
The initial inode-flock implementation conflicted with SQLite; the failure and
separate-record correction are retained in [evidence](qualification/2026-10-23/persistence-kill-recovery.json).
All eight persistence scenarios pass 24 offscreen + 8 Cocoa/Metal; full build,
nine affected CTest entries, strengthened staging unit and mandatory static checks
pass. No power-loss/external-volume qualification is inferred. Five new blocking
adapters cover catalog, backup, relink, batch and derived registration kills;
two applicable blocking IDs from phases 0.6–0.9 remain unadapted.

### LUMA-BUG-071 — Heavy interaction preview misses the 50 ms feedback budget

The retained native heavy-Clarity runs exceeded 50 ms. Direct-source 512 feedback,
parallel rows, neutral tone fast path, local blur access and revision-keyed QML
parameter maps now pass 30 native inputs in three repetitions. Full 1024 held and
2560 final output retains the existing independent pixel oracle. The strict
performance baseline is a hardware-specific candidate, not whole-release approval.
See [evidence](qualification/2026-10-24/progressive-preview-denoise.json), including
all failed intermediate measurements and the 68/22/4 regression outcome.

### LUMA-BUG-072 — Hosted Core ML Denoise worker stack overflow / watchdog

macOS 15 LLDB captured SIGBUS in __chkstk_darwin -> BNNSGraphContextExecute_v2
from CoreMLDenoise on a default JobManager thread. Inference now uses a synchronous
joined 8 MiB stack without enlarging all pools; stack/exception/ownership tests
pass. The next hosted run no longer reports SIGBUS but fails its unchanged 5 s
GUI heartbeat watchdog. Keep this open until the supervised hosted workflow passes.
A separate LLDB PASS is diagnostic evidence only. Actual local memory warnings
also cancel Denoise and disable full zoom; retain these failures and OS event logs.
No forced-normal monitor or relaxed timing/numeric budget is used to hide them.


BUG072 follow-up, 2026-10-24: supervised hosted run 40028704668 on 44397a9
passes 9/9 DNS001/002/008 with the original five-second watchdog. Joined inference
stack, parameter-map caching and coalesced DNG progress fix the reproduced failure.
Artifact 12534337423 SHA-256 is recorded in release-foundations.json. Full branch
CI and broader pressure/quality qualification remain separate gates.

### LUMA-BUG-073 — Backup destination overwritten by folder notification

Fixed and retested, 2026-10-24. A watched-directory change could overwrite the
completed manual-backup destination in Catalog and backups. Separate maintenance
result state now preserves that path and shows later generic notices below it.
Explicit catalog mutations clear the old result. Regression generates a real
watched-directory event and requires both visible messages. Backup regression:
12/12 offscreen + 12/12 native; final combined daily workflow 9/9 native. Initial
10 PASS / 2 FAIL retained in daily-backup-merge.json. Full CTest: 69 PASS / private
NEF SKIP. DAY010/011 coverage does not qualify native pickers or golden photographs.

### LUMA-BUG-074 — Update installation can overtake a cancelled merge worker

Fixed and retested, 2026-10-24. Actual HDR staging remains active after Cancel clears
aiBusy; the previous update predicate delivered its install callback too early.
The shared editor readiness contract now requires all owned job pools to drain,
plus existing pending GUI/save state. A unit case covers an old running cancelled
worker hidden by a newer terminal handle. Six operation variants pass three times
each offscreen and Cocoa/Metal; full CTest 69 PASS / private NEF SKIP. No actual
Sparkle install/relaunch is inferred. See update-worker-drain.json for the original
cancel failure, separate system-Python audit failure, corrected evidence and limits.

### LUMA-BUG-075 — Copyright-only export crashes on nested XMP overlay values

Fixed and retested, 2026-10-24. Native EXP027 crashed in ImageIO's
CGImageMetadataRemoveTagWithPath / CFRelease. A new export unit fixture with
localized title/caption, keywords, multiple creators and rights reproduced the
same SIGTRAP. Filtered parents are now removed once; descendants of a removed
parent are not mutated separately. Independent siblings keep reverse removal
order, and removal failure aborts export. Unit and actual four-policy native/
offscreen export checks pass; source hashes remain unchanged. The ensuing AppKit
restoration-prompt failures were startup failures, not executed Undo scenarios.
See native-regression-fixes.json for retained evidence and limits.

### LUMA-BUG-076 — Fullscreen reflow resets Library to its first photograph

Fixed and retested, 2026-10-24. Changing GridView columns while Library was hidden
behind Develop replaced the nonzero visible anchor with row zero. Preserve the
user's row/offset across geometry changes, refresh it after actual scrolling and
resolve it again after model reload. A changed Qt originY alone is not a visible
scroll change: acceptance now checks relative scroll plus exact asset/row/offset.
Six native repetitions complete 300 fullscreen round trips; Library/filmstrip
selection and filtering regressions pass six times on each backend.

BUG069/BUG072 hosted follow-up, 2026-10-24: full blocking run 40034163108 records
366 PASS and no failures. GPU002 and DNS001/002/008 each pass three times on tested
merge 7d795c8, preserving the original Metal numeric and five-second heartbeat
contracts. This closes their reproduced blocking failures; whole-release
qualification and the latest local fixes still require the remaining campaign.

### LUMA-QA-077 — Remaining regression adapters used stale preview/input assumptions

- Status: **fixed in test code; targeted native/offscreen checks pass**.
- REC008's correct retained-source preview differed from CPU in one RGB channel by
  one level. Reuse the existing Metal budget; CPU and alpha remain exact.
- VIS012 returned to the Histogram and then clicked the clipped Local toggle over
  the Filmstrip. Each cycle now reveals its actual hit area, checks clipping and
  verifies clicked counts; 20 native and 20 offscreen relayout cycles pass.
- ZOM002/012's unavailable-control failure reproduced with a real OS warning and
  zoomAvailable=false. Release the duplicate oracle FloatImage before input and
  wait for actual availability. Monitoring stays enabled; no forced normal state,
  timeout increase or pixel/data tolerance change. Final recovery/zoom/source-loss
  checks pass 15/15 native + 15/15 offscreen, including separate controlled warning
  and critical recovery cases. Original broad and diagnostic failures remain.
- This does not qualify host memory/load budgets or close unrelated BUG045.

[Evidence](qualification/2026-10-24/native-regression-remainder.json).

### LUMA-QA-078 — System notification prevents native qualification activation

- Status: **open environmental blocker; manual dialog dismissal requested**.
- New DAT004 migration passes three offscreen repetitions, including independent
  post-exit preservation/backup audits and exact 0.5.1 output in both processes.
- Native migration and an independent startup control stop before the scenario at
  `native window active`. Passive AppKit snapshot shows the Luma window visible,
  not minimized and capable of becoming key; foreground PID belongs to
  `com.apple.UserNotificationCenter`.
- The native UI tool refuses that system application for safety. Do not bypass
  the refusal with another automation route or turn startup failures into PASS.
  The user has been asked to dismiss the system dialog; independent work continues.
- Added failure-only diagnostic snapshot; no changes to application code, activation
  behavior, timeout, watchdog or OS security preferences.

[Evidence](qualification/2026-10-24/stable-migration-adapter.json).

### LUMA-QA-079 — Release job budget is shorter than the verified CI workload

- Fixed configuration: release `build-package` now has 180 minutes, matching the
  main qualification job, instead of 120. All tests and signing checks remain.
- Successful run 40034163108 took 7,424 seconds (123 minutes 44 seconds), including
  41 minutes 29 seconds of full clang-tidy. Release additionally packages, mounts,
  verifies and signs the archives. This timing demonstrates insufficient budget;
  it is not a claim that a release job has already timed out or passed.

[Evidence](qualification/2026-10-24/completed-ci-budget.json).

### LUMA-QA-080 — Daily workflow starts selection before a same-result filter reload

- Status: **harness fixed; 5 focused + 30 phase-route offscreen checks PASS**.
- DAY003's initial hosted-route repeat 2 fails with disabled Add to collection.
  Rating-only and rating+flag filters have identical expected members; the old
  readiness predicate accepted the pre-filter model before the 160 ms debounce.
- Wait for `reloadStarted` from the actual filter input, then Library idle before
  selecting. Verify the exact selected IDs after collection creation and await
  the actual membership control. No production or timeout change.
- Both CI workflows explicitly include the ten implemented hosted-compatible
  phase 1.0 IDs; retain native A11Y002 as a separate requirement.
- [Original failure and retest evidence](qualification/2026-10-24/phase1-ci-and-candidate-signature.json).

### LUMA-QA-081 — Import completion assertion races directory-watch status

- Status: **harness fixed; 3 complete 50-cycle retests PASS**.
- The original IMP019 failure has correct terminal SQL counts and originals; its
  screenshot shows the source-folder notification replacing the shared summary.
- Observe the real importing true-to-false transition and assert its exact summary,
  report and error. Preserve independent SQL/file/hash checks and one completion
  per operation. No production status policy or timeout changed.

### LUMA-BUG-082 — Import Cancel closes without a rejected result

- Status: **fixed; bounded and repeated offscreen checks PASS; native pending**.
- Actual Cancel emits one clicked/opened/closed sequence but no `rejected`; the
  per-field diagnostic preserves all other true form/document/history checks.
- Use `Dialog.reject()` for Cancel. IMP001 now checks both button and Escape
  through real inputs with exactly one rejection/close and no acceptance.
- Three 200-cycle Import and Export runs pass, and the blocking check passes 3/3.
  Production build, CTest 71 PASS/1 private NEF SKIP and affected tidy pass.
- [Initial failures, diagnostics and retests](qualification/2026-10-24/stress-import-cancel-fixes.json).

### LUMA-QA-083 — History chaos uses an exact-only pixel oracle for Metal

- Status: **harness fixed; original 10,000-action replay PASS**.
- The step-200 checkpoint has exact full document/history/autosave and one RGB
  channel differing by one level. Diagnostic reproduction and images are retained.
- Reuse the established GPU-compatible Metal pixel budget, with exact CPU/alpha,
  geometry/color space/DPR and existing oracle negative controls. No production
  render, timeout, action schedule or checkpoint frequency changed.
- The byte-identical original replay completes 100 durable checkpoints and the
  final retained-history sweep. This does not qualify general chaos or soak.

### LUMA-QA-084 — Offscreen routing attempts Cocoa-only performance contracts

- Status: **runner classification fixed; native measurement still blocked**.
- NAV002, PRV002, ADV012 and PERF006 explicitly reject non-Cocoa in their existing
  adapters. The first offscreen run preserves these four precondition failures.
- Register their existing platform contracts before fixtures/processes launch.
  Real CLI produces four unsupported-backend skips, exit 1, INCOMPLETE and
  NOT_QUALIFIED. Twelve routing tests pass; native selection remains eligible.
- FLT002 and TRN013 have offscreen functional passes, not native SLO acceptance.
- Fresh native startup remains blocked by UserNotificationCenter at 01:56 UTC.
  Do not bypass the system-app refusal or change startup timeouts to call it PASS.

[Original failures, replay and backend evidence](qualification/2026-10-24/history-chaos-and-native-contracts.json).

### LUMA-BUG-085 — Publisher does not verify the identity embedded in the ZIP

- Status: **fixed; nine reproduced negative cases reject before GitHub access**.
- Outer checksums/model manifests can be valid while ZIP BUILD_INFO/plist are
  stale, inconsistent or absent. The original publisher accepts all nine owned
  synthetic counterexamples; no real release was published by those tests.
- Validate embedded provenance against release metadata and exact plist identity.
  Compare mounted installer regular files, executable bits and symlink targets to
  the ZIP, including the actual executable/resources/frameworks and model files.
- Ten identity controls reject tampering, missing/extra files, changed executable
  permissions, altered symlinks, ambiguous ZIP paths and substituted metadata.
  Five affected CTest targets pass. The actual older candidate comparison passes
  for 2,350 members; it does not qualify current/final installation or upgrade.
- [Reproduction, tests and actual candidate evidence](qualification/2026-10-24/release-package-identity.json).

### BUG-045 attribution follow-up — 2026-10-24

Fifty unchanged-source offscreen repetitions pass, with normal memory pressure in
all observed callbacks and exact source/catalog invariants. No original failure
cause was captured. Keep the defect open; do not infer a fix from these passes.
