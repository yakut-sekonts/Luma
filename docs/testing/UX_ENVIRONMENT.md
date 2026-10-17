# Qualification findings outside production controller logic

These findings keep their original FAIL evidence. A later passing repeat does
not retroactively remove a watchdog event or qualify the full current scope.

## macOS clipboard waits during Qt startup / activation

The blocking run `20261016T121010Z-22497` recorded two watchdog failures in
`UX-MSK-022/repeat-1..2`, before any mask input was sent. Original hashes and
catalog integrity remained intact.

- Repeat 1: the main thread was in Context's initial activation wait, then
  `QQuickTextInput::q_canPasteChanged` → `QMimeData::hasText` →
  `CFPasteboardGetItemCount` → synchronous XPC wait.
- Repeat 2: the main thread was still in `QGuiApplication` construction, before
  Context or QML existed: Cocoa platform integration → `PasteboardCreate` →
  `CFPasteboardCreate` → synchronous XPC wait.

Both samples and raw failures are retained under the run's scenario directories.
The stacks establish where the threads waited; they do not prove the underlying
cause in Qt, macOS or another clipboard participant. No clipboard content was
read for diagnostics, no clipboard data cleared and no system service restarted.
The following isolated three mask repeats passed, so startup/activation
instability remains unresolved rather than being labeled a mask correction fix.

## Comparison fixture previously accepted an intermediate render

`UX-PRV-001/repeat-3` in the same blocking run timed out on its first Processed
comparison. The old fixture accepted any preview different from the unedited
image immediately after a multi-event slider drag. That condition did not prove
that the captured frame represented the final edit document.

The fixture now computes reference pixels from the final document and waits for
the matching QML Image URL, Ready status and provider pixels before comparison.
It retains intermediate/final images when the earlier capture differs. Numerical
renderer validation is separate; this oracle checks UI/document coherence.
The original failure remains available. Original/Processed each passed three
strict repeats after this change (`pixel-clipboard-locale-report`), without a
production comparison change in this round.

## Graphics backends

Local native suites now use Qt's default graphics backend, matching production;
each result records the actual graphics API. On this reference Mac it is Metal.
Hosted CI continues to use offscreen/software. Earlier local functional reports
used software; they remain evidence for that backend, without implying Metal
coverage. PERF_LOCAL already used the default backend before this change.

## Test fixture corrections: keyboard focus and collection names

`keyboard-extra-report` initially assumed that a mouse click transfers keyboard
focus to every button/slider. macOS does not guarantee this. The corrected Tab
tests first reach their starting control through actual keyboard navigation;
focus order, ScrollView escape and popup focus restoration then passed three
times without production changes for those behaviors. Enter activation was a
separate reproducible product/specification gap, tracked as LUMA-UX-013.

`filter-keyboard-native-report` initially expected names differing only in case
to be rejected. The existing catalog schema uses a case-sensitive UNIQUE name
constraint; such names are distinct. The corrected test checks duplicate exact
names, empty/whitespace-only errors, deterministic repeated failures, distinct
case variants and successful retry. No collection-name policy change was made.

## Gradient fixture sent input before the post-Redo canvas was ready

The full Metal blocking run `20261016T135909Z-46869` retained two failures of
MSK-021, with unchanged gradient geometry after the second drag. Instrumentation
preserved the same input sequence and reproduced two failures in ten attempts
(`gradient-press-stress-report`, run `20261016T141532Z-53585`). Both failures
recorded local_canvas_ready=false and gesture_active=false immediately after
press; all eight successes recorded both flags true. Redo invalidates the canvas
until the current render completes, and beginLocalGesture deliberately refuses
input before that point. The fixture omitted its existing ready condition here.

The corrected fixture waits on localCanvasReady after Redo and verifies that the
handle press starts a gesture. No fixed delay or production mask change is used.
Raw original failures, input traces and screenshots remain retained.

## CI evidence paths ignored a custom build directory

The initial CLI used build-verify for default reports/artifacts even when invoked
with --build build, while both workflows upload build/ux-*. A mocked CLI reproducer
confirmed the mismatch without launching Qt. Defaults now follow the selected
build directory; explicit paths are preserved. The runner unit suite tests both
CLI variants so CI uploads the evidence it actually generated.
The lock file is scoped by a context manager and closed on return or exception;
the CLI test also checks that it does not retain screen ownership after returning.

### Native file-panel cancel input remains unqualified

`navigation-first-report`, `navigation-repro-report` and
`native-cancel-input-report` retain unsuccessful harness experiments for NAV-004.
The panel is an own-process NSOpenPanel with NSRemoteView as first responder;
synthetic NSEvent dispatch did not dismiss it. Its public accessibilityCancelButton
was unavailable. This is not evidence that a real Cancel click fails in Luma.
No direct cancel:/reject call is counted as native user input. NAV-004 remains
unadapted, and the experimental helper is not linked into the application or tests.
The native opening check KEY-001 still uses reject only for fixture cleanup.

Navigation harness corrections are separate from production fixes: QFile::copy
uses temporary names and could not prepare a 255-byte destination, so the fixture
now writes that public PNG directly. Unsupported-image tests recover by dropping
the valid original after verifying the visible error and preserved catalog; this
lets the harness's final successful-save check run without an expected error.
Repeated outside clicks may legitimately close/reopen a Dialog. The 200-click
scenario verifies balanced popup lifecycle and usable input after dismissal,
without assuming every eight-click burst leaves the popup open.


### Thumbnail unit-fixture completion and error observation

`navigation-presets-final-ctest.log` retains a failure of
`edited-thumbnail-snapshot-cache`: the old unit fixture timed out while polling
Image.Error after directly completing a controlled response from the GUI thread.
The initial cause is not proven. Fifteen diagnostic repeats passed; Qt 6.10.3
already has a finished-before-connect guard, so no lost-signal production defect
is asserted. An initial added Connections observer also failed to count the
transition; its handler now names the observed thumbnail explicitly.

The unit fixture now queues its controlled completion to the response's owning
image-reader thread, after request registration, and records the exact Error
transition and source URL. It first asserts the production retry interval is
1500 ms, then lengthens that interval only in the fixture because this test
already invokes the retry signal manually to check the source binding. This
separates that binding test from automatic retry timing. The production thumbnail
provider, PhotoCard timer and retry behavior are unchanged. Original failed runs
remain retained; repeat verification does not retroactively turn them into PASS.

The final serialized-completion fixture passed 30 consecutive targeted repeats
(`thumbnail-serialized-run.log`), followed by full CTest with 38 PASS and one
optional private Nikon SKIP (`navigation-presets-clean-ctest.log`, 91.02 s).
This is verification of the normalized fixture; it does not prove the cause of
the initial timeout.

## Additional stress, race and recovery fixture corrections

The original `expanded-first-report`, `expanded-stress-first-report`,
`stress-input-report`, `expanded-functional-final-report` and
`expanded-oracles-report` remain retained, including their failures.

- A click does not give macOS Slider keyboard focus. The editing stress fixture
  now reaches it with real Tab input under process-local Qt TabFocusAllControls;
  no OS preference is changed. QQuickSlider does not implement the assumed
  Home/End behavior, so the 1,000-gesture case uses actual endpoint pointer input.
  The 500-edit cases retain full documents after every edit and verify all
  200 retained Undo and 200 Redo states, including exact final preview pixels.
- PRV-008 now waits for each photoOpened-triggered asynchronous Library reload
  and compares the final asset against the UUID saved before activation. The
  original composite final assertion did not identify which check failed; its
  catalog already contained the expected Photo-0099. A three-repeat rerun passed
  with separate load, activation, UUID and pixel assertions.
- The import-cycle test's own read-only sqlite observer originally had no busy
  timeout. Three retained failures explicitly reported SQLITE_BUSY (code 5) at
  sqlite3_prepare. Only this observer now uses a 1,000 ms busy timeout; production
  locking and the five-second GUI watchdog are unchanged. IMP-019 then passed
  three native repeats, each with 50 cancel/reimport cycles and durable session,
  item, asset and cleanup oracles (`import-cycle-fixed-report`).
- REC-008 now proves the post-source-deletion Contrast render has a new URL and
  distinct expected pixels before Undo, then requires a new URL restoring the
  original pixels. Already-visible old pixels cannot satisfy either transition.
  REC-009 corrupts only a hash-guarded private cache after the prepare process
  exits and before the verification process constructs its image provider.
- Held Qt callback tests preserve actual FIFO delivery and verify their gate was
  caught. File-dialog selection remains an explicit fixture boundary; it is not
  counted as native chooser selection. Ordinary angleDelta wheel input is not
  trackpad or momentum coverage. Small public images are functional fixtures,
  not substitutes for 20 MP performance workloads.

## Scene readback and local performance

`current-perf-first-report` retained focus failures. Performance setup now raises
and activates the native window before timing, requiring active/exposed state
again after the measurement. No global window or OS preference is changed.
`current-perf-focus-report` still failed ADV-012 despite exact current document,
Ready URL and provider pixels. Retained same-size 926 × 1532 RGBA8 frames differ
in 92 pixels / 95 channels, with maximum absolute channel difference exactly 1.
This establishes the observed discrepancy; attributing it to Qt/Metal
quantization is an inference, not a reproduced application rendering defect.

Only scene readback comparisons allow an absolute difference of one RGBA8 level;
document and provider pixels remain exact. Before/after frames must differ beyond
that tolerance, preventing an old frame from passing. Geometry and non-null item
guards precede grabWindow. NAV-002, PRV-002, ADV-012 and PERF-006 passed on an idle
host in `checkpoint-perf-guard-final-report`. Measurements include declared
input/debounce/polish boundaries; they do not measure physical scanout. PERF-006
uses a native foreground-only local run; FLT-002 retains the separate CI functional
path. The Qt Quick Metal UI is not the planned 0.6 GPU image-processing backend.

## StandardKey.Quit differs between Cocoa and offscreen

Hosted PR run 38922231206 at commit 8a1efbb was cancelled at its 50-minute limit.
Build/unit tests passed, while several exit/restart cases waited for a shortcut
that was never delivered. Local `GITHUB_ACTIONS=true` reproductions retain the
same issue in `offscreen-quit-repro-report` and `offscreen-quit-binding-report`.
The latter records an active/exposed window, enabled ApplicationShortcut and
actual QKeySequence::keyBindings(Quit) containing Exit. Qt 6.10.3's offscreen
theme uses WindowsKeyboardScheme; hardcoded Ctrl+Q does not match that binding.

Driver::quitShortcut now sends the actual resolved StandardKey through QTest and
the unchanged production QML shortcut. It never calls root.close(). Cocoa
separately asserts Cmd+Q is declared and sends that exact combination. The raw
key-input helper is unchanged. Actions record the platform, resolved binding and
whether native Cmd+Q was exercised; no fake ApplicationState is injected.

The local offscreen/software simulation passed 45 results, including 150 actual
empty-app launches (`offscreen-quit-fixed-report`). A separate Cocoa/Metal suite
passed 54 results including the native Cmd+Q/restart paths and three production
fixes (`cocoa-quit-final-report`). Both passed provenance, source consistency,
original SHA and SQLite integrity. GITHUB_ACTIONS=true here is a local environment
simulation, not proof of a hosted CI pass. The CI deadline remains unchanged;
the next pushed commit still needs its own hosted result.

## Window / WB / export draft normalization

`window-wb-export-first-report` retains 9 native PASS and 5 FAIL results with
unchanged source/binary, original SHA and valid SQLite snapshots. Four failures
were fixture assumptions; the numeric negative-zero defect is LUMA-UX-020.

- APP-014 requested 899 px through QWindow::resize and received an actual 899 px
  window despite minimumWidth=1000. That programmatic request bypasses the native
  user-resize constraint; it does not prove a real macOS resize handle can do so.
  The invalid clamp assertion and its original screenshot remain retained. This
  ID is unregistered again until real native resize input or another valid
  acceptance boundary is available; supported-width-only checks are not a full
  pass for the specified 899/900/999/1000/1199/1200/1450 contract.
- Public nextItemInFocusChain does not predict every actual focus-scope/proxy
  route. WB now uses bounded real Tab input until the target has active focus,
  checking document invariance on every step, rather than requiring predicted
  intermediate object identities. No direct focus setter or OS preference change
  is used.
- The WB prepare process closed its window successfully, but the old fixture
  required Library.working=false. That aggregate intentionally includes the
  closing state. Shutdown now checks no live import, no busy/save and a closed
  window, matching the established restart contracts.
- EXP-009 attempted to decode an empty source string before an asynchronous
  Library metadata page was ready. Reference preparation now waits for the
  actual nonempty row source and loading=false. The same guard protects the
  immutable queue oracle. No production decoding/export behavior was changed.

The first native run already passed 50 fullscreen round trips at 1000 × 640 and
50 at 1200 × 760, preserving exact edits, selection and three nonzero scroll
positions. It also passed protected original/catalog/WAL/SHM aliases, live edits
after immutable export planning, actual SQLITE_FULL autosave/Retry and real
Library wheel input overlapping a running 1,000-item export queue. These are
focused results, not full-scope release qualification.

After the formatting fix, `window-wb-export-fixed-smoke-report` retained an
additional numeric fixture failure at the third inserted preset. The label had
already correctly shown positive zero for -0.001 and 0. An intermediate fixture
added wheel input and required the actual clicked signal, but all three attempts
missed that click (`window-wb-export-final-report`, 30 PASS / 3 FAIL). These raw
failures are retained; no production preset behavior was changed in response.

The fixture now waits for ListView.moving=false and a layout polish before
testing whether the row centre is inside the clipped viewport. It then requires
one actual clicked signal. `numeric-scroll-settled-report` passed three full
80-label runs: the third row was at y=84 inside a 101 px viewport, contentY=27,
moving=false. All -0.001/0/+0.001 document and durable values stayed exact.
This verifies the normalized input boundary; the earlier missed hit is not
attributed to one unproven internal Qt cause.

The other ten WB/recovery/export cases passed three times in
`window-wb-export-final-report`. The final corrected-source fullscreen cases
each passed 50 native round trips (`fullscreen-fixed-final-report`, two Metal
results, run `20261017T062228Z-7607`). These non-blocking REGRESSION cases also
passed their first 50-cycle runs before the label correction. Offscreen may not
qualify native fullscreen: the adapter explicitly requires Cocoa. A subsequent
serialized full build and CTest passed, with 38 PASS and one optional private
Nikon SKIP in 83.74 s (`window-wb-export-full-ctest.log`).

## Gesture fixture semantics and actual keyboard focus

`gesture-remaining-first-report` retains four PASS / four FAIL native results.
The focused-control clipping was reproduced separately three times and fixed as
LUMA-UX-021. Two other failures were fixture assumptions:

- After a named WB choice, the existing currentIndex binding can display Custom
  (index 6). Up from there selects Fluorescent, not As Shot. The corrected reverse
  input is actual Home; the pinned Qt ComboBox implementation activates item 0
  for that key. No direct currentIndex setter or WB production change was used.
- Preset JSON serializes only selected groups, so decoding an Optics-only preset
  restores neutral defaults for unselected groups. Its independent expected
  document now explicitly copies only typed lens state and its enabled bit into
  a default document. In-memory Copy correctly retains the full captured document;
  both operations are still required to change only Optics in their targets.

DTL-007 remains unadapted. The initial test held an actual Option/Radius Slider
gesture and moved to a preset, but hover preview did not arrive while the Slider
owned the exclusive pointer grab. This is an input-feasibility boundary, not
proof of diagnostic persistence in production. The failed input trace is retained;
no direct HoverEvent, preview setter or simulated response is counted as coverage.

The corrected seven new cases and twelve affected existing cases passed three
native repeats in `gesture-import-final-report` (57 Metal results). Full build
and CTest then passed: 38 PASS, one optional private Nikon SKIP, 84.59 seconds.

## Hosted import-Cancel race and physical Copy checkpoints

Hosted run 39008653483 at ca38254 completed build/unit tests, then reported
281 blocking PASS / one FAIL. IMP-004/repeat-3 observed progress=0.953 at its
90% checkpoint, but the import completed before Driver::click's next frame.
The Cancel button was therefore unavailable. Retained SQLite evidence has two
cancelled sessions and one completed session; catalog integrity passed and the
main-loop maximum stall was 96 ms. No production import failure is asserted.

The original 57,572,076-byte evidence ZIP was downloaded through the GitHub
connector and verified against its supplied SHA-256:
`ecaf4c32843a6b7e858272a2d4610910f9ec4988aa466e644bab62fe841bf37f`.
Original reports, actions, screenshots and failed-case SQLite remain locally
retained under `ci-ca38254-evidence`.

The process-local test VFS now holds the real writer after the exact owned Copy
item 99/499/899 has been published. The actual Cancel or Quit input runs while
that writer is held; release then permits normal atomic completion/cancellation.
The final asset delta must be exactly the known boundary or one item below it,
with real copied files and no leaked temporary output. Default position 0 keeps
the established single-copy fault/recovery scenarios intact. This is functional
checkpoint coverage; pausing a worker does not measure production cancel latency.

An initial local observer incorrectly also required delivered GUI progress to
equal the physical checkpoint. LibraryModel throttles updates to 100 ms in the
worker; a held worker need not emit that next update. Its first timeout and a
partly run second attempt remain retained in the interrupted
`import-cancel-offscreen-fixed` run. The author stopped that known-invalid suite;
the runner cleaned up its own child. It is not a completed qualification.

Only that invalid GUI-progress condition was removed. The physical checkpoint,
real UI action, strict retained rows, integrity and cleanup remain mandatory;
observed GUI progress is logged separately. A local GITHUB_ACTIONS=true
offscreen/software simulation passed IMP-004/006 and APP-008 three times (nine
results, `import-cancel-physical-offscreen-report`). The subsequent native
57-result suite also passed those cases and the default-position import fault,
50-cycle and process-death recovery regressions. A new hosted commit run is
still required; the local environment flag is not a hosted CI result.


## Long-filename visual oracle and sidebar focus transition

The first ten visual/recovery scenarios passed in `visual-remaining-first-report`
(run `20261017T124640Z-19791`). Manual screenshot review nevertheless found the
left sidebar overflowing beneath the filmstrip for a 255-character filename.
The initial oracle checked the toolbar/card/source label, but not preset action
reachability. Its recorded PASS remains intact; a strengthened three-failure
reproducer establishes the production defect LUMA-UX-022.

The first layout correction passed pointer activation but its new keyboard check
failed (`sidebar-overflow-fixed-smoke-report`). Closing Copy restores its focus;
manual scrolling deliberately does not reveal an unchanged focus item. The fixture
now leaves through actual Tab and traverses back, without direct focus setters.
That fresh transition also failed three times in `sidebar-focus-transition-report`:
the first implementation deferred the same helper twice with different pane
arguments. The final implementation defers one callback which checks both panes.
The acceptance waits for the actual clipped bounds before Space activation.
No source-label truncation or manual contentY assignment was added to the fixture.

VIS-005 imports 100 malformed owned images. Production reports contain a source
line, an error and blank separators, so this is a declared larger-than-100-line
readability workload, not a claim that 100 failed items render as exactly 100 lines.
The actual complete text, line 100 and both endpoints are checked through real
wheel input. VIS-004 independently exercises exactly 100 export-failure lines.
Neither case is an alias for the global rendered-text overlap audit VIS-008.


## Auxiliary-window polish acknowledgment in hosted CI

Head `3e4fc19` hosted run `39033565439` completed with 281 blocking PASS and
one DSC-001 timeout; build and all available unit/integration tests passed.
The original artifact ZIP is retained with SHA-256
`8ce08c1aa7f0bdd992886ddb2a05612dcea5119cc5b68d042c54909e8dde4774`.
The failed process had a live heartbeat, no hard hang, four main-window frame
intervals, valid SQLite/original integrity and no QML warnings.

DSC-001 creates a separate real ScrollView positive-control window. Its Driver
requested an update of that window but waited for Metrics.polishes, attached
only to the main window. Unrelated main-window updates could acknowledge it;
otherwise the five-second wait expired. Driver.frame now observes the actual
window's afterAnimating through a scoped QObject connection established before
update. The timeout, hidden-window handling and production code are unchanged.
This remains a GUI animation/polish acknowledgment, not physical scanout proof.
The corrected scenario passed four final-source Cocoa attempts and ten local
CI-environment offscreen repeats (`visual-driver-final-report`,
`sidebar-driver-native-final-report`, `driver-frame-offscreen-final-report`).
Provenance, source consistency, original SHA and SQLite passed. This local
simulation is not a hosted run; the original hosted CI FAIL remains retained.

The intermediate left ScrollView also needed an explicit scrollbar gutter:
`sidebar-gutter-repro-report` retains three native failures showing actual
Flickable/source-label bounds overlapping the full vertical scrollbar hit area.
Only the new left pane reserves effectiveScrollBarWidth; a fading thumb cannot
hide the geometry assertion. Right-pane behavior is not changed by this finding.


## Import/scroll fixture required a fully visible card unnecessarily

The first Library block (`library-remaining-first-report`, run
`20261017T132538Z-32470`) passed five cases but stopped LIB-017 before Cancel:
its anchor helper required an entire card inside the viewport. Import progress
reduces the viewport height, and a legal settled offset can straddle both edges
without one whole card fitting. The screenshot retains substantially visible
cards and a working grid, not a production blank state.

The helper now requires a real asset at least half visible with its center inside
that viewport, logging the actual bounds and identity. The independent post-reload
identity/distance assertion is preserved. Three corrected runs then reproduced
the actual reload jump, tracked separately as LUMA-UX-023. The stronger final
fixture additionally verifies insertion on both sides of the anchor, using its
new independent Catalog rank rather than its previous numeric row.


## Held scrollbar coverage for the unpublished viewport correction

Independent review found a missing edge in the first LUMA-UX-023 correction:
a reload could schedule restoration after a drag/scrollbar press had already
started. The earlier movement-start handler would not fire again for that input.
A supplemental native probe held the first eligible Library queued delivery
after final progress and eight durable insertions, pressed the actual scrollbar thumb,
and observed reloadStarted with both native Qt mouse buttons and ScrollBar.pressed.
`library-held-scrollbar-repro-report` retains its failed restore-target assertion.
This tests the actual import/UI path, without a direct model refresh or fake input
state. A cacheBuffer page may deliver at that queued boundary; the separate
pressed-at-reload observation proves the actual import/input overlap either way.
Capture now ignores a current drag/held scrollbar; a post-release real
scroll is the positive control. The initial two 2,048-source Z/A insertion waves
remain separate proofs that ordinary completed input preserves asset identity.


## Hosted functional success without a build-attestation file

Hosted `56e4e28` run `39036376277` completed successfully: 282 blocking attempts
passed, as did build, unit/integration, format and clang-tidy. The retained ZIP
SHA-256 is `eeea223928875e1698cc635d12a666b0397aaa264e8ddfa2feb3542c2a20b5c3`.
Source consistency and original hashes passed; all 279 native-case catalog
snapshots passed (the three suite-wide integrity results have no catalog).

Its report nevertheless marks build provenance UNVERIFIED: ordinary cmake build
never created `build/ux-build-provenance.json`. These functional PASS results
remain intact, but are not a formally attested release qualification. Local
current-scope evidence uses build_ux.py and retains matching source/binary proof.
The workflows now call the existing build wrapper immediately before UX and
require matching provenance through --require-build-provenance. Release
qualification implies the same strict preflight. Missing or stale proof is
rejected before fixtures/child processes; setup and suite drift are also checked.
The evidence upload includes the build record. All 25 runner unit tests passed
in 5.468 seconds, including 22 mocked CLI matrix cases using the real JSON
validator. A real stale-build preflight also returned exit 2 before execution
(ci-provenance-preflight.log). These tests validate runner accounting, not native
UX or hosted CI. The prior hosted functional results are unchanged; a new hosted
attested run is still required.
