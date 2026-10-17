# UX defect register

These entries refer to reproducible application defects. Harness setup corrections
are tracked separately and are not counted as application failures or hidden retries.

## LUMA-UX-001 — Original shortcut changes state while decoding

- Severity: P1. Scope: CURRENT. Subsystem: Develop / input / asynchronous loading.
- Regression: `UX-NAV-013`; deterministic generated 6000×4000 PNG, no random seed.
- Steps: open photo A; start loading the large photo B; while `editor.busy` is
  true, press the Original shortcut (`\`).
- Expected: the disabled comparison action leaves Original and transient tools
  unchanged until decoding finishes.
- Actual before fix: `editor.original` became true while the previous session
  remained bound and B was decoding. Toolbar comparison buttons were disabled,
  but the global shortcut and controller setter allowed the transition.
- Reproducer: `tests/ux/run_ux_suite.py --case UX-NAV-013`.
- First-failure artifacts: run `20261016T071927Z/UX-NAV-013/repeat-1` under
  `build-verify/ux-artifacts`: result, actions, metrics, app log and failure PNG.
- Recorded metrics: max main-loop interval 383 ms; catalog integrity passed;
  no original-file mutation. The defect is state handling, not a hard hang.
- Fix: guard the comparison shortcut and `setOriginal` while decoding. The
  existing regression must pass without removing the busy-window assertion.
- Verification: three consecutive PASS runs in `20261016T072702Z`; the large
  decode regression recorded main-loop maxima of 105, 108 and 109 ms. Original
  hashes and catalog integrity passed in all three runs.

## LUMA-UX-002 — Decode progress starves the UI event loop

- Severity: P0 hard hang. Scope: CURRENT. Subsystem: decode progress / QML bindings.
- Reproducer: `UX-NAV-013` proceeds through the generated 24 MP decode after the
  comparison-state fix. Three consecutive runs lost main-loop heartbeat for >5 s.
- Expected: background decoding keeps the UI heartbeat and Cancel path live.
- Actual: every raster scanline enqueued `stateChanged`; a 4000-row input queued
  thousands of complete editor property/binding reevaluations on the UI thread.
- First repeat artifacts: `20261016T072146Z/UX-NAV-013/repeat-1`; independent
  diagnostic run: `20261016T072501Z/UX-NAV-013/repeat-1/thread-sample.txt`.
- Stack evidence: all sampled main-thread stacks were in Qt posted-event/QML
  binding evaluation, repeatedly constructing `parameters`/`correctionStates`.
  The worker decoded in the background; sampled footprint peaked at ~1 GB.
- Original SHA and catalog integrity remained valid in the failed runs.
- Fix: bounded progress publication and a dedicated progress notification, so
  progress does not invalidate all editor settings. Keep the 5 s watchdog and
  record the 2 s UI-stall gate; do not increase timeouts.
- Verification: three consecutive PASS runs in `20261016T072702Z`; the large
  decode regression recorded main-loop maxima of 105, 108 and 109 ms. Original
  hashes and catalog integrity passed in all three runs.


## LUMA-UX-003 — New metadata briefly displays the previous asset preview

- Severity: P0. Scope: ALL. Subsystem: asynchronous preview / asset switching.
- Regression: `UX-PRV-013`, real filmstrip click from a generated small image to
  a generated 24 MP image. A frame observer checks the visible QML Image source,
  Ready status, current metadata path and actual preview provider pixels.
- Expected: metadata B is never paired with visible pixels of A.
- Actual before fix: accepting the decoded session replaced the metadata and
  cleared busy before the first render of B finished; the previous preview URL
  remained visible for at least one polish/frame cycle.
- First-failure evidence: `20261016T073414Z-94501/UX-PRV-013/repeat-1`.
  Max main-loop interval 143 ms; original hashes and catalog integrity passed.
- Fix: invalidate preview readiness when accepting a new session, publish its
  URL only after that session's revision-guarded render completes. Keep the
  monotonic image revision and retain the current preview during same-photo edits.
- Verification: three consecutive PASS runs in `preview-fixed-report`; the busy
  shortcut/decode regression also passed three times. Full qualification remains
  incomplete; these focused runs do not authorize a release by themselves.

## LUMA-UX-004 — Modal batch dialog blocks the Quit shortcut

- Severity: P1. Scope: CURRENT. Subsystem: shortcuts / lifecycle.
- Regressions: `UX-APP-010`, `UX-BAT-012`. Select 1000 generated photos, apply a
  preset, wait for the batch modal to open in the active window, send Cmd+Q.
- Expected: coordinated close cancels/waits and leaves batch atomic.
- Actual: the modal overlay blocks WindowShortcut; the application stays open
  after batch finishes. The heartbeat remains live; this is not a hard hang.
- First failure: `20261016T075141Z-97963/UX-APP-010/repeat-1`; strengthened
  reproducer: `20261016T075413Z-98981`, both `UX-APP-010` and `UX-BAT-012`.
  The `quit_batch_state` action records an active window and opened modal.
- Qt 6.10.3 source confirms modal popups block the WindowShortcut context.
- Fix: ApplicationShortcut for Quit only; retain the restart guard and existing
  coordinated onClosing persistence/cancellation handler.
- Verification: three PASS runs each for `UX-APP-010` and `UX-BAT-012` in
  `quit-fixed-report`; idle Quit and autosave/process-restart Quit also passed
  three times each. Full qualification remains pending.

## LUMA-UX-005 — Photo click leaves search keyboard focus behind

- Severity: P1. Scope: CURRENT. Subsystem: Library focus / selection shortcuts.
- Regressions: `UX-SEL-007`, `UX-SEL-010`, `UX-KEY-003`.
- Steps: enter a filename filter, click a visible photo, press Cmd+A or P/X/U.
- Expected: clicking the photo transfers keyboard focus out of text editing;
  shortcuts apply to the visible Library selection. Typing the same keys inside
  Search continues to edit text without changing metadata.
- Actual: PhotoCard selected the asset but never took focus. Search remained
  active, suppressing Library shortcuts or consuming them as text commands.
- Reproducer artifacts: `20261016T082636Z-10639`, all three focus cases.
  `UX-SEL-011` passed with real letter key events in the same run.
- Fix: give the clicked PhotoCard mouse focus before changing selection.
- Verification: the focus cases and real-key typing suppression passed three
  times each in `focus-fixed-report`. Its unrelated slider failure was retained
  and diagnosed separately as LUMA-UX-006.

## LUMA-UX-006 — Maximum slider handle is covered by the adjustment ScrollBar

- Severity: P1. Scope: CURRENT. Subsystem: Develop panel width / pointer input.
- Regression: strengthened `UX-SLD-001`, actual minimum→maximum→center drags.
- Expected: the whole slider fits its viewport and its maximum handle remains
  draggable. Pointer-pixel quantization is distinct from the default reset value.
- Actual: at the default panel width, the slider was 268 px wide in a 254 px
  viewport. Its maximum handle overlapped the vertical ScrollBar. A press at the
  actual handle center gave the mouse grab to `develop.adjustmentsBar`; Slider
  pressed stayed false and Exposure remained 5 throughout the attempted drag.
- Evidence: `20261016T083403Z-11617/UX-SLD-001/repeat-1`, actions
  `maximum_thumb_press` and `slider_zero_geometry`, failure PNG and metrics.
- Fix: constrain Develop controls/panel roots to available width and allow their
  layouts to shrink; elide a long slider label while preserving the numeric value.
- Verification: `slider-layout-report` passed 18 checks: three runs each of
  SLD-001/003/004, DSC-001/003 and CRP-001. `narrow-fixed-report` additionally
  passed 21 checks of DSC-007, Basic, Crop and Detail at 250/400/250 px.
  Final full qualification remains incomplete.
- Extended reproducer: `UX-DSC-007` drags the real SplitView separator through
  250/400/250 px. Run `20261016T084602Z-13068` found the nested Transform sliders
  still 268 px wide in a 214 px viewport. Basic and Crop controls fit correctly.
- Extended fix: allow the nested Transform layout to shrink and wrap the
  Constrain Crop checkbox label within its actual available width.

## LUMA-UX-007 — Adjustment scrolling steals curve point drags

- Severity: P1. Scope: CURRENT. Subsystem: Curves / pointer capture.
- Regression: `UX-ADV-001`, a real paced add-and-move gesture on the curve graph.
- Expected: the point follows the pointer through the final position, in one Undo.
- Actual: the parent Flickable took the mouse grab midway through the drag;
  curve dragging became false and the point stopped at x≈0.484/y=0.5 instead
  of the requested x≈0.65/y≈0.75.
- Evidence: `20261016T092043Z-16097/UX-ADV-001/repeat-1`, actions
  `curve_pointer_owner` (grabber QQuickFlickable) and `curve_final_points`.
- Fix: preserve the curve MouseArea grab during its editing gesture.
- Verification: three clean repeats in `curve-state-fixed-report`; the complete
  release qualification remains pending.

## LUMA-UX-009 — Curve point selection crosses the photo boundary

- Severity: P1. Scope: CURRENT. Subsystem: Curves transient view state.
- Regression: `UX-ADV-014`, select/move an interior point, save, click another
  filmstrip photo and wait for its own preview.
- Expected: the new photo has no transient curve point selected.
- Actual: the Curves panel kept selected index 1 from the previous photo.
  Three consecutive repeats failed after the pointer-capture fix made the full
  fixture gesture possible.
- Evidence: `advanced-fixed-report`, `UX-ADV-014/repeat-1..3`, including the
  selected-point assertion after the source/preview switch.
- Fix: clear curve selection and dragging when a new photo is accepted.
- Verification: three clean repeats in `curve-state-fixed-report`; the complete
  release qualification remains pending.

## LUMA-UX-008 — Original permits an already active targeted gesture to edit

- Severity: P1. Scope: CURRENT. Subsystem: targeted tools / comparison transitions.
- Regression: `UX-ADV-011`. Start a targeted curve drag, activate Original with
  backslash while holding the mouse, continue moving, then release.
- Expected: Original finalizes the prior gesture and suppresses further targeted
  mutation; Undo restores the state before that gesture.
- Actual: Original was true but the same targeted point changed from y≈0.810
  to y=1 while dragging continued. The controller retained its active target.
- Evidence: `20261016T092043Z-16097/UX-ADV-011/repeat-1`, actions
  `targeted_original_boundary` and `targeted_original_after_pointer`.
- Fix: end the current edit on comparison transition; reject targeted adjustment
  while Original is active, including when a disabled MouseArea retains its grab.
- Verification: three clean repeats in `curve-state-fixed-report`; the complete
  release qualification remains pending.

## LUMA-UX-010 — Export expands tokens inside a literal source filename

- Severity: P1. Scope: CURRENT. Subsystem: export naming / filename data.
- Regression: `UX-LOC-005`, import/open/export a generated PNG named
  `Снимок-%{filename}-{sequence}-{date}-🌅.png` with the `{filename}` template.
- Expected: PNG output keeps that exact literal basename.
- Actual: all three runs produced `Снимок-%{filename}-0001-undated-🌅.png`.
  The sequential replacements interpreted source filename data as further tokens.
- Evidence: `pixel-clipboard-locale-report`, run `20261016T123028Z-30978`,
  `UX-LOC-005/repeat-1..3`, actions, failure screenshots and actual output names.
- Fix: expand only tokens found in the original template, preserving source/date
  replacement values as literal data. Add unit coverage for repeated tokens and
  tokens embedded in both source names and date replacement values.
- Verification: LOC-005 passed three native repetitions in `locale-complete-report`;
  all 39 CTests completed with 38 PASS / one optional private Nikon fixture SKIP /
  zero failures. Focused clang-tidy passed for the naming code and its unit tests.

## LUMA-UX-011 — Symbol-only controls lack descriptive accessible names

- Severity: P2. Scope: ALL. Subsystem: accessibility / primary editing controls.
- Regression: `UX-KEY-012`, activate Qt accessibility locally and inspect actual
  accessible names in Develop, Presets, Local and Library states.
- Expected: icon/numeric controls describe their action or setting.
- Actual: preset creation/actions were named `+`/`⋯`, crop rotations only arrow/
  degrees, White Balance's checkbox had no name, and rating buttons only digits/
  stars. Slider value/range and primary AX tree checks passed separately.
- Evidence: `accessibility-report`, run `20261016T125017Z-35715`, KEY-012 actions
  `accessible_icon_names`, screenshot and the complete first failure list.
- Fix: add localized action/setting names to the controls and all eight language
  catalogs; inspect every language through the public Qt AX interface.
- Verification: `accessibility-fixed-report` passed KEY-012/013/014 three times
  (nine native results). KEY-012 inspects all eight runtime languages in each run;
  language catalog coverage/placeholder checks and focused clang-tidy passed.

## LUMA-UX-012 — Rating/flag changes reset Library scroll

- Severity: P1. Scope: CURRENT. Subsystem: Library metadata/model updates.
- Regression: `UX-SEL-013`, import 1,000 public PNGs, scroll to 60%, select a
  visible card and press `5` to set its rating.
- Expected: the same selected asset and viewport remain visible when the filter
  and filename ordering are unaffected by the metadata change.
- Actual: `contentY` changed from 31,290 to 0; selection stayed intact.
- Evidence: `selection-scroll-repro-report`, run `20261016T132729Z-38728`,
  `metadata_scroll_result`, the first failure screenshot and catalog snapshot.
- Fix: update cached metadata rows without modelReset when membership/order are
  stable; preserve reloads for matching filters and pending query/selection work.
  Cancel/reissue pending pages with a new revision so late old metadata cannot
  overwrite the committed values. Add unit coverage for reset-free updates and
  removal from a rating-filtered result.
- Verification: `metadata-scroll-fixed-report` passed 14 selection/metadata/keyboard
  scenarios three times (42 native results), including reset-free viewport updates.
  Source consistency, build provenance and original hashes passed. Focused clang-tidy
  passed; the rating-filter membership regression is covered by LibraryTests.

## LUMA-UX-013 — Enter/Return does not activate a focused button

- Severity: P1. Scope: ALL. Subsystem: keyboard navigation.
- Regression: `UX-KEY-010`, navigate by Tab to Library's Select all button,
  press Space, Return and keypad Enter while observing the real clicked signal.
- Expected: each key activates the focused button exactly once, as specified.
- Actual: Space emits one clicked signal; Return and Enter emit none. Reproduced
  in all three runs of `keyboard-focus-repro-report`; exact key/focus/click counts
  are retained in `button_key_activation` actions. Build provenance and source
  consistency passed. Qt 6.10.3's default ButtonPressKeys contains Space/Select,
  so the application needs an explicit Enter binding to meet this requirement.
- Fix: a non-repeating application shortcut for Return/Enter, enabled only while the
  active window has a visible, enabled Button in keyboard focus. Text inputs and
  ComboBox popups retain their own key handling; activation uses Button.click().
- Verification: `keyboard-modal-report` passed three repeats with software;
  `filter-keyboard-native-report` passed KEY-010 and seven related keyboard
  scenarios three times using Metal. Coverage includes one clicked signal per
  Space/Return/Enter, held-Enter repeats, text/ComboBox input isolation and the
  focused Cancel button inside a modal dialog. Original failures remain retained.

## LUMA-UX-014 — Leaving Guided retains unapplied endpoint changes

- Severity: P1. Scope: CURRENT. Subsystem: Transform transient tools.
- Regression: `UX-TRN-009`, hold a guide endpoint, move it, press Backslash to
  leave Guided for Original, release the pointer, return to Processed and reopen
  Guided. This is real keyboard/pointer input, without controller mode injection.
- Expected: leaving the tool discards transient endpoints, retaining the chosen
  guide layout and the unchanged persisted edit document/history.
- Actual: Guided reopens with the unfinished endpoint movement. The image edit
  document remains unchanged, but the cancelled draft leaks into the next session.
- Evidence: `transform-extra-report`, run `20261016T142532Z-56189`, TRN-009
  `guided_leave_while_pointer_held`, first failure screenshot/actions/catalog.
- Fix: clear draft endpoints when Guided closes. Store the selected layout as
  explicit transient controller state and bind its ComboBox to that state so
  reopening regenerates the correct vertical/horizontal/combined defaults.
- Verification: `transform-draft-fixed-report` passed 13 functional scenarios
  three times (39 Metal results), including all three Guided layouts and 100
  Auto/Cancel cycles per repetition. Source consistency, build provenance and
  original hashes passed. Full CTest: 38 PASS, one optional private Nikon SKIP;
  clang-format and focused clang-tidy passed. Separate idle-host Auto measurements
  passed three repeats in MEASURE_ONLY mode (`transform-auto-perf-report`).

## LUMA-UX-015 — Update notifications ignore Escape

- Severity: P2. Scope: CURRENT. Subsystem: update notification popup.
- Regression: `UX-NAV-012`, edit Exposure, open the bell, press Escape.
- Expected: the declared CloseOnEscape policy dismisses the popup without changing edits.
- Actual: the popup stays visible; its focus and activeFocus are both false while
  the old Library grid retains keyboard focus.
- Evidence: `navigation-repro-report`, run `20261016T144555Z-61460`, three failures;
  `update_popup_focus`, key actions and screenshots retain the original results.
- Fix: request keyboard focus when opening notifications.
- Verification: `navigation-presets-final-report` passed 19 scenarios three times
  (57 Metal results), including 20 bell open/Escape cycles per NAV-012 run and
  the 255-byte filename at minimum window size in NAV-007. Source consistency,
  build provenance, original hashes and SQL integrity passed.

## LUMA-UX-016 — Long filenames displace Open photo beyond the window

- Severity: P2. Scope: CURRENT. Subsystem: toolbar layout.
- Regression: `UX-NAV-007`, open a valid PNG with a 255-byte filename, resize to
  the supported minimum 1000 × 640 logical pixels.
- Expected: filename elides while every toolbar control stays inside the window.
- Actual: the filename elides at its maximum width, but Open photo moves beyond
  the right edge and cannot be clicked.
- Evidence: `navigation-repro-report`, run `20261016T144555Z-61460`, three failures
  and original screenshots; the image is decoded successfully.
- Fix: allow the filename label to participate in flexible width distribution,
  retaining its zero minimum and 350 px maximum.
- Verification: `navigation-presets-final-report` passed 19 scenarios three times
  (57 Metal results), including 20 bell open/Escape cycles per NAV-012 run and
  the 255-byte filename at minimum window size in NAV-007. Source consistency,
  build provenance, original hashes and SQL integrity passed.

## LUMA-UX-017 — Leaving an old preset clears the newly hovered preview

- Severity: P1. Scope: CURRENT. Subsystem: transient preset preview ownership.
- Regression: `UX-PRS-006`, move the pointer directly Natural → Soft Portrait →
  Landscape. A standalone single-row hover first proves transport and pixel coherence.
- Expected: Landscape remains hovered and shows its own transient preview; applied
  document, catalog and history remain unchanged. Late older render results cannot replace it.
- Actual: Landscape is hovered but presetPreview is false. Qt delivers the new
  row's HoverEnter before the old row's HoverLeave; every old row unconditionally
  calls the controller's global clearPreview, clearing the new row's request.
- Evidence: `hover-owner-repro-report` retains three direct-input failures.
  `hover-trace-report` additionally records HoverEnter/HoverLeave ordering and
  Landscape hovered=true with preview=false. This is independent of the later
  queued-callback race test and is not attributed to physical cursor location.
- Fix: track the hover-preview owner inside PresetPanel; a delegate may clear
  only its own preview. Panel/tab/editor actions still clear the preview globally.
- Verification: `hover-owner-fixed-report` passed PRV-004 and PRS-004/005/006
  three times (12 Metal results). PRS-006 also passed three final native repeats
  in `cocoa-quit-final-report`. Source/binary provenance, original hashes and
  catalog integrity passed; full CTest passed 38 tests with one optional Nikon SKIP.


## LUMA-UX-018 — Slider release overwrites a double-tap reset

- Severity: P1. Scope: CURRENT. Subsystem: Basic/Detail slider reset.
- Regression: `UX-SLD-014`, move Temperature to 25000 K, then double-click the
  slider centre. The public fixture is small; this is a functional input test.
- Expected: Temperature resets to its declared 6500 K default, Custom white
  balance remains selected, other document fields remain unchanged and the
  pointer gesture finishes.
- Actual: Temperature finishes at 13500 K, the range midpoint. The doubleTapped
  signal fires once and temporarily sets 6500, then Slider.onMoved overwrites it
  with 13500 before pressed becomes false.
- Evidence: `slider-reset-diag-report`, run `20261016T214418Z-79147`, retains
  three native failures, unchanged source/binary provenance, original hashes
  and valid SQLite snapshots. `basic_reset_state` records 13500 → 6500 → 13500;
  `basic_reset_double_taps` confirms one actual handler signal. The QtTest
  helper delivers both releases; a missing release is not the cause.
- Fix: retain a pending reset until Slider releases, suppress intermediate moved
  writes during that reset, then apply the declared default before ending the edit.
- Verification: `slider-reset-fixed-report` passed SLD-005/006/014 and DTL-003/004
  three times (15 Metal results). The strengthened SLD-014 requires exactly one
  doubleTapped signal and both pressed/resetRequested false; it passed three
  times in `expanded-oracles-report` and again in `cocoa-quit-final-report`.
  Full CTest passed 38 tests with one optional private Nikon SKIP.


## LUMA-UX-019 — A denied recursive child aborts all readable import siblings

- Severity: P1. Scope: CURRENT. Subsystem: import discovery and durable reporting.
- Regression: `UX-IMP-010`, recursively Add two readable PNG siblings alongside
  an owned child folder whose mode is 0000. The fixture proves opendir returns
  EACCES/EPERM and restores its permissions on every exit.
- Expected: two assets import, the denied folder appears in the visible error
  report and durable failed journal, and the session terminates normally.
- Actual: recursive_directory_iterator throws while entering the child; the
  entire import stops before beginImport, leaving zero assets and no journal.
- Evidence: `inaccessible-import-repro-report`, run `20261016T214804Z-80690`,
  retains three native failures, permission-denial diagnostics, source/binary
  provenance, unchanged original hashes and valid SQLite snapshots.
- Fix: discover each directory independently using error_code, continue readable
  siblings and journal per-path failures atomically with the initial import
  session. Failed entries retain their state through cancellation and recovery.
  Child directory symlinks are not followed; file deduplication and cancellation
  checks are preserved. There is no catalog schema change.
- Verification: IMP-005/010/011/012 each passed three native repeats in
  `expanded-functional-final-report`; IMP-010 passed three more final repeats in
  `cocoa-quit-final-report`. Unit coverage verifies durable initial errors,
  invalid error-map rejection and recovery preserving failed paths. Full CTest:
  38 PASS, one optional private Nikon SKIP. Provenance, originals and SQL passed.

## LUMA-UX-020 — Valid near-zero edits display a negative zero

- Severity: P2. Scope: CURRENT. Subsystem: shared numeric slider labels.
- Regression: `UX-SLD-017`, import and apply a valid sparse JSON preset with
  Exposure=-0.001, then inspect the actual numeric Label after Ready preview and
  durable autosave. This is an external value permitted by the document schema;
  Slider.stepSize does not quantize imported edits.
- Expected: the label displays 0.00 EV at its declared precision, retaining the
  exact -0.001 document, rendered effect and persisted value.
- Actual: the label displays -0.00 EV. The full boundary/numeric inspection passed
  before this assertion, and the underlying value was preserved correctly.
- Evidence: `negative-zero-repro-report`, run `20261017T061411Z-4978`, retains
  three Metal failures with source/binary provenance, original SHA and valid
  catalog snapshots. The first occurrence is also retained in
  `window-wb-export-first-report`.
- Fix: normalize only formatted zero to a positive zero at the existing display
  precision; keep all nonzero text, suffixes and underlying edit values intact.
- Verification: `numeric-scroll-settled-report`, run `20261017T062117Z-7274`,
  passed three complete native inspections with 80 actual numeric-label checks
  per run. Each verifies exact -0.001/0/+0.001 documents and persisted values,
  positive-zero text, Ready preview and one actual clicked signal per preset.
  Source/binary provenance, original SHA and SQLite integrity passed. Full CTest:
  38 PASS, one optional private Nikon SKIP (`window-wb-export-full-ctest.log`).

## LUMA-UX-021 — Tab focuses adjustment controls outside the viewport

- Severity: P1. Scope: CURRENT. Subsystem: Develop keyboard/scroll navigation.
- Regression: `UX-DSC-009`, reach Sharpen Amount with real Tab input, then continue
  through Detail controls without fixture scrolling or direct focus setters.
- Expected: each focused adjustment remains visible and keyboard-reachable.
- Actual: Tab reaches noise.luminanceSlider below the ScrollView viewport while
  contentY stays unchanged. The control has actual active focus but is clipped.
- Evidence: `focus-visibility-repro-report`, run `20261017T063425Z-12429`, retains
  three native failures, real focus traces, screenshots, source/binary provenance,
  unchanged original SHA and valid catalog snapshots.
- Fix: when active focus enters an adjustment, scroll only enough to expose its
  bounds, clamped to the actual Flickable range. Manual scrolling and content
  relayout do not force focus-following; controls outside this pane are ignored.
- Verification: `gesture-import-final-report`, run `20261017T123506Z-16828`,
  passed 19 scenarios three times (57 Metal results), including the actual
  Tab-to-visible-control regression, deep-scroll relayout/language/photo switches,
  keyboard focus/modal input and held Slider transitions. Source/binary
  provenance, original SHA and SQLite integrity passed. Full build and CTest:
  38 PASS, one optional private Nikon SKIP (`gesture-import-full-ctest.log`).


## LUMA-UX-022 — A long source filename hides sidebar preset actions

- Severity: P1. Scope: CURRENT. Subsystem: Develop sidebar layout/navigation.
- Regression: strengthened `UX-VIS-001`, open an owned image with a 255-character
  filename at 1000×640. Retain full source-name wrapping, then reach Copy through
  actual pointer/keyboard input without overlapping the filmstrip or footer.
- Expected: the full filename remains available, sidebar content stays within its
  viewport and preset actions have a usable scroll/focus route.
- Actual: the unscrollable ColumnLayout grows beyond its pane; Copy/Paste overlap
  the filmstrip/footer and catalog details disappear outside the window.
- Evidence: manual inspection of the initial narrow VIS-001 PASS screenshot in
  `visual-remaining-first-report` exposed the missing action oracle. The strengthened
  `sidebar-overflow-repro-report`, run `20261017T125915Z-21622`, retains three native
  failures with source/binary provenance, unchanged original SHA and valid SQLite
  snapshots. The initial narrower PASS is retained as such, without relabeling it.
- Fix: put the complete sidebar in a clipped ScrollView, preserve full filename
  wrapping and normal full-height layout, and share the existing minimal focus
  reveal behavior across both Develop panes. An explicit scrollbar gutter keeps
  text and preset actions outside its hit area. A single deferred callback checks
  both panes; focus outside them and manual scrolling do not force repositioning.
- Verification: `visual-driver-final-report`, run `20261017T131309Z-27651`,
  passed all eleven new cases and DSC-001 on Metal. VIS-001 and DSC-001 each
  passed three further native repeats in `sidebar-driver-native-final-report`,
  run `20261017T132106Z-30385`. Copy is actually clicked, reached by a fresh Tab
  transition, activated by Space, and contained outside the full scrollbar hit
  area. Source/binary provenance, original SHA and SQLite passed. Full build,
  CTest (38 PASS, one optional private Nikon SKIP, 83.90 s), format and focused
  clang-tidy passed. Original intermediate failures remain retained.


## LUMA-UX-023 — Import completion resets the scrolled Library viewport

- Severity: P1. Scope: CURRENT. Subsystem: Library reload and view positioning.
- Regression: `UX-LIB-017`, scroll deeply through 1,000 real imported assets,
  perform actual wheel input while an ungated Add worker commits more files,
  then Cancel and observe the viewport after the actual metadata reload.
- Expected: remain around the same asset identity, retaining selection, all
  prior full documents, originals and the durable import checkpoint.
- Actual: the reload resets the grid to the beginning, losing the distant asset.
- Evidence: `library-anchor-corrected-report`, run `20261017T132723Z-33025`,
  retains three native failures. Each proves physical worker progress across
  wheel input, source/binary provenance, original integrity and valid SQLite.
  The first earlier run stopped at a fixture-only full-card visibility condition;
  that raw report is retained separately, not labeled a production failure.
- Fix: capture the visible asset and its offset before a preserving reload,
  resolve its new row on the metadata worker using the current filter/sort,
  and restore the bounded grid viewport after layout. Superseded reloads and
  new user movement invalidate deferred restoration; filter changes clear it.
  There is no catalog schema change or selection/sort policy change.
- Verification: `library-viewport-final-report`, run `20261017T174218Z-45300`,
  passed all six new scenarios three times (18 Metal results), with two independent
  2,048-source before/after insertion waves and a further actual import completion
  during a held scrollbar per repeat. `library-viewport-affected-report`, run
  `20261017T174723Z-45228`, passed thirteen affected scenarios three times (39).
  Provenance, unchanged source/binary, original SHA and SQLite passed. Unit cases
  cover NOCASE ties, combined filters, missing assets, insertion before the anchor
  and stale reloads. Full build and CTest: 38 PASS, one optional private Nikon SKIP,
  86.59 seconds. Format and five focused clang-tidy checks passed.
