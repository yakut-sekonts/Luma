# Native UX regression

Official `UX-P06-GPU-002` imports one neutral 256×192 PNG and uses actual Settings
clicks to compare CPU and GPU preview buffers and completed Image-node readbacks.
A UX-only `MetalPreviewProbe` forwards to the exact controller-created native
accelerator, counts genuine completions and compares every returned linear sample
with the CPU pipeline at the existing `2e-5 + 2e-5 * abs(reference)` tolerance.
CPU fallback, missing buffers, nonfinite samples or extra GPU calls while disabled
fail. Final CPU samples are exact; GPU/scene RGB8 allows one existing quantization
step and alpha remains exact. Saved document/revision/identity, selection/history
and original hashes remain unchanged. The observer never changes pixels or GPU
eligibility. Shipping isolation includes local as well as global observer symbols.
This bounded neutral check does not qualify arbitrary RAW/edits, GPU failures,
display transitions or performance; observation adds CPU work in the worker.

Official `UX-P06-LRU-002` checks the actual thumbnail provider's 512 MiB disk cap
with two owned photos and two 256 MiB sparse cache entries (less than 1 MiB physical
allocation each). Cold insertion and the first warm hit on a recreated real
provider must evict the oldest entry, retain the newer/current JPEGs and preserve
a non-JPEG marker. The second photo has never appeared in QML: a real successful
worker/texture response while its original is hidden rules out Qt pixmap reuse
and regeneration. Actual filters and scene pixels, saved revisions/documents,
original hashes and JPEG bytes are checked. Sparse entries are cleaned on failure
as well as success. This is bounded disk accounting; it does not qualify RSS/VRAM,
whole-app restart, multi-process cache concurrency or a physical 512 MiB workload.

Official `UX-P09-RLK-001/002/003/004/006/007` use three owned PNGs with Unicode
paths and a two-member collection. Actual UI saves Exposure and descriptive
metadata, moves one original or its parent tree, checks Missing badges, then
drives Locate Original/Folder and reopens the new path. File/folder chooser URLs
are explicit fixture boundaries; native picker navigation is not qualified.
The folder fixture sets `currentFolder` before opening the actual dialog because
Cocoa does not select a different directory through an already open panel's
`selectedFolder` setter. Returned local URLs are logged and must resolve to the
exact owned canonical target before the production accepted handler runs.
A same-name/same-size/different-hash candidate must fail without any mutation,
then a valid candidate must succeed. Exact document/revision/identity, saved
metadata, collection, selection, history and source hashes are preserved. Preview
pixels after reopening are checked against an independent CPU reference. The
missing-only case restores the folder and moves a nested file again to verify
watch rearming. Three repeats run in shared CI; RLK001/002/003/006 are blocking.
These bounded checks do not cover network volumes, eject/remount, watcher-limit
saturation, process kill during relink or large-catalog latency.

P06-MEM-001/002 exercise warning/critical and normal recovery through the real
EditorController GUI handler. The UX-only friend probe stops and drains OS
notifications on an empty owned controller so host events cannot interleave with
the prescribed sequence. Actual UI zoom, full/Fit CPU pixels, three reclaimable
cache budgets, document/history/catalog and original hashes are checked. This
is controlled application recovery, not OS event delivery, RSS or stress
qualification. The same scenarios run on offscreen/software and Cocoa/Metal.

This separate Qt Test executable loads production Main.qml and the real controllers.
It sends real clicks, drags, keys and ordinary wheel events. A native file-panel
selection may be prepared as a fixture boundary; this does not qualify the native
file chooser itself. It never opens private photographs or the user's real catalog.

Current schedule (user direction, 2026-10-19): until the pre-release campaign
for 1.0.0, run functional/image-quality/UX/data-integrity checks only. Do not run
stress, soak, chaos, performance or large-catalog/repeated-load workflows.
Existing load adapters and historical results are retained for 1.0.0; deferral
never means PASS. Use `--quality-only` for the reviewed functional policy. It keeps load deferrals
visible as SKIPPED and rejects new, unreviewed adapters. Fix and recheck failures
before publishing an intermediate release.

Build with the pinned toolchain:

```sh
.tools/venv/bin/python tests/ux/build_ux.py --build build-verify
.tools/venv/bin/cmake --build build-verify --target luma_ux_metric_tests --parallel 3
.tools/venv/bin/ctest --test-dir build-verify --output-on-failure
.tools/venv/bin/python tests/ux/run_ux_suite.py --quality-only --case UX-MAC-007 --case UX-SEL-002 --runs 3 --require-build-provenance
```

All native UI runs share an exclusive screen/focus lock. Do not interact with the
test window or run other native UI suites concurrently. Use a focused selection
while investigating a reproducer:

```sh
.tools/venv/bin/python tests/ux/run_ux_suite.py --quality-only --case UX-MAC-008 --runs 3
```

The partial selected-photo keyboard regressions (Return and keypad Enter) share
one generated PNG and use separate fresh processes in their own report domain.
Both run automatically in both workflows; their results never qualify
KEY015 or alter the 770-ID specification inventory. Its wrapper defines the bounded
workload directly, so the official-inventory `--quality-only` flag is unnecessary.

```sh
.tools/venv/bin/python tests/ux/run_keyboard_regression.py --build build-verify --runs 3 --require-build-provenance
```

The separate `run_keyboard_workflow_regression.py` uses one generated PNG at
1000×640. Actual Tab/Space/Return/arrows and focused text input drive Import,
selection, Develop, a 0.01 EV edit and PNG export. The native chooser's URL is the
only setup input seam. No pointer input, focus/scroll setter or controller action
is allowed in the workflow. This remains supplemental, not full native KEY015.
The PNG samples must match the complete literal edit through the CPU reference
with sRGB output, independently of the display profile. Profile declaration is
checked separately from sample bytes. Both workflows run three repeats and retain
the focus trace, pointer audit and actual/expected PNGs.

```sh
.tools/venv/bin/python tests/ux/run_keyboard_workflow_regression.py --build build-verify --runs 3 --require-build-provenance
```

Crop-related gradient handle regressions also use a separate one-PNG supplemental
domain. Normal and thin-photo fixtures exercise each Linear/Radial handle across
four distinct H/V flip states, with exact document/history/pixel/persistence checks.
The thin fixture also rejects ordinary brush, range and new-gradient input in
expanded photo padding. These bounded checks run in CI but do not register or
qualify the full MSK023 scenario; other geometry and projective edge cases remain
separate qualification work.

```sh
.tools/venv/bin/python tests/ux/run_mask_handle_regression.py --build build-verify --runs 3 --require-build-provenance
```

The transparent-Transform Radial handle regression has a different generated
one-PNG fixture and its own supplemental wrapper. It exercises the visible radius
above an uncovered output region after real center motion, Constrain Off and
Scale50, then verifies genuine press/move/release, independent relative geometry,
complete document/history/persistence, current pixels and original bytes. It is
native-only: the unchanged scene control requires an active/exposed Cocoa/Metal
window and actual composited gold handle pixels. It does not qualify the full
MSK023 scenario or hardware pointer/physical scanout behavior.

Run this on a logged-in local macOS desktop with actual Cocoa/Metal and matching
build provenance. Existing screen/focus locking applies:

```sh
.tools/venv/bin/python tests/ux/run_mask_transparent_regression.py --build build-verify --runs 3 --require-build-provenance --report build-verify/ux-mask-transparent-report
```

Hosted offscreen/software workflows run the fixture/accounting unit test through
CTest; they do not execute or report PASS for this native-only scene scenario.
An incompatible backend is still rejected by the strict C++ runtime guard; this
supplemental wrapper does not fake backend observations or add official SKIP/PASS
rows. Its RGB grayscale fixture remains separate from the two Crop/thin cases.

The legacy full-matrix --release-qualification command includes load workloads;
reserve it for the pre-1.0.0 campaign. Its strict full-matrix status is separate
from the requested intermediate functional-quality checkpoint.

Full-matrix qualification repeats blocking scenarios three times and runs other scenarios
once by default. An explicit --runs value applies to every selected scenario.

The complete 770-ID specification inventory is separate from executable adapters.
An implemented product capability without a verified adapter is explicitly SKIPPED;
it cannot satisfy a blocking gate. Absent future/conditional capabilities are
NOT_IMPLEMENTED. Selecting cases, a single phase, or only blocking mode never qualifies a full release.
The default scope follows the CMake source version; qualification cannot exclude it. An incomplete full
run returns nonzero, even if every executed test passes.

Evidence includes result/metrics JSON, UI actions, app log, screenshots while a
window exists, and an isolated SQLite snapshot on failure. Original SHA-256 values
are checked before the suite, after each executed scenario and at suite end.
Source commit, dirty state, a suite-end source snapshot and test-binary SHA are
recorded. Keep the tested sources unchanged when collecting release evidence. The build wrapper
serializes against UI runs and records source/binary hashes after a successful build.
Full release qualification requires this matching record and unchanged source/binary
hashes across the suite. A focused run may still report useful case results while
explicitly recording missing build provenance.

The parent samples a UI heartbeat and kills only its own process group on a >5 s
hard hang. A delayed heartbeat also captures a native stack sample when available.
Restart tests launch distinct processes over shared test state. Persistence fault
tests use a test-only SQLite VFS to return SQLITE_FULL or pause an actual WAL write;
intentional SIGKILL checkpoints are explicitly whitelisted and recorded.

Performance is MEASURE_ONLY on a recorded local Mac. Dispatch timing is not visual
acknowledgment. Frame intervals currently include idle time and must not be used as
an interaction FPS baseline. Trackpad/momentum, soak, the general Chaos Monkey
and tests of packaged app bundles still need executable adapters. Unsupported
--app and unsupported chaos selections are rejected, never silently ignored.
Reports keep this work separate from missing product capabilities.

Deferred until the pre-1.0.0 campaign: scoped history chaos supports only UX-UND-017: Contrast, Shadows and Vibrance Sliders,
discrete Left/Right inputs and Undo/Redo, with an independent full-document hash
and history model. Other CHAOS IDs, pointer gestures and randomized worker delays
are not covered. The default remains exactly 10,000 semantic actions; setup,
Tab traversal and observation inputs do not consume that count.

```sh
.tools/venv/bin/python tests/ux/run_ux_suite.py --suite chaos --case UX-UND-017 --actions 10000 --seed 123456 --require-build-provenance
.tools/venv/bin/python tests/ux/run_ux_suite.py --replay /path/to/saved/replay.jsonl --require-build-provenance
```

The complete seeded plan and execution intent/results are fsynced. Replay is
regenerated byte-for-byte before creating Context or sending UI input. Seed,
count, schema/model/generator, app/Qt version and generated fixture hashes must
match; conflicting flags and corrupt records abort. A shorter explicit count is
smoke evidence, not the full 10,000-action baseline. Full release qualification
refuses that reduction.

The macOS and release workflows run reviewed functional blocking adapters three
times with `--quality-only`. A versioned policy records reviewed bounded adapters
and source-based load deferrals; a new adapter requires explicit policy review.
Quality fixtures use a separate small directory and do not create the 24 MP image.
Deferred tests retain their original inventory mode and do not count as PASS.
This hosted functional gate is not full release qualification. The original
100,000-row Library model contract is opt-in through
`-DLUMA_ENABLE_SCALE_CONTRACT_TESTS=ON` for the pre-1.0.0 campaign; the default
256-row fixture retains ordinary selection, filtering and metadata contracts.

Updater tests use the public native SDK installation-callback boundary with a
non-installing probe, real controller readiness and real QML operations. They do
not download/install Sparkle packages. The Cmd+O scenario checks chooser opening
in Library and Develop; direct reject is fixture cleanup, not native cancel-input
or file-selection qualification. Copy crash recovery uses an intentional process
kill after publishing a complete file but before its catalog commit, followed by
the real LibraryModel startup recovery in a distinct process.


Auto Transform completion has a bounded official `UX-RACE-005` regression. Two
actual successful solver results are held on the worker before their original
queue/delivery; real Cancel and filmstrip photo switch must reject those exact
operations without changing complete documents, history, Ready/provider pixels,
natural durable revisions or source bytes. Its observed load-revision change does
not independently exercise the later load guard because the first Auto-revision
guard rejects both stale callbacks. The test-only probe is compiled exclusively
into `luma_ux_tests`; the ordinary application has no gate or probe ABI.

Run with `--quality-only --case UX-RACE-005`. This uses one generated PNG plus
small Building/Lens DNG fixtures, imports two assets and starts Auto twice. There
are no TRN014 stress cycles. The REGRESSION case needs its own focused CI/release
step because the blocking subset does not select it. Full current-scope release
qualification remains separate from this result.


Supplemental PhotoCard captions use the bounded wrapper
`run_card_flag_regression.py` and retain their own ID and NOT_QUALIFIED release
status. The card test imports two small PNGs and changes all
eight languages through real Settings controls, with independent caption literals,
complete data/history and current Image-node/cache pixel checks. It does not
qualify every localization/visual scenario.

Official `UX-LIB-004` imports exactly32 opaque RGB PNGs, the minimal reviewed
overflow for the actual GridView geometry (strict precondition). It
holds one successfully computed real response before its original completion
queue while actual scrolling reveals a previously unrequested response. Current
OLD Loading is required after the real scrollbar press and before movement.
Actual serial/revision/events, NEW Ready pixels, real cancellation/finished/
destruction, final viewport and complete pinned data establish acceptance; a
cancelled OLD response need not deliver GUI pixels. No source/status setter or
fabricated request/finished signal can satisfy it. This boundary is pending
completion, not active disk I/O. The historical supplemental wrapper
`run_thumbnail_response_regression.py` remains available with its original ID;
it is not another official adapter.

Official `UX-RACE-002` uses exactly two small opaque RGB sources. A real successful
OLD response is held while one actual Exposure gesture naturally saves a new
revision and emits its targeted invalidation. The NEW request must read that
revision, finish and display independently expected pixels while OLD remains
held. Explicit OLD release/finished/destruction must preserve the NEW image,
the unchanged control asset, complete documents, revisions and source bytes.
One separate Undo/Redo branch validates history. The declared FileDialog selection
and setup activation do not qualify native picker selection or platform focus
return. The driver now reports a missing text-input focus item instead of sending
an event to null; original setup failures remain archived.

Run both through the ordinary specification runner:

```sh
.tools/venv/bin/python tests/ux/run_ux_suite.py --build build-verify --quality-only --case UX-LIB-004 --case UX-RACE-002 --runs 3 --require-build-provenance
```

Each eligible official case receives a fresh `case-fixtures/<caseID>` directory.
Their32+2 originals form a strict SHA union with the unchanged generic fixture
profile of other selected cases; identical bytes at different owned paths remain
separate originals. Solo selections create no generic DNG/24MP/large-catalog
fixtures. A narrow manifest records actual directories and counts; mutation,
missing files, symlinks, aliases and duplicate paths fail integrity checks.
Explicit profile-only selections that become ineligible create no historical
large profile; their existing incomplete/nonzero outcome remains visible.
Both workflows run the REGRESSION cases in separate focused quality-only steps.

The real-response trace/gate is private to `luma_ux_tests` through
`LUMA_UX_THUMBNAIL_TRACE`; normal application targets have no probe or altered ABI.
An uninstalled Session retains the ordinary UX-target provider path. The finite
CV gate is outside catalog/cache/provider locks, leaves the other worker and GUI
alive, and has explicit/RAII release. Qt cancelled-response lifetime expectations
were reviewed against the pinned6.10.3 implementation. Artifact document helpers
include canonical `local_masks_serialized` alongside preset settings; preset
encoding alone is not a complete mask document. Original incomplete archives are
retained with their limitations.


Caption reveal uses one actual scrollbar drag, not a synthetic wheel prerequisite.
It requires press acquisition/release, positive directional movement, legal settled
offset and the same strict caption visibility/bounds/data checks. The response
candidate independently predicts the physical provider size from declared384×384
sourceSize and effective window DPR before arming; actual OLD/NEW tuples must match
exactly. Logical sourceSize remains separately asserted. DPR1 and Retina DPR2 do
not use the same physical request size.

The mask supplement also retains a passive bounded controller/window readiness
timeline before Context cleanup. No scheduling gate, render, delay or acceptance
predicate changes with it. Dropped/failed sample counters are explicit; observed
successful reruns do not establish individual causes of earlier native failures.
The user confirmed external interaction during that mixed run; preserve it
separately from subsequent controlled acceptance. See UX_QUALIFICATION_STATUS.md.

Official `UX-EXP-028` covers a missing watermark at execution. Its dedicated
profile contains three64x48 RGB photographs and one32x32 red original watermark.
The four original paths remain in the shared SHA guard; only three are imported.
After actual A output is atomically committed, the existing completion gate holds
its queued continuation before B/C. The test independently checks A pixels, then
removes exactly one verified, unaliased private execution-input copy. It does not
delete the original watermark or mutate controller settings, worker options or
queue state. B and C must each report the exact missing-watermark error, C must
actually start after B fails, and only the unchanged A output may remain.
Full neutral documents/revisions, metadata, source SHA and all catalog/session/
import/membership facts must survive. This is not per-job snapshot-path tracing.

```sh
.tools/venv/bin/python tests/ux/run_ux_suite.py --build build-verify --quality-only --case UX-EXP-028 --runs 3 --require-build-provenance
```

The official launcher creates a fresh canonical0700 temporary sibling for this
exact case/repeat and passes its environment copy to Popen before Qt starts.
The parent environment and other case launches are unchanged. Actual case ID,
wrapper/child PID claims, nonce, UID/mode and file hashes are checked and embedded
in `exp028-evidence.json`; CI uploads that narrow diagnostic rather than private
temporary-file contents. SoloEXP generates four RGB originals and no generic
profile. Combined LIB/RACE/EXP profiles have38 paths; generic10/20 retain their
own two small DNGs, producing50/60 originals. All78 infrastructure tests pass.
The historical supplemental EXP wrapper retains its own ID and domain.

RACE002 now records actual setup Import/session rows, explicitly nested expected
session, the historical expected expression and individual clause outcomes before
the strict baseline assertion. Hosted a94eac6 reproduces the former expression
as a flat array, while the actual session and explicit expected row are nested.
Its archive alone does not expose the compiler version. The later completed job
logs confirm AppleClang17.0.0.17000013/Xcode16.4 for this run; the exact compiler/Qt
overload-selection cause is not inferred from the JSON shape. All three new
hosted RACE002 runs pass. Original setup FAILs
remain retained; their missing immediate values are not retroactively invented.

`AppDeactivationCandidate.cpp` is supplemental only. The UX-only
`LUMA_UX_HISTORY_READBACK` provides a plain C++ const observation, never a QML
property/invokable or a shipping-target getter. Genuine existing-Finder handoff
has proved native/Qt inactivity and completed control/history before any test
release. Programmatic return and external Cmd+Tab did not prove own activation;
CUA rejected the unbundled process name. Full return/new-gesture/history acceptance
and official APP017/SLD009 qualification remain open. Its45s opt-in coordination
deadline is a fixture guard, not a product latency threshold. The original10s
return path and all original failures are retained.

`UX-LIB-005` is now official with its own fresh32-RGB profile. It performs real
Library scroll-away, one naturally saved Exposure edit and scroll-back while an
actual successful OLD thumbnail result remains held before its normal finished
delivery. The returned current NEW Image must be Ready with the correct pixels
before OLD is released. Actual OLD cancellation is allowed and recorded; no
uncancelled branch, active CPU rendering overlap or per-consumer ownership is
claimed. Full document/history/revision/metadata/journal/source checks remain
required. The historical supplemental ID is retained separately.

Combined LIB004/LIB005/RACE002/EXP028 profiles now contain70 distinct paths;
generic10/20 plus their two DNGs produce82/92-path SHA unions. The102 combined
Python infrastructure checks pass. Run the bounded official case with:

```sh
.tools/venv/bin/python tests/ux/run_ux_suite.py --build build-verify --quality-only --case UX-LIB-005 --runs 3 --require-build-provenance
```

On macOS the UX executable is a build-only `luma_ux_tests.app` with the unique
`app.luma.ux-tests` identity and `LumaUXTests` display name. The existing CLI path
is an absolute symlink to its canonical executable; the launcher executes that
canonical path. Provenance includes exact generated plist identity/SHA and binary
SHA and fails closed for changed or aliased bundle metadata. This is not a
signed, relocatable or distributed application. For one-time migration of an
existing flat UX build, configure first while its old attestation still exists;
`build_ux.py` removes stale attestation before building. CMake refuses to replace
an unverified old file or a different symlink.

The bundle identifies the live native test window correctly, but CUA Raise/title
input still did not establish own native activation. The user deferred manual
Cmd+Tab on2026-10-19. APP017/SLD009 remain unqualified; no official adapter or
PASS is assigned from the successful deactivation half alone.

The three approved contract clarifications are recorded with original wording
in [UX_SCOPE_CLARIFICATIONS.md](../../docs/testing/UX_SCOPE_CLARIFICATIONS.md).
Changing those descriptions alone does not qualify APP014, CRP010 or VIS003.

CRP010 and VIS003 now have runtime-qualified official adapters for their approved
contracts. APP014 still needs actual native resize/clamp input and remains a gap.

```sh
.tools/venv/bin/python tests/ux/run_ux_suite.py --build build-verify --quality-only --case UX-CRP-010 --runs 3 --require-build-provenance
.tools/venv/bin/python tests/ux/run_ux_suite.py --build build-verify --quality-only --case UX-VIS-003 --runs 3 --require-build-provenance
```

CRP010 owns one256x192 stripe PNG. It observes real drag/Done, positive active
Canvas overlay and independent composited-scene source/letterbox pixels after
Done, followed by ordinary input, empty re-entry/Done and one natural Undo/Redo.
No Cancel action is invented. The final read-only audit recomputes35 scene PNGs
across seven actual native children,28 after the first Done including Undo.
Independently predicted flat pixels have zero mismatches; predetermined border/
interpolation bands are excluded and not qualified.

VIS003 owns four64x48 RGB sources with short, ASCII230-byte, Cyrillic230-byte and
231-byte stems. Real active exports retain full logical basename while middle
elision is allowed. Allocated item geometry and virtual painted bounds are checked
separately, with exact stable controls and paint containment. The first draft's
painted-bounds-as-layout failure is retained; its measurement correction changes
no product UI or tolerances. Every child verifies three literal sRGB PNG outputs
and a pre-execution231-byte rejection without changing sources, complete typed
documents, catalog identities, persisted settings or existing destination files.
This is a PNG boundary representative, not every format or an OCR/scanout oracle.

MAC001 and CRP010 require actual Cocoa/Metal (MAC001 also requires native DPR2).
When the existing GITHUB_ACTIONS route requests offscreen/software, the runner
classifies these as unsupported_backend before fixture generation/child launch.
It records required/requested backend and explicitly unobserved actual runtime;
no PID or actual backend is fabricated. Unsupported-only --runs3 emits one policy
SKIP, not three invented child results. Mixed supported rows still run, but raw
reports keep INCOMPLETE, exit1 and NOT_QUALIFIED. Unforced native runs continue to
validate the actual backend rather than infer it from the machine model.

All six dedicated profiles contain75 native original paths. Mixed SEL002/IMP009/
global unions are87/97/75. Software omits the Crop source, yielding74 profile paths
and86 originals with SEL002. Fixture accounting/policy tests verify unsupported
requests cannot fall back to generic/Large generation. The combined infrastructure
suite has122 PASS. The final focused cohort has39 PASS and3 SKIPPED in42 rows,
with38 actual children, one global result and three no-child policy SKIPs. It is
not a full adapter sweep or release qualification. Both workflows run VIS003
three times and upload its JSON, current-label and active-export artifacts.


### Partial thumbnail render across a batch commit

`UX-RACE-007` is an official bounded functional adapter. Its owned fixture has
three literal 256x192 PNGs. The OLD thumbnail is held after a genuine converted
RGBA row while a real UI batch operation commits; current NEW Ready/pixels must
precede OLD release. The runner verifies original hashes, SQL integrity and
attested build provenance. The complete emitted JSON and row/node PNG artifacts
are needed to audit the result. This is not a simultaneous CPU-instruction claim.

```sh
python3 tests/ux/build_ux.py --build build-verify
python3 tests/ux/run_ux_suite.py --build build-verify --quality-only --case UX-RACE-007 --runs 3 --require-build-provenance
```

Native runs require Cocoa/Metal and an unlocked session. Local software routing
is supplemental and does not imply hosted-CI success. Both GitHub workflows
include three RACE007 repeats. The row observer is test-target-only; CTest checks
CPU equivalence and shipping binary symbol isolation separately.

LIB004/LIB005/RACE002/VIS003/RACE007 share `readyItemGrab`, which acknowledges the
actual Qt ready signal and keeps a failed pending result alive through teardown.
This changes the capture harness, not the product render behavior. The retained
original failures and exact completed cohorts are recorded in
`docs/testing/UX_QUALIFICATION_STATUS.md`; a focused PASS is not release approval.

### Minimize during a real export queue — unqualified candidate

`CANDIDATE-APP016-MINIMIZED-EXPORT` uses two literal 64x48 PNGs and the real
production import/export controls. Only the file chooser result is a fixture
boundary. The first real queued completion is held after its output is installed;
the second file must be exported while the actual owned NSWindow is miniaturized.
AppKit notifications, Qt state and the stable native window identity are observed
independently. The adapter uses public `performMiniaturize`/`deminiaturize` actions,
not a simulated visibility flag; it does not claim physical pointer or Dock input.

After restore, it checks literal output pixels, source hashes, all logical
catalog tables, export settings bytes, selection and a real Redo/Undo control.
No performance thresholds or repeated load workload are involved.

```sh
python3 tests/ux/build_ux.py --build build-verify
python3 tests/ux/run_minimize_export_regression.py --build build-verify --runs 1 --timeout 120 --require-build-provenance
```

Run only on an unlocked Cocoa/Metal session. It is not registered as official
APP016, included in the default suite, or counted as PASS. The original
standard-button `performClick` experiment timed out waiting for minimization;
the following public-action build could not activate its initial window on a
locked Mac. After unlock, active-window runs and independent AppKit-only probes
also timed out during minimization. The QEventLoop experiment did not help and
was removed. These failures are retained; they do not establish a Luma defect.
`app016-minimize-evidence.json` records
actual checkpoints reached, and `app016-restored.png` is produced only after a
successful restore. Focus notifications and frontmost process identity are
observed without recovery activation. A completed native repeat is still required.

### Localized preset dialogs — bounded visual and keyboard slice

`run_localized_dialog_regression.py` uses one generated PNG and one preset made
through actual UI. At 1000×640 it visits Settings and preset Create/Edit/Copy in
all eight languages. Its 256 explicit text bindings compare the actual paint
node with the language resource, check wrapping/elision and ancestor clipping,
and reject body content overlapped by the separately painted footer. This is
resource/render consistency, not human linguistic or glyph-shape qualification.
Real sidebar/dialog wheel input exposes fields; actual Tab/Space round trips
exercise Geometry and the replace-current-settings option without focus setters.
Cancel preserves the preset file, full current/durable document, revision,
source bytes, metadata, selection, history flags and preview pixels.
The fixture makes a real Exposure edit before the dialog tour. After all eight
languages, actual Undo must recover the complete neutral document and Redo must
recover the exact edited document and pixels; both are naturally persisted.

```sh
python3 tests/ux/run_localized_dialog_regression.py --build build-verify --runs 3 --timeout 240 --require-build-provenance
```

Both workflows retain the binding ledger, input trace and 32 screenshots per
repeat. This supplemental domain does not register VIS007/VIS008 or qualify
every product surface, native chooser, translation meaning or physical scanout.


Official `UX-P09-META-001` displays all six capture-data fields from one dedicated
128×96 DNG with an EXIF IFD, then saves/reopens all nine supported descriptive
fields. A real queued metadata completion is held while Escape, workspace
navigation and Library selection run; the original result must deliver without
reopening the dismissed dialog. This is a completion-boundary test, not a slow
filesystem or large-catalog benchmark. The generic Lens/Building fixtures keep
their previous bytes; only this case enables the DNG generator's EXIF IFD option.

Official `UX-P09-META-006` selects three of four photos and uses the actual Rating,
Flag and Label controls. For each operation the test VFS holds the first successful
catalog write, independently reads the old snapshot, and checks that PhotoCards
do not expose uncommitted values. `SQLITE_FULL` on a subsequent write must preserve
all metadata/revisions and show an error. A retry commits all selected rows exactly
once and preserves the unselected control, original hashes, Develop state/history
and sidecar absence. This covers implemented catalog batch fields; crash recovery
and multi-file XMP publication have separate cases and remain unqualified.

Both adapters are in the blocking CI subset and support Cocoa/Metal and offscreen
software runs. Bounded fixture routing and independent TIFF/EXIF decoding are
registered as `ux-metadata-display-fixture-accounting` in CTest.
# Merge workflow qualification — 2026-10-23

`MergeScenarios.cpp` and isolated `merge_fixtures.py` cover MRG001/003/004/005/
006/009/010, HDR001/009, PAN001/002/004, ALG002 and FLT002. Actual UI clicks create
HDR/Panorama jobs. A UX-only friend copies their genuine JobHandle; terminal state
is required before cancellation cleanup is asserted. Real catalog writes are
held through the existing owned SQLite VFS, after decode/alignment/encoding.
Shipping symbol isolation rejects the probe in the distributed executable.

EXIF bracket and exact overlapping crops are deterministic, owned and hash
protected. Independent TIFF/IEEE Float32 and sRGB scene oracles check storage,
dynamic range, source preservation and panorama content. Source URLs are declared
picker fixtures; these tests do not qualify native chooser navigation.

Final results: 42 offscreen and 14 Cocoa/Metal DPR2 PASS. Original failures,
fixture corrections, intermediate HDR rejection and retained limitations are in
[`merge-workflows.json`](../../docs/qualification/2026-10-23/merge-workflows.json).

PAN012/FLT001 additionally run as distinct prepare/verify processes. A successful
merge is edited, saved and closed through the UI. The next process verifies the
complete document, exact original/Float DNG hashes, decoded HDR range, independent
preview and continued edit/Undo behavior. Six offscreen and two Cocoa/Metal DPR2
paired runs pass, with all individual stage results retained in
[`merge-restart.json`](../../docs/qualification/2026-10-23/merge-restart.json).
These adapters do not imply crash/kill recovery coverage.
