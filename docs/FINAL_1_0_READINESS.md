# Final 1.0 readiness — NOT QUALIFIED

The human postponed all builds, tests, static analysis and QA to final preparation.
This document records the handoff requirements; it is not a test report, waiver
or permission to publish. The stable release remains 0.5.1. Do not tag 1.0.0 or
update its Sparkle channel until the complete campaign, fixes and retests finish.

### Release archive/installer identity — 2026-10-24

[Evidence](qualification/2026-10-24/release-package-identity.json): nine controlled,
network-free negative cases reproduced acceptance of stale or missing embedded
ZIP metadata despite correct outer release checksums. Publication now checks exact
embedded/release BUILD_INFO and the plist version/build/app/executable identity
before contacting GitHub. The installer check additionally compares every regular
file, executable bit and symlink target against the ZIP. Only post-signing Developer
ID receipt fields may differ; existing signing/model/source gates remain intact.

Nine publisher tests, ten package negative-control tests and five affected CTest
targets pass. The existing signed 0.9.0 candidate at 91caa17 passes the actual
read-only DMG/ZIP comparison: **2,350 files/symlinks; 564,917,208 regular-file bytes**.
The owned volume detached successfully and archive checksums stayed unchanged.
This old candidate predates the Import Cancel fix and is not a final artifact.

PhotoCard's unchanged-source attribution series passes **50/50 offscreen**, with
normal pressure in every observed callback. This does not establish the cause of
the historical URL-only mismatch, so BUG045 remains open. A nonpublishing hosted
candidate build at 535eb54 has passed signing configuration and is still running;
it predates this package-audit change. No final CI/release approval is claimed.

### History chaos baseline and native measurement routing — 2026-10-24

[Evidence](qualification/2026-10-24/history-chaos-and-native-contracts.json): the
original seeded **10,000-action** schedule now completes with **100 durable
checkpoints**, exact typed documents, natural autosave and retained Undo/Redo.
The replay is byte-identical to the initial failing schedule. That failure and a
diagnostic reproduction remain retained: one Metal RGB channel differed by one
level, with exact alpha. The oracle now uses the existing GPU-compatible Metal
budget; CPU, alpha, geometry, color space and DPR remain exact, and existing
negative controls execute before the scenario. Production rendering is unchanged.

Release CI now mandates the 15 implemented stress IDs and this complete seeded
history baseline, retaining schedules, measurements and execution traces. Local
affected unit/CTest checks, mandatory tidy, format and graph diagnostics pass.
This bounded three-slider/Undo/Redo schedule is not general Chaos Monkey or soak.

The attempted offscreen performance subset records **2 PASS / 4 native-only
precondition FAIL**. The runner now classifies those four explicitly Cocoa-only
contracts before spawning fixtures or child processes: **4 unsupported-backend
SKIP**, exit 1, INCOMPLETE and NOT_QUALIFIED. Twelve routing tests pass. Neither
this classification nor the two functional passes qualify native performance.

An ordinary native readiness recheck at 01:56 UTC still fails before the scenario:
UserNotificationCenter retains foreground while Luma is visible and key-capable.
Manual dismissal remains pending; the system-app safety refusal was not bypassed.
The remaining native, installation/update, artifact, broader data-safety, soak and
hardware gates remain open. No new hosted result or published candidate is claimed.

### Implemented stress campaign and Import Cancel — 2026-10-24

[Evidence](qualification/2026-10-24/stress-import-cancel-fixes.json): the first
software/offscreen stress campaign completed **13 PASS / 2 FAIL**. Passing cases
include 1,000 Exposure inputs, 500 Curve/HSL edits with exact retained Undo/Redo,
100 Auto/Cancel cycles, 200 detail edits plus export, 1,000 preset-store mutations,
100 batch Apply/Undo cycles, a 500-item export queue, 100 export Start/Cancel cycles
and 200 Export panel cycles. Original failed attempts remain retained.

IMP019's SQL counters and copied originals were correct, but a real directory-watch
notification replaced the shared status before the test read it. The test now
captures the actual import-completion transition and requires exact summary/error
values plus the unchanged independent journal/file checks. PERF018's diagnostic
isolates `rejected == 0` after one actual Cancel click/close; every form/document/
history check is true. The production Import Cancel handler now calls `reject()`.

Affected repeats pass **12/12**: three 50-cycle import runs and three 200-cycle
runs for each Import/Export panel, plus repeated-Cancel checks. Mandatory IMP001
now checks Cancel button and Escape separately and passes **3/3**. Full production
build and CTest pass (**71 PASS / 1 unavailable private NEF SKIP**); affected
mandatory clang-tidy, formatting and diff checks pass. Graphify's structural graph
is updated, with known self-loop warnings retained; QML was inspected manually.

All 15 currently implemented stress IDs now have passing observations across these
checkpoints. These are not native Cocoa results, reference-machine performance
acceptance, full soak/chaos coverage or final release approval. The system-dialog
native blocker and remaining final gates are unchanged. The local signed candidate
at source 91caa17 predates this QML fix and must not be treated as a final artifact.

### Phase 1.0 CI routing and local signed candidate — 2026-10-24

[Evidence](qualification/2026-10-24/phase1-ci-and-candidate-signature.json): the
explicit hosted-compatible phase 1.0 route passes **30/30**. CI and release
workflows now run these ten implemented IDs three times even while the source
version remains 0.9.0. Native-only A11Y002 remains a separate Cocoa requirement.

The first route attempt retained **29 PASS / 1 FAIL**: DAY003 selected photos
before a debounced flag-filter reload when old/new result identities matched.
The harness now observes the actual reload before selection and verifies the
selection remains exact after collection creation. Its focused retest passes
**5/5**, followed by the 30-case route. Production code and timeouts are unchanged;
affected mandatory clang-tidy, format, YAML parsing and diff checks pass.

Local 0.9.0 candidate artifacts at source 91caa17 pass strict ad-hoc signature,
SDK-hidden standalone execution and read-only DMG checks. The actual 434,015,893-byte
ZIP is signed with the dedicated Sparkle key and independently verified by
CryptoKit. A one-byte archive alteration, unrelated public key and truncated
signature are rejected; original ZIP/hash manifest remain valid. The candidate
feed is local only and is **not** a published update or final 1.0 qualification.
CI 40037519005 on older head 1c1c37e completed successfully including clang-tidy;
latest-head CI and all remaining release gates still require completion.

### Reproducible stable migration and native activation blocker — 2026-10-24

[Evidence](qualification/2026-10-24/stable-migration-adapter.json) adds
UX-P10-DAT-004. Its checked-in fixture was created by the actual 0.5.1 app:
three RAW/WebP assets, schema 11, global adjustments and complete/local masks,
plus historical export PNGs. Only original-path prefixes are normalized.

The final offscreen run passes **3/3**, with two clean processes per repetition
and **18 exact historical-image export comparisons**. Independent post-exit
checks cover all 11 old tables, 22 current tables, 425 numeric SQL values, full
decoded masks/documents, unchanged originals, an exact readable schema-11 backup
and restart idempotence. There are no saved versions or collections in this fixture.
Eleven auditor negative controls, twelve policy tests, four affected CTest targets,
format and affected mandatory clang-tidy checks pass. Production code is unchanged.

Native attempts fail before the scenario at the existing five-second activation
precondition. Passive AppKit evidence shows a visible, non-minimized, key-capable
Luma window and `com.apple.UserNotificationCenter` as the foreground process.
The computer-use tool refuses access to that system app for safety; manual dismissal
has been requested. No timeout, activation policy or security preference was changed,
and no native migration PASS is claimed. The diagnostics are retained in the runner
for future activation failures.

Applicable IDs without adapters are now **20 blocking / 305 total**. CI run
[40034163108](https://github.com/yakut-sekonts/Luma/actions/runs/40034163108)
completed successfully, including the full clang-tidy step, on older head 7d66d83;
latest-head CI remains pending/in progress. The release job budget is increased
from 120 to 180 minutes: the completed equivalent job took 123m44s before release
packaging ([timing evidence](qualification/2026-10-24/completed-ci-budget.json));
no checks or individual watchdogs were removed. Final installation/update/artifact,
long-running/hardware gates and BUG045 remain open. Local 0.9.0 candidate artifacts
are preparation only; no tag, appcast or public release is authorized by these results.

### Resumed native regression and stable-catalog migration — 2026-10-24

[Evidence](qualification/2026-10-24/native-regression-remainder.json): the resumed
101-case Cocoa/Metal campaign completed **97 PASS / 4 FAIL** with unchanged source
fingerprint and originals. REC008 displayed the correct new preview; one RGB channel
differed by one level. Its oracle now uses the established Metal budget, with exact
CPU/alpha. VIS012's second click targeted a clipped toggle over the Filmstrip after
wheel-to-top. Every cycle now reveals the toggle, checks ancestor clipping and counts
its actual pointer/keyboard clicks. **20 native + 20 offscreen cycles pass**.

A zoom failure reproduced with passive diagnostics: a real macOS memory-pressure
warning changed zoomAvailable to false. The functional fixture now releases its
redundant full FloatImage before input and waits for actual control recovery, without
injecting normal pressure, disabling monitoring or widening timeouts. Final REC008,
zoom and separate warning/critical recovery cases pass **15/15 native + 15/15
offscreen**. Initial failed attempts remain retained. Build, affected mandatory tidy,
format and diff checks pass; production code did not change in this follow-up.

All **275 currently implemented REGRESSION IDs** now have a passing observation
across the source-bound campaign checkpoints. This is not a single-source full release
run or coverage of the **21 blocking / 306 total** unadapted IDs. BUG045, final
installation/update/artifact and long-running/hardware gates remain open.

[Stable migration pilot](qualification/2026-10-24/stable-051-migration-pilot.json):
actual signed 0.5.1 creates and reopens three owned RAW/WebP catalogs. The development
executable migrates schema 11 to 21, preserves historical data and full typed edits,
creates readable exact pre-migration backups and reopens again. Original hashes and
exported PNG bytes match across versions and restarts. Two earlier fixture-setup
failures are retained. This bounded pilot does not qualify final installation or
actual Sparkle replacement; the official adapter inventory is unchanged.

### Native regression crash and responsive Library viewport — 2026-10-24

[Evidence](qualification/2026-10-24/native-regression-fixes.json): the broader
native regression campaign recorded 168 PASS and six scenario failures before
an ImageIO export crash caused ten later processes to block in AppKit's saved-window
recovery prompt. The campaign was stopped, not declared complete; its partial
results and crash report remain preserved. An ordinary LaunchServices diagnostic
restored test-window activation without changing global OS preferences.

Copyright-only export now removes a filtered XMP parent once, rather than first
removing its nested values/qualifiers. A new descriptive-metadata overlay unit
regression reproduced SIGTRAP before the fix and passes afterward. Failed metadata
removal now fails the export instead of silently retaining private fields.
Library retains its user-chosen row and pixel offset across column-count changes,
including resize while Develop hides Library. Before the fix, row 24 became row 0.
The acceptance check now pins the visible asset and offset while allowing Qt to
rebase originY; all document, selection, layout and scroll-distance checks remain.

Three acceptance defects were isolated with evidence: SLD018 differed in one RGB
channel by one level (the existing Metal budget, not an increased tolerance);
PRS006 observed stateChanged instead of previewChanged despite correct C pixels;
PRS012 clicked while an enclosing Flickable was still scrolling. CPU/alpha remain
exact, actual hover ordering is observed, and preset clicks wait for all scroll
ancestors and remain inside their clips.

Final affected cases pass **18/18 Cocoa/Metal + 12/12 offscreen**, including 300
fullscreen round trips. Library/filmstrip selection regressions pass **6/6 native
+ 6/6 offscreen**. Full build, formatting and five mandatory tidy units pass;
CTest records **70 PASS / one private Nikon fixture SKIP**. Existing non-blocking
tidy warnings remain. The remaining 101 executable regression scenarios still
need the resumed broad campaign. Unadapted coverage remains **21 blocking / 306
total**; install/update, long-running and hardware qualification are not waived.

[Hosted blocking evidence](qualification/2026-10-24/hosted-blocking-regression-green.json):
run 40034163108, tested merge 7d795c8 for head 7d66d83, records **366 PASS / zero
FAIL**, with 33 skipped and 31 unimplemented. GPU002 and DNS001/002/008 each pass
three times with unchanged numerical/watchdog requirements. All later functional
steps also pass; full-job clang-tidy was still running at the recorded checkpoint.
This older CI source does not include the latest metadata/viewport fixes.

### Post-exit data invariants after restored daily work — 2026-10-24

[Evidence](qualification/2026-10-24/restored-data-invariants.json): DAT005 restores
through the real backup workflow, validates complete persisted documents, then
closes the app and drains workers. The independent supervisor requires final
context/application destruction and audits the actual restored copy read-only:
22 tables, 158 numeric values, foreign keys, every original hash, owned backups,
import/derived journals and temporary/derived files. Any invariant failure overrides
a child PASS. Retained versions are supported by the validator but absent from
this fixture; no version-history coverage is inferred.

Initial DAT005 passes 1/1 offscreen. Final DAT005 plus DAY010 regression passes
**6/6 offscreen + 6/6 Cocoa/Metal**. Build, 20 affected CTest checks, 15 new negative
and read-only audit tests, 47 runner tests, 12 policy tests, format and both affected
mandatory tidy units pass. Fixture-only SQLite connection warnings were fixed
with explicit closing before the final runs. **21 blocking / 306 total** applicable
IDs remain unadapted. Other golden/chaos/migration and install/update gates stay open.

[Old full push CI](qualification/2026-10-24/historical-ci-40022592049.json) on 8c4ab32
completed with the same 351 PASS / 12 FAIL / 34 SKIPPED / 31 NOT_IMPLEMENTED split;
three GPU002 failures and nine Denoise SIGBUS crashes predate their fixes. Full tidy
passed. The artifact SHA-256 was verified. Current 7d66d83 full CI has passed
photographic Denoise/runtime checks and is still running blocking UX.

### Standalone bundle and current format verification — 2026-10-24

[Evidence](qualification/2026-10-24/standalone-current-bundle.json): the current 0.9.0
application was packaged with the verified v4 models and passes strict recursive
ad-hoc signature verification. With developer Qt/Sparkle directories actually hidden,
the complete standalone verifier passes RAW/common-image Library, editing, complete
masks, sparse XMP, lens/transform/filter/Undo, export/restart, processed DNG, all eight
languages and source integrity. Developer SDK locations were restored afterward.

The verifier itself had stale schema expectations: catalog 11, mask version 2 and
preset library 7. Reproduced failures are retained. It now requires catalog 21,
mask version 3 with two saved masks, and sparse library/entry version 9; all pixel,
restart and parameter checks remain. No product serialization changed. The default
old model cache was correctly rejected; explicit qualified models-v4 packaging
passed. This bundle is source 2fd6f65, version 0.9.0, not a tagged release or DMG
installation/update qualification. **22 blocking / 307 total** remain unadapted.

### Complete 100-photo culling workflow — 2026-10-24

[Evidence](qualification/2026-10-24/daily-culling.json): DAY003 imports 100 distinct
owned images, rates/flags two groups, checks exact filter/selection/collection IDs,
edits one member and applies a preset to ten members. All 100 saved documents are
checked; exactly ten full-resolution sRGB PNGs match independent CPU pixels. All
original hashes, untouched documents and catalog integrity pass. Final series is
**3/3 offscreen + 3/3 Cocoa/Metal**. Initial adapter failures are retained: row count
arrives before identity pages, and equal counts cannot identify a completed query.
The adapter now waits for exact expected IDs with unchanged timeouts/driver/product.
Build, 19 affected CTest checks, format and both affected tidy units pass.
This does not claim photographic quality, native picker navigation or large-catalog
performance. **22 blocking / 307 total** applicable IDs remain unadapted.

### External-volume relink daily workflow — 2026-10-24

[Evidence](qualification/2026-10-24/daily-external-relink.json): DAY009 uses a real
owned HFS+ disk image, normal detach, Missing/Locate, identity and exact full-document
preservation, continued edits/Undo/Redo and exact CPU PNG pixels. Remount verifies
pinned original hashes; mandatory cleanup and catalog integrity pass. Final runs
pass **3/3 offscreen + 3/3 Cocoa/Metal**. Initial offscreen failures were an invisible
fallback chooser retaining keyboard focus after the explicit picker-result fixture;
only that hidden offscreen owner is reactivated. Native runs never needed this.
Build, 19 affected CTest checks, format and both affected tidy units pass.
Physical USB removal and native picker navigation remain separate. **23 blocking /
308 total** applicable IDs remain unadapted.

[Historical full CI](qualification/2026-10-24/historical-ci-40015920664.json)
40015920664 completed on branch head 2e9e8b4 / merge build 863eb852: **351 PASS /
12 FAIL / 34 SKIPPED / 31 NOT_IMPLEMENTED**, full clang-tidy PASS. Failures are three
GPU002 assertions and nine pre-fix Denoise SIGBUS crashes. The summary archive's
GitHub SHA-256 was verified. Hosted Denoise fix already passes 9/9 separately;
current full CI and remaining release gates are still open.

### Offline provider failure preserves daily work — 2026-10-24

[Evidence](qualification/2026-10-24/offline-daily.json): DAY012 runs under a
process-only network-denial sandbox, verified inside the child by EPERM on a local
connection. The production AI provider rejects an unavailable owned custom model
before inference. Its visible error leaves the committed RAW document untouched;
normal editing, durable save, complete Undo/Redo, exact full PNG pixels and Library
away/back continue to work. Original hashes and catalog integrity pass.

Initial and final series each pass **3/3 offscreen + 3/3 Cocoa/Metal**. Mandatory
tidy required explicit handling of a failed socket creation instead of a helper
whose throwing behavior is not visible to the analyzer; no check was suppressed.
Final UX build, 19 affected CTest checks, format and both affected tidy units pass.
System network settings and native model picker are not exercised. This bounded
case does not qualify model image quality or actual installation/update.
**24 blocking / 309 total** applicable IDs remain unadapted. No 1.0 publication.

### Update waits for cancelled workers — 2026-10-24

[Evidence](qualification/2026-10-24/update-worker-drain.json): UPG012 executes six
independent processes for save/import/export/batch/merge/cancelled-merge. First five
passed, but Cancel cleared aiBusy while the real HDR worker was held at catalog
staging, allowing the actual UpdateController installer callback prematurely.
EditorController.readyForUpdate now checks pending GUI/save flags and every owned
JobManager pool. The unit regression retains an old cancelled running worker after
a newer queued handle is already terminal. Production and UX share this predicate.

Corrected checks pass **3/3 offscreen + 3/3 Cocoa/Metal**, each containing all six
variants (36 successful operation processes). Original files and catalog integrity
pass. Full build, CTest (**69 PASS / 1 private-NEF SKIP**), format and eight affected
mandatory tidy checks pass. The initial system-Python read-only audit failure is
retained separately; the configured runtime reached and reproduced the actual bug.
The temporary Denoise workflow push trigger is removed; manual diagnostics remain.

This tests the real deferred SDK callback boundary, without downloading/installing
an update. Actual Sparkle upgrade remains a separate gate. **25 blocking / 310 total**
applicable IDs remain unadapted; no release qualification or publication is claimed.

### Backup status and daily recovery/merge workflows — 2026-10-24

[Evidence](qualification/2026-10-24/daily-backup-merge.json): a real watched-directory
notification could replace the successful backup destination. Backup/restore results
now persist separately, with background notices visible below. The retained initial
series has 10 PASS / 2 FAIL; corrected backup cases pass 12/12 offscreen and 12/12
Cocoa/Metal, including actual watcher events.

DAY010 verifies restoration to a new file, fresh-process exact global/local edits,
continued editing/Undo/Redo and full PNG pixels. The supervisor audits the opened
restored database after child exit. DAY011 exercises both actual HDR and Panorama,
derived-photo Library/Edit/Export and source preservation. An initial adapter-only
export-close assumption caused three parent failures; corrected series passes 3/3
offscreen, each with both variants. Final combined BKP002/DAY010/DAY011 is 9/9 native.
Full build, full CTest (69 PASS / private NEF SKIP), format and mandatory affected
tidy pass. Visual inspection confirms backup destination plus notice and the derived
Panorama export. Graph navigation reports four self-loops, no dangling/duplicate edges.

There remain 26 blocking / 311 total applicable IDs without adapters. Native chooser
navigation, golden photographs and the remaining release gates are not covered by
these owned fixtures. No 1.0 release has been published.

### Hosted Denoise confirmation and release foundations — 2026-10-24

[Evidence](qualification/2026-10-24/release-foundations.json): supervised hosted
run **40028704668 passes 9/9** Denoise checks on 44397a9, with unchanged five-second
watchdog, exact source provenance, originals and catalog integrity. The combined
joined inference stack, parameter cache and coalesced progress fix closes the
reproduced BUG072 crash/watchdog; full branch CI remains pending.

DAY007 now performs an actual SIGKILL after committed global/local edits, restores
the exact document in a new process and continues editing/Undo. DAY013 switches
language mid-project and verifies exact restart/continued work. A11Y002 checks
roles, names and states for eight primary controls in the actual Cocoa AX tree.
Restart regression passes **21/21 offscreen**; restart/zoom **6/6 native**; repeated
release foundations **15/15 native**. Four previously pressure-affected zoom checks
pass in the follow-up without suppressing OS warnings. Historical failures remain.

Full CTest exposed stale stateChanged-only acceptance observers after preview
completion was split into previewChanged. Common-image restore failed and RAW
restore timed out; the initial run was stopped during test 61. The observer now
subscribes to previewChanged, which also forwards document changes. Rebuilt full
CTest: **69 PASS / 1 SKIP** (private Nikon D7500 NEF unavailable), no CLI exclusions.
Full build, format and mandatory affected tidy pass; nonblocking warnings remain.

There are **28 blocking / 313 total** applicable IDs without adapters. This is
partial phase 1.0 coverage, not full qualification or native VoiceOver/chooser
certification. Installation, actual update, artifact, load/stress/soak and other
remaining gates still apply. No release has been published.

### Coalesced Denoise progress and batch/history — 2026-10-24

[Follow-up evidence](qualification/2026-10-24/denoise-progress-coalescing.json): hosted
run 40027066662 is **2 PASS / 1 watchdog FAIL**. Its thread sample places the GUI
in repeated QML parameter-map work while the AI worker has reached staging. DNG
progress reports each row; Denoise now coalesces those values into one pending GUI
callback, with the original revision/cancellation guards. The five-second watchdog
is unchanged. Local confirmation is **9/9 offscreen + 3/3 Cocoa/Metal PASS**, with
full build, four affected CTest checks and mandatory tidy PASS. Hosted confirmation
on the combined progress/parameter-cache fix remains pending.

The previously deferred 100-photo batch and 50-edit Undo/Redo scenarios separately
pass **4/4 offscreen + 4/4 Cocoa/Metal**. These are bounded functional/load cases;
they do not replace long stress/soak or whole-release qualification.

### Progressive feedback and hosted Core ML stack — 2026-10-24

[Evidence](qualification/2026-10-24/progressive-preview-denoise.json): real native
PRG001 passes **3/3**, ten held Clarity inputs each, p95 **37.613 / 30.449 / 42.001 ms**
under the unchanged 50 ms budget. Direct-source 512 feedback refines to 1024 and
2560; independent pixel, committed document and Undo checks pass. Row-parallel
rendering preserves deterministic histogram accumulation. Revision-keyed QML
parameter caching removes repeated map construction during the input callback.
A strict baseline schema/adapter validates raw samples and comparable hardware;
its candidate baseline does not qualify other hardware or release performance.

Full build and **17 affected CTest checks PASS**. Offscreen regression is
**68 PASS / 22 FAIL / 4 SKIPPED**. LRU001's uniform-entry assumption was corrected
for actual transient/final byte costs; follow-up LRU/Denoise is **4/4 offscreen**.
Native follow-up is **3 PASS / 6 FAIL**: GPU002, LRU001 and DNS001 pass; four zoom
cases encounter actual memory warning, and passive DNS002/008 logs confirm OS
normal-to-warning cancellation. The monitor and acceptance budgets remain intact.
These failures are not converted to PASS and memory/load qualification stays open.

macOS 15 LLDB identifies Core ML BNNS stack overflow on the default worker stack.
A synchronous joined 8 MiB inference worker fixes that crash, with ownership,
exception and stack-page tests. Subsequent hosted watchdog failures remain open;
a separate debugger run passes but does not satisfy the supervised gate. GPU002
now waits for the actual settled render/refine/histogram state before comparing
CPU/Metal counters; local checks pass, hosted confirmation is pending.

The last two phase 0.6–0.9 blocking adapters (PRG001/PERF001) exist. This is coverage,
not completion: phase 1.0, failed regressions, load/stress/soak, image/model quality,
hardware, artifacts and actual upgrade remain unqualified. No 1.0 publication.

### Persistence checkpoints and owned staging cleanup — 2026-10-23

[Evidence](qualification/2026-10-23/persistence-kill-recovery.json): **24/24 offscreen
+ 8/8 Cocoa/Metal DPR2 PASS** across XMP, both sides of catalog COMMIT, backup,
relink, metadata batch and HDR registration. Actual parent SIGKILL is followed by
fresh application processes. Full documents, originals, exact old/new revisions,
prior backups and exactly-once registration/retry are checked independently.

BUG070 is fixed for fully recorded new stages: separate locked ownership records
identify exact path/device/inode. Recovery removes only inactive matching stages;
live operations, unmarked legacy files, symlinks and replaced inodes survive.
The first inode-flock implementation blocked SQLite backup; that failure is
retained and corrected by locking only the separate record. Full build, nine
affected CTest entries, strengthened staging safety unit, format, mandatory tidy
and shipping/UX symbol isolation pass. Earlier fixture/oracle/unit/tidy failures
remain in the evidence. Native backup/HDR result screenshots were inspected.

Cleanup is conservative: legacy or incompletely initialized ownership records
are preserved. Power loss, external disks and native picker navigation are not
certified here. **Two** phase 0.6–0.9 blocking IDs still need adapters. Phase 1.0,
complete load/performance/quality, artifacts/update and BUG045/069 remain open.

### Atomic XMP publication and real process kills — 2026-10-23

[Evidence](qualification/2026-10-23/xmp-atomic-crash.json): XMP001/XMP012/FLT002
pass 9 repeated offscreen and 3 Cocoa/Metal checks after the initial 3 passes.
A real parent SIGKILL occurs before or after file installation, before catalog
acknowledgement commit. Fresh processes retain complete old/new sidecars, pending
Unicode drafts, original hashes and Develop documents; actual UI retry succeeds.
Full build, five affected CTest entries, format, mandatory tidy and shipping/UX
symbol isolation pass. Native recovery screenshot was inspected.

**Open:** pre-install kill leaves an owned staging orphan (BUG070); these passes
certify atomic data/retry, not cleanup. Hosted run 39999961136 on a7b30d4 reports
286 PASS / 2 FAIL / 59 SKIPPED / 31 NOT_IMPLEMENTED: GPU002 repetitions 2/3 fail
its real Metal completion/numeric assertion (BUG069). Ten local diagnostic runs
and three final regressions pass. New passive counters distinguish cancellation,
exceptions, empty results and invalid sizes; no acceptance/budget is relaxed and
no root cause is claimed. Seven applicable blocking IDs still lack adapters;
final release qualification remains incomplete.

### Actual Remove and semantic-search workflows — 2026-10-23

[Evidence](qualification/2026-10-23/remove-semantic-workflows.json): final source
passes **18/18 offscreen** (including mask-profile regression) and **4/4 Cocoa/Metal
DPR2**. Real Remove computes its donor before the test retains the original queued
dispatch; progress/Cancel remain visible. Cancel rejects that exact late result;
success reduces the known central defect, matches independent CPU pixels and is
one exact Undo/Redo unit. Actual Vision/sentence embedding ranks the declared
portrait/river/night queries correctly and opt-out deletes persisted embeddings.
Source hashes, catalog integrity and provenance pass with no QML warnings.

Full build, four affected CTest entries, format, mandatory affected tidy and
shipping/UX symbol isolation pass. Initial 4 + 12 + 4 passes are retained;
static review then prompted exception containment in the test-only observer.
All final checks require its explicit hookFailed state to remain false. Other
nonblocking diagnostics remain in the logs. Native result screenshots were read.
The center-defect fixture does not certify feathered edges or broad photographic
removal quality; three search queries do not certify arbitrary semantic quality
or network privacy. **10** applicable blocking IDs still need adapters, alongside
the broader final qualification and actual artifact/update checks.

### Actual AI subject-mask lifecycle — 2026-10-23

[Evidence](qualification/2026-10-23/ai-mask-workflow.json): **6/6 repeated offscreen
+ 2/2 Cocoa/Metal DPR2 PASS**, plus the initial two PASS. Real Apple Vision operates
on the pinned NASA portrait. Exact bitmap Undo/Redo, visible overlay, local
exposure pixels, Enabled/Invert/Delete and complete Undo all pass; originals and
catalog remain intact. The native overlay screenshot was visually inspected.
Full build, five affected CTest entries, format and mandatory affected tidy pass.
This does not certify segmentation boundaries across photographs or providers.
There are **14** applicable blocking IDs still without adapters; no release gate
is waived or inferred from this workflow result.

### Fresh-process catalogs at 10k / 100k / 500k — 2026-10-23

[Evidence](qualification/2026-10-23/catalog-scale-open.json): **9/9 Cocoa/Metal
DPR2 PASS**, three separate prepare/verify process pairs per size. Actual startup
opens the existing catalog without test repopulation or refresh. Nine physical
scrollbar positions require bounded delegates/pages and real visible IDs,
filenames and Missing state. The 500k screenshot was visually inspected after
adding the populated-card requirement. Catalog integrity/foreign keys pass.
Six affected recovery/Denoise/Float DNG/metadata/XMP/backup regressions, seven
CTest entries, full build, format and mandatory affected tidy pass.

The original three failures expose an incorrect test expectation that working()
becomes false after closing_ is set; it does not. A retained 500k failure and
thread sample identify the test's full PRAGMA integrity audit on the GUI thread.
That same complete oracle now runs in a worker while the genuine heartbeat and
unchanged five-second watchdog stay active. The initial nine native passes did
not require populated cards; their screenshot exposed transient placeholders.
The stronger final runs use an isolated empty source folder so evidence writes
do not trigger source-watch reloads, and wait for actual visible card data.
All earlier outcomes remain retained; none is rewritten as PASS.

These are **metadata-only catalogs**, not photo-decode or sustained-load tests.
Expected missing-original QML warnings remain recorded. Whole-run timings include
setup, audit and shutdown and do not qualify startup latency. No 16 GiB/external
display or memory-slope qualification is inferred. Remaining applicable blocking
IDs without adapters: **16**. Complete release qualification remains open.

### Bundled AI Denoise workflow — 2026-10-23

[Evidence](qualification/2026-10-23/denoise-ui-workflow.json): **9/9 repeated
offscreen + 3/3 Cocoa/Metal DPR2 PASS**, plus the first 3/3 offscreen run.
Full build, six affected CTest entries, format and mandatory affected tidy pass;
nonblocking warnings remain. DNS001/002/008 use the actual bundled CoreML model,
visible processing/progress/Cancel and real storage-boundary cancellation after
inference. Cancel preserves committed source edits/history and leaves no partial
DNG, journal or catalog result. Success creates exactly one unedited Float32 DNG
with provenance; Develop preview matches independent CPU pixels and Undo works.
A separate fresh process retains exact hashes, identity and the full saved edit
document. Native processing screenshots were inspected.

No inference path is substituted or pressure callback suppressed. This 256×192
workflow fixture does not qualify photographic quality, latency, large inputs
or crash recovery. Remaining applicable blocking IDs without adapters: **19**;
complete release qualification remains open.

### Editing tools at zoom/pan — 2026-10-23

[Evidence](qualification/2026-10-23/zoom-tool-mapping.json) adds ZOM006–009.
Crop, Guided Transform, Local Adjustments and targeted tone/HSL no longer force
Fit. Render and viewport dimensions share the same prepared canvas document;
zoomed tools render the full source. Tool drags retain pointer ownership, while
wheel/pinch remain available. Canvas stores are clipped to the visible viewport
and paint translated source coordinates; inactive stores also stay bounded.

**4/4 Cocoa/Metal DPR2, 12/12 Fit regressions and 11/11 affected CTest PASS**.
Full build, formatting and mandatory affected tidy pass; nonblocking warnings
remain. Actual controls exercise 100%/137.5%, both pan axes, Crop corners, Guided
endpoints, stored Brush path, independent tone/HSL sample and delta, one-unit
Undo/Redo and source hashes. Actual scene pixels locate Crop/Guide handles.
Canvas dimensions remain bounded at 1600%. The first three tools use full
3072×2304 pixels; targeted sampling uses 2048×1536 to isolate mapping.

Local repeated large-fixture checks are **incomplete**: three Crop repeats pass,
then nine failures remain recorded. Eight explicitly observe real OS memory
warning; one reset after an initial normal snapshot has no conclusive transition
trace. Separate isolated runs pass the other three once after real kernel-normal
preflight. The next planned cycle did not start because kernel warning persisted
for 60 seconds. No pressure callback was suppressed and no normal state injected.
A previous small-fixture run also preserves a clipped Crop-start harness error
and three uninstrumented availability failures; after correcting the pointer
fixture, all four small cases pass. No failed row is converted into a pass.

Remaining applicable blocking IDs without adapters: **22**. Hosted confirmation,
complete repetitions, memory/load qualification, BUG045 and the final release
gate remain open. These results are not a performance or all-tool certification.

### HDR alignment/deghost controls and bounded transforms — 2026-10-23

[Evidence](qualification/2026-10-23/hdr-options-alignment.json): **21/21 offscreen,
7/7 Cocoa/Metal DPR2 and eight affected CTest entries PASS**. Full build, format
and affected blocking tidy pass; nonblocking warnings remain. Four new blocking
adapters cover HDR006/007, DGH001 and ALG001. Default-on Align frames now has a
real localized checkbox and passes through service/core; output provenance records
both options. On/off jobs preserve originals and source edits, create distinct
Float DNGs, and match independent preview pixels.

Known shifts reduce MSE from .002533 to .000731; moving-object deghosting reduces
it from .038294 to .000189. All repeats use the unchanged maximum .6 error ratio.
Actual platform registration maps a 121-point grid within one pixel of the known
translation, with finite coefficients and inverse round-trip. Its real UI panorama
consumer also meets the prior .001 scene-pixel budget. Cancel, default HDR and
clean Float DNG restart regressions pass. These bounded fixtures do not qualify
all photographic scenes, large jobs or performance. Remaining applicable blocking
IDs without adapters: **26**. BUG045 and the complete release gate remain open.

### Derived Float DNG across clean process restart — 2026-10-23

[Evidence](qualification/2026-10-23/merge-restart.json) adds PAN012 and FLT001:
**6/6 offscreen and 2/2 Cocoa/Metal DPR2 paired-process runs PASS**. Every outcome
contains distinct prepare/verify process IDs. Actual merge, negative exposure,
save and clean UI quit precede verification of exact source/derived file hashes,
Float32 representation, decoded HDR range, identity, catalog membership, complete
saved edit document and independent CPU preview. A fresh Contrast edit and Undo
work after restart; session-local history does not cross processes. All stages
retain catalog integrity and have no QML warnings. Four affected CTest entries,
format and affected blocking tidy pass; nonblocking warnings remain.

Production code is unchanged by this follow-up. This covers clean restart, not
kill/crash or interrupted derived registration. Remaining unadapted applicable
blocking IDs decrease to **30**; the complete release gate remains open.

### HDR/Panorama creation, cancellation and Float DNG — 2026-10-23

[Evidence](qualification/2026-10-23/merge-workflows.json) adds fourteen official
adapters, eleven blocking. The new block passes **36/36 offscreen workflow runs,
6/6 offscreen refusal/recovery runs and 14/14 Cocoa/Metal DPR2 runs**. Full build,
13 affected CTest entries, formatting and affected blocking clang-tidy pass;
nonblocking warnings remain. Original hashes, catalog integrity, source/build
provenance and QML warnings are checked. Tests observe the actual merge worker's
termination, not just the cleared busy flag.

Three reproduced defects are corrected: cancellation while journal staging is
blocked no longer installs a hidden result; per-row progress no longer floods
the GUI queue; panorama alignment verifies correspondence and considers large
translations instead of accepting a wrong but finite Vision homography. The
overlap/correlation check applies only to panoramas, because differently clipped
HDR brackets do not meet that photometric assumption. A retained intermediate
run documents that rejected HDR experiment. Current phase labels cover all eight
languages. Expected-error tests recover by opening an intact source; they do not
erase the error through test-only setters.

Owned fixtures contain a real three-exposure EXIF bracket and two overlapping
PNG crops. An independent TIFF reader requires Float32 LinearRaw, finite pixels
and HDR values above SDR white; decode and Develop must retain those values.
Both panoramas match the known scene within the fixed .001 linear RGB budget,
including the added area. Unrelated crops refuse cleanly. This does not qualify
all photographic scenes, projections, deghosting, restart/crash recovery, large
sequences or performance. Remaining unadapted applicable blocking IDs: **32**.

Two earlier hosted checkpoints (`a056341`, `282f781`) each have **243 PASS,
74 SKIPPED, 31 NOT_IMPLEMENTED, zero executed FAIL** in their blocking summaries,
with verified archive hashes and intact source/originals. Their gates remain
NOT_QUALIFIED for missing coverage; these artifacts do not qualify the new merge
changes. BUG045 and the complete release gate remain open.

### Neutral CPU and actual Metal preview agreement — 2026-10-23

[Evidence](qualification/2026-10-23/neutral-metal-preview.json) adds blocking
UX-P06-GPU-002. A forwarding observer wraps the genuine controller accelerator
without changing eligibility or returned pixels. Every actual linear RGB sample
must satisfy the existing MetalDevelopTests budget `2e-5 + 2e-5 * abs(reference)`;
complete native execution is required, so silent CPU fallback cannot pass.
Actual Settings toggles Metal off/on. Preview buffers and completed Image-node
readbacks must match the independent CPU reference and each other within one
RGB8 step and exact alpha. Saved edits/revisions, identity, selection, Undo/Redo
and source hashes remain unchanged.

The new scenario passes **3/3 offscreen**, with exactly two real Metal completions
and zero linear RGB difference on each neutral fixture. Disabling Metal produces
no extra accelerator call. The existing GPU001 UI regression passes **1/1**.
Full app build, Metal equivalence, workload policy, formatting and affected
blocking tidy pass; nonblocking tidy warnings remain. Symbol isolation also passes
after correcting its inspection to include local observer definitions, preserving
the original global renderer-signature check. The initial isolation failure and
an initial unsigned-to-QJsonValue build error are retained as harness corrections.
No product rendering defect was observed in this scenario.

Fresh native confirmation passes **1/1 Cocoa/Metal DPR2** on clean commit
`4157cf9`, with matching source/build provenance, clean original/catalog integrity
and no QML warnings. Three actual Metal completions (two during initial native
loading, one after re-enabling) have zero linear RGB difference; disabling Metal
adds no accelerator call. Hosted verification remains pending. This is a
bounded neutral PNG check, not all RAW/calibration/edit combinations, GPU failure,
profile transitions, hardware coverage or load/performance qualification. QML
uses software offscreen while Develop's actual Metal compute is observed directly.
The extra CPU comparison runs in the worker; timings are not performance evidence.
Remaining unadapted applicable blocking IDs are **43**; the release gate and
BUG045 remain open.

### Persisted thumbnail cache byte cap — 2026-10-23

[Evidence](qualification/2026-10-23/thumbnail-cache-bound.json) adds blocking
UX-P06-LRU-002. A real warm provider response left an oversized persisted cache
at 536,882,181 bytes, above the 536,870,912-byte cap. New thumbnail publication
already enforced the cap, but a cache-hit-only session never did. The provider
now checks its first successful disk hit under the existing cache mutex; later
hits do not rescan, and every new publication retains its existing trim.

Two owned images and sparse cache ballast exercise cold insertion, oldest-entry
eviction and a persisted JPEG loaded without its original. A newly created real
engine provider and a never-displayed second image rule out Qt pixmap reuse;
actual response/texture observations are mandatory. Real filters and scene pixels,
full saved documents/revisions and original hashes are checked. The new case
passes **3/3 offscreen**, with six affected CTest entries, full application build,
format, affected blocking clang-tidy and 11 policy tests PASS. Fresh native
confirmation passes **1/1 Cocoa/Metal DPR2** on clean commit `c985ef6`, with
matching build provenance, intact catalog/original hashes and no QML warnings. Earlier fixture failures are retained
and distinguished from the actual provider negative control.

This is disk byte accounting with sparse files, not a 512 MiB physical-write
workload, whole-app restart, memory-growth or multi-process cache-concurrency
qualification. External enlargement during an already checked provider lifetime
is not watched. Remaining unadapted applicable blocking IDs decrease to **44**;
the complete release gate and BUG045 remain open.

### Missing originals and relink preservation — 2026-10-23

[Evidence](qualification/2026-10-23/relink-missing-originals.json) adds official
RLK001/002/003/004/006/007 coverage: real missing badges, file/tree relink,
same-name/same-size wrong-checksum refusal and retry, saved edits/metadata/
collection/selection/history preservation, and reopening the new path with an
independent CPU pixel oracle. Four of these IDs extend blocking coverage.

An actual parent-directory move left Library's availability unchanged. Watching
existing ancestors within the existing 512-watch cap and rearming after changes
now detects both removal and restoration. The event timer coalesces bursts without
postponing an already scheduled check. Tests also move a file after restoring the
folder, and exercise ongoing unrelated directory activity. The initial suite
and an intermittent pre-coalescing unit timeout are retained. The latter's exact
cause is not inferred from the subsequent passing repeat. Chooser-fixture cleanup
now uses the actual metadata Close button instead of sending Escape to a stale
popup focus target; this was a harness correction.

The six scenarios pass **18/18 offscreen** with clean catalog/source integrity and
no QML warnings. Full application build, format, affected blocking clang-tidy,
11 policy tests, and **three affected CTest entries repeated three times** pass.
The six cases also pass **6/6 Cocoa/Metal DPR2** with clean original/catalog
integrity and no QML warnings; the quiet-desktop run is complete.
The initial native folder case stopped at the declared chooser fixture: Cocoa
returned the current repository directory instead of the assigned target. Preparing
the chooser's initial directory before the actual Locate click fixes the fixture;
all six cases then pass on Cocoa/Metal and again offscreen. Both earlier native
failures and the path readback diagnostics are retained. Production code is unchanged.
Selected file/folder URLs are declared fixtures, not native picker qualification.
Push and PR runs include the new scenarios through the shared CI workflow. Volume
eject/remount, network paths, large catalogs and crash recovery remain separate.

Verified `fe64231` push/PR blocking artifacts each contain **225 PASS, 80 SKIPPED,
31 NOT_IMPLEMENTED and 0 FAIL**. Source/original checks and build provenance pass.
The failed blocking step correctly represents incomplete coverage, not an executed
test failure. The prior PR run 39971392506 is now complete with overall FAILURE:
all supplemental UX and full format/tidy pass; the final model aggregate step is
SKIPPED. Its artifacts predate the new metadata and relink coverage. Current-head
CI is still pending. BUG045 remains open and the complete release gate is unchanged.

### Metadata display and atomic batch organization — 2026-10-23

[Evidence](qualification/2026-10-23/metadata-display-batch-atomicity.json) adds
UX-P09-META-001/006 to blocking coverage. The first displays six capture-data
values from a dedicated EXIF IFD DNG, saves/reopens all nine descriptive fields,
and preserves real GUI navigation/selection while a metadata completion is held.
The second selects three of four photos and changes Rating, Flag and Label through
actual controls. Each operation holds a successful SQLite write, observes old
values from an independent connection, injects `SQLITE_FULL`, verifies full
rollback and a visible error, then retries successfully. Each selected revision
advances exactly once; the unselected control and Develop/original/sidecar state
remain unchanged.

The complete metadata/XMP regression passes **24/24 offscreen**, and the two new
cases pass **2/2 Cocoa/Metal DPR2**, with no QML warnings or integrity failures.
Full application build, formatting, affected blocking tidy, 11 policy tests and
two dedicated EXIF/routing tests pass. After CTest registration, the fixture/policy
regression passes **10/10 CTest entries**. Initial fixture failures are retained:
LibRaw's normalized camera name and a main-IFD-only synthetic source did not match
the new full-value expectations. The new fixture has an actual EXIF IFD; default
DNG bytes remain identical. Neither production behavior nor acceptance gates were
relaxed. These are completion-boundary and catalog-transaction checks, not slow
I/O, large-catalog, multi-file XMP or crash-recovery qualification.

On prior commit `fe64231`, the main PR run **39971392506** has passed all model,
build and CTest stages: **62 PASS / 1 private Nikon RAW SKIP**, including both XMP
fixes. The CTest archive digest is verified. UX/static stages are still in progress;
this is not overall CI PASS and does not include these new metadata adapters.

### Hosted XMP portability and retained UI failures — 2026-10-23

[macOS 15 evidence](qualification/2026-10-23/xmp-macos15-portability.json)
confirms two ImageIO compatibility defects: rejected legal XML prologs/leading
whitespace, and non-default language substitution. XML-based merging preserves
foreign properties/languages; validated document-element input and declaration-free
output restore compatibility. The affected hosted unit + Library integration
pass **2/2**, with production/unit sources matching main commit `9f55a9a`.
Full local build and CTest pass **62 / one private Nikon SKIP**; the six affected
metadata/XMP UI scenarios have **18 offscreen PASS** across two reports. An
accidentally selected unadapted META001 remains explicitly SKIPPED. Fresh native confirmation passes **6/6 Cocoa/Metal DPR2** without QML warnings
or integrity failures. The isolated hosted UX run also passes **18/18**. This is not full CI PASS.

[Retained CI/UI evidence](qualification/2026-10-23/ci-b404ad0-followup.json)
records all b404ad0 UX failures. Library scroll now checks synchronized rendered
frames and the final anchor at the unchanged one-pixel tolerance, retaining raw
in-handler movement; it passes **6/6 offscreen and 2/2 Cocoa/Metal DPR2**. PhotoCard's final-result gate
is armed after the edited interactive image, and passive OS pressure observations
are retained. The latest **10/10 offscreen and 1/1 native PASS** do not close BUG-045: a prior local failure
changed only preview URL, with identical pixels and preserved document/history.
The cause remains open. Hosted scroll and PhotoCard retests pass **6/6 and 3/3**.
Full hosted clang-format/clang-tidy passed on both b404ad0 runs. Both overall
runs failed on the retained unit/UX failures. Qualification now queues the
latest new commit without cancelling an active scan. Other branches retain
their previous cancellation policy; release gates and deadlines are unchanged.

### Generated XMP validation — 2026-10-23

[Evidence](qualification/2026-10-23/xmp-generated-xml-validation.json) records a
separate reproduced edge case: valid UTF-8 U+FFFE/U+FFFF produced invalid XML in
new sidecars. Generated packets are now parsed before returning bytes for file
publication. Actual MetadataService checks retain the old sidecar or absence of
one, acknowledgement, catalog draft, original and edits after failure; valid
Unicode publication is the positive control. Full application build, **3 affected
CTest checks and 12 offscreen UX repetitions PASS**, as do format and affected
blocking tidy. Earlier native/hosted portability passes precede this final
validation change and are not claimed as retests of it.

### Preview cache budget and explicit clear — 2026-10-23

[Evidence](qualification/2026-10-23/preview-cache-budget.json) adds official
UX-P06-LRU-001. Actual Settings reduces the configured budget from 256 to 64 MiB,
evicts rendered cache entries to the production 16/32/16 MiB split, and clears
all three caches. The active image remains exact; Undo/Redo reconstruct the
expected CPU pixels and persist precisely two revisions. Document, selection,
history and original hashes remain intact; no QML warnings occur.

The adapter passes **3/3 offscreen and 1/1 Cocoa/Metal DPR2**; existing 3072×2048 zoom and controlled
memory-recovery cases pass **2/2** after the shared fixture gained an optional
size. Affected blocking clang-tidy and 11 policy tests pass; nonblocking tidy
warnings remain. Native confirmation has no QML warnings and preserves catalog/
original integrity; Mac is free. This measures cache-owned
references and bytes, not RSS, physical memory pressure or load qualification.
The OS notification stream is deliberately isolated in this owned test controller.

### Independent hosted checks after a failed gate — 2026-10-23

[Hosted evidence](qualification/2026-10-23/ci-memory-pressure-recovery.json)
captures the final available `e5850c1` artifacts: **210 executed blocking UX
repetitions PASS**, with 56 applicable blocking adapters absent. Twelve completed
supplemental stages pass. The preview-ordering report also has 18 PASS, but its
GitHub step was cancelled when `b404ad0` superseded the run. Viewer input, Auto
Transform and full static analysis did not execute; no overall CI PASS is claimed.
The artifact digest is verified and both the report and cancellation are retained.

On `b404ad0`, the early blocking artifact has **219 PASS and 3 FAIL**: all three
UX-P09-XMP-003 repeats fail while importing existing XMP on hosted macOS 15.
Original/catalog integrity remains intact. Unit/integration and the separate
Library-scroll stage also fail; their detailed final artifacts are not yet
available at this snapshot. An isolated diagnostic workflow on the exact source
commit runs XMP and Library tests without interrupting the main qualification.
A full local CTest rerun still has 62 PASS / 1 private Nikon SKIP; this does not
supersede the hosted failures. Subsequent CI now uploads CTest diagnostics before
UX so failures can be examined immediately. Gate conditions remain unchanged.

### Catalog restore and destination protection — 2026-10-23

[Evidence](qualification/2026-10-23/catalog-restore.json) adds BKP002/006/007 to
the official adapter set. Together with manual backup, all four cases pass
**12/12 offscreen and 4/4 Cocoa/Metal DPR2**. A real edit after backup makes the live catalog different;
restore reproduces the selected snapshot's edits, revision and photo identity.
Corrupt input publishes no destination, and restoring over the active catalog
is explicitly refused. Existing backups, originals and the live document/history
remain unchanged. No temporary leftovers or QML warnings occur.

This exercises the production restore handler through a declared FileDialog
selection fixture, not native source/destination pickers. Fresh native confirmation passes all four cases without QML warnings or original/
catalog integrity failures; the one-minute interval is complete and Mac is free. Full build,
5 affected CTest checks, 11 policy tests, formatting and affected blocking tidy
pass. CI/release run all four cases three times and retain reports. The current
inventory has 52 applicable blocking scenarios without adapters; neither that
inventory count nor these bounded checks qualify the outstanding release scope.

### Native metadata and XMP lists — 2026-10-23

[Evidence](qualification/2026-10-23/metadata-xmp-lists.json) records a confirmed
ImageIO interoperability defect: creators/keywords can be returned as arrays of
`CGImageMetadataTag`, while Luma required bare strings. Both external packets and
Luma's own written lists failed to read. Unwrap only string-valued tags and retain
strict structure/type checks and the 256-item limit. Native unit regression
covers Unicode, ordered/repeated creators, unique keywords, exact roundtrip,
normalized scalar spelling and rejection of nested/oversized lists.

Six new metadata/XMP UX adapters pass **18/18 offscreen and 6/6 Cocoa/Metal DPR2**:
Unicode form save/reopen, catalog-only storage, explicit XMP write/read,
confirmation cancellation, external-write conflict and malformed input. Original
hashes, catalog integrity, edits, history, revision and selection remain intact;
no QML warnings occur. The native interval is complete and Mac is free. Timing
is MEASURE_ONLY; a 2398 ms native stall is retained, not counted as a latency pass.

Full application build and **62 CTest checks PASS**, with one SKIP for the absent
private Nikon RAW. Formatting and affected blocking clang-tidy pass; nonblocking
warnings remain. Initial harness Tab-input and XML-fixture failures are retained
separately from the reproduced production defect. These checks do not qualify
crash/disk-full recovery, native file pickers, load or the complete phase 0.9.
Full hosted CI and all remaining release gates are still required.

CI and release workflows explicitly run all six metadata/XMP cases three times
and retain their report. Only two belong to the blocking inventory; the four
regression cases must not disappear from hosted coverage. This addition keeps
existing failure propagation and source attestation requirements.

### Cancel Import and catalog maintenance — 2026-10-23

[Evidence](qualification/2026-10-23/import-cancel-maintenance.json) records a
confirmed production defect: Cancel Import left its cancellation token set, and
later backup/restore, XMP and relink operations reused that token. Actual
LibraryModel reproduction passes backup before cancellation and then fails with
`Operation cancelled.`. An otherwise identical no-cancel control passes.

Maintenance now has a separate token, cancelled by Request Close/destruction.
The integration regression verifies backup/restore, explicit/pending/automatic
XMP, metadata readback, file/folder relink and preserved identity, edits,
revision, metadata, collection membership and original hash. A deterministic
close-before-worker control confirms that shutdown still cancels maintenance
without publishing a snapshot or accepting new work.

New blocking UX-P09-BKP-001 passes **3/3 offscreen and 1/1 Cocoa/Metal DPR2**.
Actual Settings and Back up now clicks create readable snapshots with current
saved edits and retain previous good copies. Live document, selection, history
and originals stay unchanged; no QML warnings or integrity failures occur.
This bounded case does not qualify restore dialogs, retention rotation,
disk-full/crash recovery or load. The native interval is complete; Mac is free.

Complete application build, **7/7 affected CTest checks**, **40 runner tests**,
**11 policy tests**, formatting and affected blocking clang-tidy pass. The
duplicate local full-tree tidy scan was deliberately stopped before source
edits and remains INCOMPLETE; its log is retained. Full hosted CI/static and
all other outstanding release requirements remain open. No 1.0 publication.

### Hosted CI and memory recovery — 2026-10-23

[Verified evidence](qualification/2026-10-23/ci-memory-pressure-recovery.json)
records hosted `21b39ac` CI: full build, model numerical/source/photographic
checks and unit/integration pass (**61 PASS / 1 private Nikon SKIP**). All
**204 executed blocking UX repetitions pass**, including ZOM001 3/3 after the
async CPU-oracle correction. The stage correctly exits INCOMPLETE: 58 applicable
blocking adapters were absent and 29 load scenarios were deferred. This is an
incomplete qualification, not an executed-case failure or an overall CI PASS.

The workflow now runs independent UX and static checks after any earlier
failure, provided their build prerequisite succeeded and the run was not
cancelled. No UX `continue-on-error` or acceptance waiver is added. Every failed
step still fails the job. The job ceiling is 180 minutes to allow the model,
regression and full static sequence to finish; per-case limits are unchanged.

The next two adapters expose a real recovery defect: normal pressure restores
the controller's zoom availability and cache budgets but did not notify QML.
Both warning and critical controls reproduce an enabled controller with disabled
buttons and zero state notifications. The fix notifies on recovery without a
new render. Identical post-fix checks pass **6/6 offscreen**; three reclaimable
cache budgets/entries reach zero under pressure and restore correctly. Pixels,
document, catalog revision, history, selection and original hashes remain intact.

These are bounded injected-handler checks. Their owned UX controller stops and
drains OS notifications before prescribing the transitions. Initial attempts
with live OS notifications are retained: genuine host warning disabled zoom
before the intended test. This does not qualify physical memory stress, RSS,
OS delivery, or AI/retouch cancellation.

The fresh approved quiet interval passes **6/6 native** on Cocoa/Metal Qt scene,
Retina DPR2: PRV012, ZOM003/004/012 and MEM001/002, with exact CPU photo references,
no QML warnings and intact original/catalog checks. Mac was released. Pinch uses
Qt native-gesture delivery, not a physical trackpad. Full application build,
5 affected CTest checks, 40 runner and 11 policy tests, whole-tree formatting
and affected blocking clang-tidy pass. A previous full tidy scan was invalidated
by concurrent source edits and stopped; its log is retained, not counted PASS.
Full hosted static analysis and the remaining release scope still need evidence.

### Viewer scale and input — 2026-10-23

[Evidence](qualification/2026-10-23/viewer-scale-and-input.json) records the
implementation of Fill, manual 1–1600% (including 2:1), pointer-anchored wheel and
pinch zoom, smooth/momentum pan, and double-click Fit/100%. Scale changes on an
already full-resolution image do not start another render or change saved edits,
history or selection. New-photo, pressure and full-resolution failure reset Fit.

The first input runs exposed incorrect scrolled-content coordinates for wheel
and pinch. Scene-to-viewport conversion fixes both; queued wheel events preserve
their pending bounded transform. Full-to-full anchors are exact; the rounded Fit
raster and 1:1 backing-pixel snap have explicit half-source-pixel budgets.

Two subsequent 1600% runs hit the unchanged five-second watchdog. Disabling
image smoothing did not fix this and was reverted. Inactive tool Canvases were
following the 49152×32768 magnified image. Keeping their geometry zero/tiny while
zoom is active makes the same scenario pass, with original smoothing retained.
Their dimensions are now asserted. Original failures and incomplete stack
samples remain in the evidence; no watchdog or integrity limit was relaxed.

The affected regression passes **51/51 offscreen/software DPR1** repetitions,
with no QML warnings or original/catalog integrity failures. Full application
build, **9/9 affected CTest checks**, **40 runner tests**, **11 quality-policy
tests**, keyboard import/edit/export and eight-language preset dialogs pass.
All eight languages also keep the new controls within a 1000×640 window.
Affected formatting and blocking clang-tidy checks pass; nonblocking warnings
remain. PRV012 enters qualification through an explicit implemented-target phase
mapping; its original TARGET specification is unchanged. CI/release require all
four new input adapters, three repetitions each, with retained evidence.

Fresh native confirmation of all four input adapters now passes in the follow-up
above. Synthetic Qt native-gesture delivery is not physical trackpad qualification. PRV011 and
ZOM006–009 overlay mapping at arbitrary zoom remain open; tools still use Fit.
GAP049 is therefore not closed. Hosted CI on `21b39ac` finishes with 204 executed
UX repetitions PASS but an INCOMPLETE coverage gate, as detailed above. No overall
CI or 1.0 release PASS is inferred.

### Native transient confirmation and zoom-oracle worker — 2026-10-23

The subsequent [PhotoCard readiness control](qualification/2026-10-23/photocard-final-readiness.json)
demonstrates that identical small interactive/final pixels do not prove final
callback completion. The test now waits for the existing pending-histogram flag
to clear before pinning its preview URL. A real held final result proves the old
predicate insufficient and the new predicate rejects it; release delivers the
same current callback exactly once and changes the URL without changing pixels.
The full bounded caption workflow passes **3/3 offscreen** with unchanged
document/history/catalog/source assertions. No product code changed. This does
not attribute or close the original aggregate BUG045 failure; native and hosted
confirmation of this follow-up remain pending.

[Evidence](qualification/2026-10-23/native-preview-zoom-worker.json) records
**5/5 native PASS** on clean `bdea5bd`: SLD016/017, PRS017 and PRG007/008,
Cocoa/Metal, Retina DPR2, with no QML warnings or catalog/original-integrity
failures. The approved three-minute quiet interval is complete; Mac was released.

[Hosted CI on bdea5bd](https://github.com/yakut-sekonts/Luma/actions/runs/39932371084)
passes model preparation, numerical/source/photographic checks, full build and
unit/integration. Blocking UX returns **203 PASS / 1 FAIL**, plus 87 SKIPPED and
31 NOT_IMPLEMENTED. ZOM001 repeat 1 exceeds the unchanged five-second main-loop
watchdog; repeats 2/3 pass. Supplemental and static stages are skipped.

The failed sample contains a single LCMS frame on the GUI thread. Both passing
sibling samples identify the synchronous test-only `reference -> renderCPU ->
DisplayTransform` path; their maximum loop stalls are 2678/3573 ms. This supports
test-oracle blocking as the cause, but the failing stack itself is incomplete.
The original failure and verified artifact digest remain retained.

The independent zoom CPU oracle now runs in an async worker using immutable
fixtures/settings/profile data. Real GUI events and heartbeats continue while
waiting; timeout cancels and joins before fixture lifetime ends. Exact pixels,
ICC/geometry and watchdog limits are unchanged. All eight affected scenarios
pass **24/24 offscreen repetitions** without warnings or integrity failures.
The adversarial runner control still kills a hung process despite its forged PASS.
Formatting and affected blocking tidy checks pass; nonblocking warnings remain.

Native confirmation above precedes this worker-only harness change. The replacement
hosted run now passes ZOM001 3/3; this is not an overall release PASS. GAP049,
BUG045 and all other unqualified scopes remain open. No 1.0 publication.

### Transient preview ordering and preset pixel oracle — 2026-10-23

[Retained evidence](qualification/2026-10-23/transient-preview-preset-oracle.json)
records the complete bounded offscreen baseline: **264 PASS / 1 FAIL**, plus
427 SKIPPED and 78 NOT_IMPLEMENTED inventory entries. The complete CTest run
passes **61 tests**, with one SKIP for the absent private Nikon RAW fixture.
These counts do not qualify skipped workflows or hardware/native behavior.

The PRS017 failure reproduces in three runs with failure-only pixel capture.
Each actual UI image equals fresh actual Metal replay byte-for-byte; the expected
image equals the independent CPU replay. Exactly one RGB byte differs by one,
with exact alpha, and the linear error stays below the existing fixed budget.
Preset readiness now shares the established WB/numeric Metal rounding allowance:
RGB <= 1 only for enabled, GPU-compatible processing; CPU-only/incompatible paths
remain exact. Alpha, geometry, channel format, color space and DPR remain exact.
Negative controls reject violations, including equal bytes with different metadata.
No product pixel math or complete document/history/persistence assertion changed.

New PRG007/008 adapters verify actual delayed final delivery during Original/
Processed transitions and preset hover. Current hover completion is accepted
without saving; completion after leaving hover is rejected once by the original
revision guard. Both Original directions, final CPU pixels, histogram, saved
revision, selection, history and original hashes are checked. The UX-only probe
reuses actual results and original callbacks without blocking the render worker.
The renderer is explicitly CPU to isolate ordering; native Metal describes the
Qt scene. CI and release require both cases with three repetitions.

The final affected regression passes **42/42** bounded preset/WB/transient
repetitions, with workload-only PRS012/019 explicitly skipped. New native
confirmation is pending a fresh quiet interval; no permission is inferred from
previous completed intervals. Source/build identities are in the linked evidence.
This regression is separate from the preceding full baseline.
The independent hosted run remains a required gate; no overall CI PASS is inferred.

The source audit also confirms **LUMA-GAP-049**: requirements §16 specifies Fill,
2:1, arbitrary scale, wheel/pinch zoom and double-click 1:1/Fit, while the current
Viewer exposes only Fit/100% plus pan. Missing behavior is an implementation gap,
not merely an absent adapter. BUG045 and the remaining qualification gates stay
open. Source remains 0.9.0, stable distribution 0.5.1; no 1.0 artifact is published.

## Qualification started — 2026-10-21

The source-completion handoff is now being qualified on `codex/1.0-qualification`
(base `c375740`). Local environment: Apple Silicon arm64, 8 GiB RAM, macOS
27.0.1 (26A434), Qt 6.10.3, RelWithDebInfo, Python 3.12.15. After the human
installed Xcode 27.1 (27A9275), accepted its license and completed first launch,
both model compilations and the full application/PLCrashReporter build succeeded.
Xcode tools use `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`;
the existing C++ build still uses its recorded Command Line Tools compiler and
macOS 27.0 SDK. This is a qualification build, not the final signed artifact.

The first pass exposed a QML syntax error, a backup-helper/member name collision,
strict Objective-C++ compile errors, missing editor includes and test link
dependencies. These were fixed with the original compiler warnings enabled.
New regression coverage exercises creative settings/snapshots/rendering/Auto
Adjust, smart rule queries/revision conflicts/backup restore, and advanced import
Copy/Move with secondary backups and frozen metadata/develop settings. Existing
historical-catalog fixtures and future-preset rejection cases must represent the
current schema 21 and preset codecs 8/9 without weakening their invariants.

Intermediate logs are retained locally under `.tools/final-*`; only completed,
retested checks count as passing. This section does not close the full campaign.

### Library scroll and cancellation follow-up — 2026-10-22

[Evidence](qualification/2026-10-22/library-scroll-and-cancel.json) retains the
original failures, source/binary hashes, local report locations and retests.
These runs use the local offscreen/software backend, not the native macOS UI.

- BUG-035 now has a mandatory supplemental CI/release regression with 176 small
  PNGs. Selection, open/return, filename filtering and metadata paging preserve
  the actual visible asset and offset in three repetitions. A newly reproduced
  10 px return shift is fixed by retaining the mouse-selected viewport after
  focus transfer. Arrow-key reveal remains active. Two targeted QML mutations
  fail the expected offset/model-reset assertions.
- Adjacent Tab/Return/keypad activation passed 6/6. DSC006 passed 3/3 after its
  obsolete requirement for a Library reload was replaced with observation of
  the actual prior/busy/photoLoaded transitions. Viewport geometry, controls,
  rendered edits and durable settings remain checked in both directions.
- [CI on 406f161](https://github.com/yakut-sekonts/Luma/actions/runs/39627660731)
  passed model conversion/numerical/source/photographic checks, app build and
  unit/integration tests. Blocking UX returned 194 PASS / 1 FAIL, plus 29 SKIPPED
  and 92 NOT_IMPLEMENTED entries. These are repeat/result counts, not unique
  completed specification scenarios. The failure was TRN003 repeat 2: Cancel
  unavailable; subsequent supplemental and lint stages were skipped.
- The old TRN003 adapter passed 10/10 locally, so the CI failure was not locally
  reproduced. Its generic QObject MetaCall interception could hold an unrelated
  preview/save delivery. TRN003/TRN004 now use the existing test-only actual
  successful Auto-result gate and a calibrated 320×240 fixture. Real Cancel
  input precedes delivery; the same operation must be rejected exactly once by
  the original stale-revision guard. Both cases pass 3/3; no production Auto
  implementation or cancellation guard was changed.
- Formatting, affected-file clang-tidy (with non-blocking warnings), 40 runner
  tests and 10 quality-policy tests passed. Post-fix hosted CI is pending.

The subsequent human-authorized native interval passed **18/18** repetitions on
Cocoa/Metal with Retina DPR 2: Library scroll 3/3, Tab/Return/keypad activation
6/6, DSC006 3/3 and TRN003/TRN004 6/6. No QML warnings were recorded. All reports
retain the clean `ff2bcb0` source/build attestation and original-file integrity.
See [native evidence](qualification/2026-10-22/native-scroll-cancel.json).
The chooser remains a fixture boundary. Physical trackpad/wheel input, other
window sizes, additional filter/sort/load coverage and the remaining final
campaign stay open. Stable distribution remains 0.5.1.

### Recording, filmstrip and hosted keyboard follow-up — 2026-10-22

[Review of the supplied recording](qualification/2026-10-22/scroll-recording.json)
identifies the original surface as the horizontal Develop filmstrip, with 0.5.1
visible in the window. The earlier grid-only regression did not qualify this
surface. A new 176-image case reproduces a 24.82 px partial-card shift in current
source; compact PhotoCard now preserves `contentX` after mouse focus. A subsequent
check found Right-key index advancement without revealing the next card; explicit
Left/Right containment fixes it. The test checks the actual anchor/offset,
single activation/load, no reset, frame-synchronized positions and navigation in
both directions across the metadata-page boundary.

Final local offscreen retests: grid **3/3**, filmstrip **3/3**, keyboard **6/6** and
neighboring SEL017/SEL018/DSC006 **9/9**, with no QML warnings. A control restoring
the previous horizontal positioning fails the unchanged final-anchor assertion.
The intermediate property-only observer incorrectly counted a move-and-restore
inside one synchronous handler as visible motion; its failures are retained.
The final observer records both property notifications and nonempty positions at
scene synchronization. The subsequent authorized native run passes grid **3/3**,
filmstrip **3/3** and keyboard **6/6** on Cocoa/Metal, Retina DPR 2, with no QML
warnings and intact originals. Reports attest the same dirty worktree based on
`ff2bcb0`; this is distinct from the earlier clean-commit run. Physical trackpad
input and complete native UX qualification remain open.
See [exact evidence](qualification/2026-10-22/filmstrip-keyboard-followup.json).

[Hosted CI on ff2bcb0](https://github.com/yakut-sekonts/Luma/actions/runs/39762926272)
passes model conversion/numerical/source/photographic checks, application build,
unit/integration tests and blocking UX (**195 PASS**, 29 SKIPPED,
92 NOT_IMPLEMENTED result entries). Supplemental keyboard returns **5 PASS /
1 FAIL**; later supplemental and lint stages are skipped. Its last keypad run
retained a destroyed PhotoCard pointer even though both actual Tab directions
reached the expected UUID. The observer now resolves the current card by UUID
without setting focus or mutating the model. A declared QML control that recreates
the delegate passes with one observed instance replacement. The original failure
and artifact digest are [retained](qualification/2026-10-22/ci-ff2bcb0.json).
The post-fix hosted run remains pending; no full release gate is closed.

### Sky photographic interiors and runtime contracts — 2026-10-22

[Raw report](qualification/2026-10-22/sky-photographs.json) and
[bounded visual observations/provenance](qualification/2026-10-22/sky-followup.json)
record **3/3 source and 3/3 native photographic cases PASS**. Conservative regions
were annotated before prediction on the existing licensed day/night/indoor PNGs.
The indoor portrait has no mask pixels at or above 0.5; day/night sky interiors
pass the fixed confidence/coverage criteria and annotated river, rocks and tower
interiors pass the background limits. Unannotated pixels remain unknown.

The application's real CoreMLSky provider is compared with independent PyTorch
resize plus the pinned source network. Maximum source/native difference is
0.001969, below 0.0005 plus the unavoidable half-step of the provider's uint8 mask
(0.5/255). This does not loosen the separate Float32 Core ML numerical gate.
Native progress/input preservation, unsupported selections, malformed input,
pre-cancel, cancellation after resize and exact replay after cancellation pass.
Seven oracle controls reject constant/inverted masks, concealed false positives,
weak confidence and malformed annotations/probability planes.

CI and release packaging now require this check and preserve its report, native
logs and comparison sheets. Initial local build, affected clang-tidy, formatting
and workflow YAML parsing pass; independent hosted execution is pending.
No production Sky code, weights or conversion recipe changed. The three sheets
were inspected, with soft/coarse fine edges and missed gaps in foliage recorded.
Dense boundary accuracy, RAW and broader scene coverage, crop/orientation mapping,
CPU fallback, actual accelerator placement and resource limits remain open. The
full 1.0 gate remains **NOT QUALIFIED**.

### Bounded functional fixes and GitHub distribution — 2026-10-23

[Retained evidence](qualification/2026-10-23/functional-release-fixes.json)
records the original local functional baseline (**240 PASS / 16 FAIL / 188
SKIPPED / 326 NOT_IMPLEMENTED**), both intermediate failing retests and **48/48
passing repetitions** of those sixteen scenarios after fixes. These are local
offscreen/software results, despite the backend selector recording
`hosted_ci: true`; no new native-window interval is included.

Add without a backup now accepts legal existing long filenames. Generated
Copy/Move/backup names retain their validation, and file-open errors identify the
actual failed path. Directory notifications refresh only cached Missing roles
asynchronously: they no longer reset the Library, destroy delegates or move its
viewport. A real file removal/restoration test checks both role changes, zero
resets, selection, documents and source integrity. Import, Library and new Metal
unit tests pass 3/3; the shipping QML smoke test and probe-symbol isolation pass.

The Metal HSL calculation now avoids cancellation near black/white. Seven actual
Metal cases meet the unchanged linear error budget and final one-step RGBA8
budget, with exact alpha. Combined curves/HSL on HDR still exceeds that budget
and is explicitly routed to the unchanged CPU implementation; two fallback
cases preserve exact CPU pixels. This is a correctness/performance tradeoff,
not a claim that the combined Metal path is numerically fixed. Original failed
numeric experiments and unchanged exact UX pixel assertions are retained.

UX fixtures now use the current retouch-aware cache identity, destination layout
and twelve preset groups. Cancellation holds the actual successful load result;
export waits on an actual pending WAL write; crop input acquires the real corner
grip. Per-field/card, model-reset and failed pixel evidence remains enabled.
Affected static analysis completed with nonblocking warnings; formatting and
workflow parsing pass. The 100k Library contract passed separately, but was run
alongside UX and supplies only functional bounds, not performance acceptance.

[Hosted CI on 1bcec07](https://github.com/yakut-sekonts/Luma/actions/runs/39774239377)
passes model and application/unit stages, blocking UX, keyboard/scroll/dialog
and gradient-handle regressions. PhotoCard localization fails 3/3 on the obsolete
cache identity; subsequent stages are skipped. After that correction a local
run reports 2 PASS / 1 FAIL at a separate aggregate state invariant. Failure-only
diagnostics were added; subsequent runs pass 10/10 and 20/20 without reproducing
it. This intermittent observation remains open; those passes do not explain it.

The owner defines the final release as GitHub open-source publication. Packaging
and publisher tests accept truthful ad-hoc, non-notarized 1.0 artifacts while
requiring Sparkle signatures, bundled models and verified asset identities.
Developer ID/notarization are optional. Full QA, installation and update
qualification remain mandatory; stable distribution is still 0.5.1.

### Initial qualification baseline — 2026-10-21

[Machine-readable baseline](qualification/2026-10-21/baseline.json) records the
source commit, per-case outcomes and hashes of retained local logs.

- 54 background CTest cases passed (`final-offscreen-tests-1.xml`), including
  the full app's ordinary-image, import, edit/export/restart and QML workflows.
- Three native CTest cases passed in a human-approved uninterrupted interval:
  eight-language UI, complete masks and local gradients, including restart/export
  checks (`final-native-tests-1.xml`, 150 seconds). This is not the complete UX spec.
- The private Nikon RAW case was SKIPPED without its configured fixture; it is
  not counted among the 57 passing cases.
- New coverage checks creative controls, smart collections, advanced import and
  deterministic retouch donor selection. Historical fixtures now construct
  coherent old catalogs before migrating to schema 21; version-rejection tests
  distinguish valid mask codec 3 / preset codecs 8–9 from future codecs.
- Fixed QML errors in catalog maintenance, SmartRuleEditor conditions and wrapped
  confirmation dialogs. Thumbnail tests wait for the requested asset's loaded
  metadata page; an empty asynchronous placeholder no longer counts as invalidation.
- Both `.mlmodelc` resources were compiled, inventoried and installed in Luma.app
  and the UX bundle. Their manifest remains `qualification: NOT_RUN`.
- Native CPU numerical comparison against the adapted source networks is **FAIL**:
  Sky passed all three synthetic inputs (max absolute error 2.39e-7); Denoise
  failed all three (max error 0.2002, RMSE up to 0.00486). Tolerances remain
  max error 0.0005 / RMSE 0.00005. HDR/negative-pixel passthrough passed.
  See [retained numerical evidence](qualification/2026-10-21/model-numerics.json)
  and LUMA-BUG-036. No model-quality gate is closed.
- The first Python prediction probe crashed in `libcoremlpython` / `_PyObject_Free`
  on the Core ML reset queue. Its stack matches the upstream NumPy-owner lifetime
  bug described in [coremltools #2829](https://github.com/apple/coremltools/pull/2829).
  The retained numerical probe uses a separate native process with Core ML-owned
  tensors; it does not patch the pinned converter or hide a crashed process as PASS.
- Formatting passed. The full clang-tidy pass covered 251 unique translation
  units and exited 1 with 13 blocking diagnostics in 10 files. All affected files
  were fixed and passed targeted retests, along with the added native model probe
  and changed regression tests. The original failing log is retained; this is a
  full initial pass plus affected-file retests, not a second full clean run.
  Non-blocking analyzer warnings remain.
- After the merge worker ownership change, the application rebuilt and its
  affected QML shell, batch/history and preset regressions passed (3/3).

Formatting-only changes were separated in `e18a2d6`; functional fixes and evidence
follow separately. The baseline above does not cover all new 0.6–0.9 workflows,
the 23 legacy gaps, photographic AI quality, load/stress or signed distribution.

### Denoise follow-up — 2026-10-21

Adaptation v2 fixes the independently reproduced native downsampling error using
checkpoint-equivalent spatial slices/1×1 convolution. It also preserves exactly
uniform RGB tiles to prevent the source model from inventing structure on them.
Five adaptation tests passed (algebra, channel/spatial order, uniform preservation,
nonuniform processing and HDR/negative-pixel preservation); the four toolchain
preflight tests and native-probe clang-tidy also passed. The application rebuilt
with v2 installed and verified in both bundles. Six affected packaging, update
and offscreen smoke CTest regressions passed; they overlap the earlier baseline.
See [follow-up evidence](qualification/2026-10-21/model-followup.json).

The expanded comparison remains **FAIL**: [CPU 9/11](qualification/2026-10-21/model-numerics-v2-cpu.json)
and [ALL 10/11](qualification/2026-10-21/model-numerics-v2-all.json). Dark nonuniform
inputs exceed unchanged limits. Source-only checks also reproduce bright
artifacts on dark inputs, recorded separately as LUMA-BUG-037. LUMA-BUG-036 is
not closed. No photographic, installation, update or whole-campaign gate is
waived by the numerical improvement. Historical v1 reports remain unchanged.

### Source-quality gate and CI compatibility follow-up — 2026-10-21

The independent macOS 15.7.9 / Xcode 16.4 [v2 CI report](qualification/2026-10-21/model-numerics-ci-v2.json)
also passes 9/11 numerical cases and fails dark noise/impulse. Source quality now
has an independent mandatory CI/release gate against analytical clean images:
[5/7 inputs pass, overall FAIL](qualification/2026-10-21/denoise-quality-v2.json).
Both dark failures extend into the runtime's interior crop. Seven oracle tests
pass, including rejection of identity, bright, over-smoothed, invalid and isolated
amplified outputs. This gate does not replace photographic qualification and
does not modify the current model or relax numerical limits.

CI additionally exposed unsupported `std::jthread` / `std::stop_token` usage in
the diagnostic writer on Xcode 16.4. A mutex-protected stop flag and joined
`std::thread` preserve queue draining without that library dependency. The app
and new real-writer lifecycle test build locally; the test passes. Affected
clang-tidy passes its blocking checks, with existing nonblocking warnings.
Xcode 16.4 validation must still come from the next CI run. No new native UI
session, signed installer or release was exercised in this follow-up.

### DRUNet v3 and current UX fixtures — 2026-10-21

The source-quality failures persisted in an exploratory Baseline SIDD model, so
it was not adopted. Bundle v3 instead uses the author's hash-pinned MIT DRUNet
color checkpoint, equivalent down/up-sampling and a per-tile Haar noise estimate.
Original weight bytes are unchanged. The same analytical-clean gate now passes
[7/7 source cases](qualification/2026-10-21/denoise-quality-v3.json), including
the two dark failures. The native outputs also pass that quality oracle and
source comparison on [CPU 11/11](qualification/2026-10-21/model-numerics-v3-cpu.json)
and [ALL 11/11](qualification/2026-10-21/model-numerics-v3-all.json). Maximum
errors are 1.44e-6 and 3.46e-6 respectively, below the unchanged 0.0005 limit.
This resolves the exact BUG-036/037 reproductions for the replacement model;
photographic/noise/texture/tile acceptance remains open. Eleven adaptation tests
and seven independent quality-oracle tests pass. Both application bundles contain
the same verified v3 manifest; eight affected CTest regressions pass.

[CI on 5ef81d3](https://github.com/yakut-sekonts/Luma/actions/runs/39616268298)
built successfully on Xcode 16.4: 58 CTest cases passed and the private Nikon
fixture was skipped. This closes the reported BUG-038 build failure. Its UX
step still failed (15 PASS, 180 FAIL, 29 SKIPPED, 92 NOT_IMPLEMENTED), often because
chooser fixtures wrote retired LibraryPane properties. Tests now assign the
actual ImportDialog, use all 12 preset groups through visible UI clicks, and
scroll to the recapture control before clicking. The real DiagnosticsController
is included in the UX context with logging/capture disabled like unattended
production startup; the rerun has no QML warnings.

Four targeted import/selection/export cases passed in offscreen/software. A
dependent eight-case run passed seven, then the recapture scrolling correction
passed its single affected retest: twelve distinct cases have passing results,
not a full suite pass. Two UX bundle/isolation CTest checks also pass. Formatting
and affected clang-tidy pass blocking checks; existing nonblocking warnings remain.
The local offscreen runs explicitly used the existing `GITHUB_ACTIONS=1` backend
route; their `hosted_ci` metadata therefore does not mean they ran on GitHub.
An earlier broad CTest invocation was stopped after it included native UI tests;
its partial results are not used as additional qualified native coverage.
See [follow-up record](qualification/2026-10-21/drunet-ux-followup.json).

The new commits still need independent CI. Full new-workflow/native UX,
photographic model quality, resource/load/recovery and signed distribution gates
remain open. Stable remains 0.5.1; there is no new installer, tag or appcast.

### Photographic Denoise and runtime tiling — 2026-10-21

The first photographic campaign exposed LUMA-BUG-041: bundle v3 passes only
four of six noisy photo cases despite nine passing source/native comparisons.
With weak added noise, its mean-based Haar estimate over-smooths the portrait
and landscape; channel errors and landscape seam/gradient errors increase.
[The failure report](qualification/2026-10-21/denoise-photographs-v3-failure.json)
is preserved. Bundle v4 replaces that mean with a lower absolute median,
calibrated to Gaussian sigma and exported as topk/amax. The checkpoint, halo,
color conversion and acceptance limits are unchanged.

The corrected actual-runtime campaign passes [all six noisy photo cases and
nine source/native tiled comparisons](qualification/2026-10-21/denoise-photographs-v4.json).
The three unchanged photographs are observational controls, not certified
noise-free sensor references or extra quality passes. At Amount 1, each RGB
channel improves in both linear and sRGB MSE, and gradient errors improve along
both axes and actual tile seams. Maximum source/native error is 3.82e-6 against
the fixed 0.0005 limit. Native contracts also pass Amount 0 identity, Amount 0.5
linear blend, alpha/input preservation, tile progress, pre-/mid-loop cancellation
and invalid-amount rejection. Neither ALL nor these checks establish actual
accelerator placement or CPU-fallback behavior.

The synthetic gates remain [7/7 source](qualification/2026-10-21/denoise-quality-v4.json),
[11/11 CPU](qualification/2026-10-21/model-numerics-v4-cpu.json) and
[11/11 ALL](qualification/2026-10-21/model-numerics-v4-all.json). Thirteen adaptation
tests and 13 independent quality-oracle tests pass. The application build,
model installation/manifest verification in both bundles, eight affected CTest
regressions, formatting and native-probe clang-tidy pass. The photographic gate
is required in CI and release packaging, with reports and comparison sheets
retained as artifacts. [Evidence and visual-review limits](qualification/2026-10-21/denoise-photographic-followup.json)
record the exact model/probe identities, including an initial run superseded
after the probe rebuilt; only the fresh run with a stable probe is accepted.

This is bounded qualification on three small developed PNGs with deterministic
added Gaussian noise. Real camera noise, full-resolution RAW texture/color,
Sky scene quality, native UX, resource/load/recovery and signed distribution
remain open. No new native UI session or release was performed.

## Source decisions and qualification requirements

### Preview completion ordering and photo-switch zoom — 2026-10-23

P06-PRG-004/009 and P06-ZOM-005 pass **9/9** local offscreen repetitions and
**3/3** in the authorized two-minute Cocoa/Metal Retina DPR2 interval. The
existing three zoom/export cases also pass again. No QML warnings, original-file
or catalog integrity failures occurred. There is no new production behavior fix
in this batch: the existing revision guard and photo-load reset policy pass.

The UX-only probe retains the actual successful render dispatch after its final
cancellation check, freeing the sole worker for a newer render. Its original GUI
callback is later delivered and the existing revision guard rejects it exactly
once. PRG004 exercises delivery after a newer final image. PRG009 additionally
holds the next actual SessionService load and proves old-stage rejection while
the editor is still busy, before the new photo's success callback. Image/URL,
histogram, document, selection, saved revision and Undo/Redo remain correct.
Zoom switching exercises actual 100% scene pixels and pointer pan, then verifies
Fit and zero offsets on each filmstrip load while preserving saved edits.

All targets, shipping QML/probe-isolation, formatting, 40 runner and 10 policy
checks pass; blocking affected-file tidy passes with optional-access warnings.
The new probe is absent from the shipping binary. CI and release workflows now
run the preview-ordering and zoom-switch regressions and retain their evidence.
These checks explicitly use CPU image rendering; native Metal identifies the Qt
scene backend. They do not qualify GPU-command races, RAW, physical input,
external displays, latency or memory pressure. The native interval has ended.
[Evidence](qualification/2026-10-23/preview-ordering-zoom-switch-followup.json).

### Full-resolution zoom and progressive export — 2026-10-23

New P06-ZOM-001/002 adapters reproduced **2/2 failures**: Fit to 100% left
`contentX/contentY` at zero, showing the top-left corner instead of the center.
The QML callback ran before its content-size bindings settled. BUG-048 now defers
centering until those bindings settle and snaps the offset to backing pixels.
Both even and odd window dimensions are exercised.

P06-ZOM-001/002 and P06-PRG-002 pass **9/9** final local offscreen/software
repetitions. A separately hashed 3072×2048 generated chart distinguishes the
1024 interaction, 2560 Fit and full-resolution paths. The held Exposure gesture
actually displays 1024 output before release. The full provider image matches
an independent CPU document; the composited 100% scene preserves single-pixel
detail. PNG export retains all 3072×2048 pixels, declares sRGB and equals the
full-source CPU reference exactly. Saved revision/document, selection, identity,
original bytes and one-gesture Undo/Redo are preserved. No QML warnings occurred.

All targets, QML shipping smoke, test-probe isolation, 40 runner and 10 policy
checks pass. Formatting and blocking tidy checks pass; optional-access warnings
remain behind the test's throwing `require()` guard. The subsequently approved
two-minute interval passes **3/3** on actual Cocoa/Metal, Retina DPR2, using clean
`6056dd2` with matching build/source provenance. No QML warnings, catalog or
original-file integrity failures occurred. These generated PNG checks
do not qualify RAW, physical input/panning, OS pressure, latency or GPU failures.
The independent CPU oracle runs on the test GUI thread, so heartbeat stalls in
these reports are not product performance measurements.
[Evidence](qualification/2026-10-23/zoom-progressive-followup.json).

The accelerator interface also passes deterministic fault contracts: unavailable
output falls back to exact CPU RGBA/geometry, every histogram field and clipping
overlay for neutral and Noise/Sharpen/Crop/Exposure documents. Eight malformed
buffer, exception and cancellation cases reject without returning partial pixels;
subsequent renders recover and source RGB/alpha remain unchanged. The same CTest
run passes the existing 13 actual Metal numerical cases and two explicit
Curve/HSL CPU fallbacks at unchanged tolerances. These injected interface outcomes
do not qualify native command-buffer, device, driver or allocation failures.
[Evidence](qualification/2026-10-23/develop-fallback-followup.json).

### Initial phase 0.6 contracts and native confirmation — 2026-10-23

The subsequent approved three-minute interval passed **4/4** on Cocoa/Metal,
DPR 2: SLD016/017, ADV009 and the new P06-GPU-001 Settings workflow. No QML
warnings, original changes or source/provenance drift were observed. This closes
the bounded native follow-up for the WB/targeted fixes described below, without
claiming complete native QA. [Evidence](qualification/2026-10-23/phase6-native-preview-followup.json).

P06-GPU-001 also passes **3/3** offscreen. It toggles the real Metal checkbox in
both directions and checks a newly rendered preview against its CPU fixture,
complete edits, saved revision/asset identity, membership/selection, preference
persistence and one-step Undo/Redo. This is a UI/state-preservation contract;
actual GPU execution and failure injection require separate evidence.

New `preview-cache-contract` coverage passes LRU eviction, byte accounting,
immutable active ownership across replacement/purge, stale generation rejection,
zero-budget purge/resume, invalid replacements and SIZE_MAX arithmetic without
large allocations. It tests the cache directly, not actual OS memory pressure,
overall RSS or multi-hour load. All-target build, shipping smoke/probe isolation,
40 runner tests, 10 workload-policy tests, formatting and blocking tidy checks
pass; non-blocking optional-access warnings remain in the UI test.

The capability manifest now reflects source availability for 0.6–0.9. Missing
adapters become explicit SKIPPED/unadapted qualification gaps rather than future
NOT_IMPLEMENTED capabilities. No unexecuted case becomes PASS. That checkpoint
had one official phase 0.6 adapter of 58; the zoom/export follow-up above adds
three; the completion-ordering/photo-switch follow-up adds another three, for
seven at that checkpoint; PRG007/008 above bring the total to nine. Phases 0.7/0.8/0.9 still require adapters for
their 56/50/84 inventory cases, with applicability review for conditional tools.
Existing unit/model reports remain separate evidence. Release qualification is
still open, including load, hardware, installation and updater checks.

The actual `SessionService` integration additionally passes cold/warm PNG decode,
distinct standard/interactive resolutions, fresh saved edits on cache hits,
byte-limited eviction with exact regenerated pixels/alpha, full-resolution cache
reuse, zero-budget operation/resume and ownership after purge. A purge during
the real decoder's progress callback prevents late insertion into the old
generation. Cancelled reads preserve catalog state; a populated full-resolution
cache still refuses an explicitly changed test-owned source checksum. Both
cache CTest targets pass, as do formatting and blocking tidy checks. These small
isolated fixtures do not qualify OS memory pressure, RAW quality or process RSS.
[Evidence](qualification/2026-10-23/session-cache-followup.json).

The CI job budget is now 90 minutes: the prior run reached PhotoCard at about
59 minutes before failure skipped later UX/lint steps. Individual scenario
deadlines, numeric budgets and release gates are unchanged; release packaging
already has its separate 120-minute budget.

### WB, targeted gestures and native follow-up — 2026-10-23

[Evidence](qualification/2026-10-23/wb-targeted-native-followup.json) retains the
clean `41601b5` full functional run: **254 PASS / 2 FAIL**, plus 188 SKIPPED and
326 NOT_IMPLEMENTED result entries. Unit/integration checks return **56 PASS /
1 SKIP** (the absent private Nikon fixture). These counts do not close full QA.

The approved five-minute native interval passed **8/8** affected import, thumbnail,
Crop, preset and race scenarios plus **3/3** eight-language PhotoCard runs on
Cocoa/Metal, DPR 2. No QML warnings occurred and original hashes stayed intact.
BUG-045 remains open: passing repeats do not explain its original intermittent
state-invariant failure. This interval predates the fixes below.

The two functional failures are SLD016/017: actual preview differences from the
independent CPU reference are one RGB step in 48 and one channels respectively.
Actual Metal replay confirms the same differences within the existing linear
budget. The WB/numeric UI oracle now explicitly allows RGB error <= 1 with exact
alpha for Metal-enabled compatible documents; CPU-only/incompatible documents
remain exact. This changes the obsolete bit-exact UI expectation, not the
pre-existing renderer budget. Negative controls reject two-step RGB errors,
altered alpha, wrong dimensions and missing images. Document, history, durable
state and ready/source checks remain unchanged. Six extreme WB cases extend the
native oracle to 13 Metal cases and two exact CPU fallbacks, all passing.

ADV009 previously passed with four QML binding-loop warnings. Ending a targeted
gesture during photo loading was followed by MouseArea cancellation, re-entering
`endEdit`/`stateChanged`. `endTargeted` now does nothing after its gesture ended;
tests also verify that stale cancellation cannot split an unrelated edit's Undo.
The scenario now rejects binding loops. SLD015/016/017, ADV009/015 and RACE006
pass **18/18** offscreen repeats without QML warnings. ADV003 was explicitly
skipped by the quality-only workload policy, not passed. Eight affected CTest
checks, all-target build, formatting, shipping-probe isolation and affected-file
clang-tidy pass; non-blocking tidy warnings remain. New native coverage, hosted
CI, later-phase qualification, load and final distribution checks remain open.

- Denoise/Sky delivery is resolved: include both models in the installer and
  Sparkle archive. Pinned DRUNet color and U²-Net Sky sources/licenses, conversion,
  default model lookup and packaging guards are implemented in source. The
  compiled models now exist, but the installer and model qualification remain
  open. See [model delivery](BUNDLED_MODELS.md).
- Qualify the current bounded HDR/panorama algorithms. Further seam optimization,
  projection coverage or tiled processing depends on the photographic/resource
  evidence; do not advertise unimplemented 360° or generative panorama fill.
- XMP interoperability is implemented: ordered creators, exact custom label
  text, standard-color mapping, Reject as Rating -1 and catalog-only Pick.
  Fractional ratings are refused visibly, not rounded. Migration 17 and all
  import/edit/sidecar/export paths require final qualification; see
  [metadata semantics](PHASE9_DEVELOPMENT.md#metadata-and-xmp).
- Local rotating logs and PLCrashReporter capture are implemented in source;
  no capture/rotation/formatting execution has taken place. The dependency is
  pinned, its license travels with the bundle and reports remain local. See
  [diagnostics design and limits](PHASE9_DEVELOPMENT.md#local-logs-and-crash-reports).
- Named processing versions now have a source implementation (schema 18):
  full-document storage, guarded CRUD, one-step Undo on restore and eight-language
  UI. Qualify round trips, stale revisions/quotas, corruption/failure behavior,
  autosave/close/update coordination, migration and backups. See
  [version behavior](PHASE9_DEVELOPMENT.md#named-processing-versions).
- The §33 History browser now has a source implementation: chronological states,
  arbitrary navigation, guarded clear and named-version creation from any retained
  state. History stays session-local (200 steps); the UI states its lifetime.
  Qualify branch/gesture semantics, stale handles/confirmation, snapshot fidelity,
  keyboard/scroll behavior and autosave/close coordination. Persistent history is
  not implemented. See [History behavior](PHASE9_DEVELOPMENT.md#history-browser).
- The §17 Histogram and §18 Option clipping previews now have source workflows:
  bounded RGB/luminance sampling before display ICC, alpha-weighted counts,
  output-referred clipping overlays and five tone controls with gesture Undo.
  Qualify numerical/visual correctness, stale-render/cache behavior, progressive
  and 1:1 updates, keyboard/Option/focus handling, UI size/languages and cost on
  supported hardware. These are preview estimates, not RAW sensor saturation or
  an export-profile soft proof. See [Histogram behavior](PHASE9_DEVELOPMENT.md#histogram-and-clipping).
- Requirement §18 Auto WB / neutral eyedropper now have source implementations.
  Qualify gray-world scene bias and the shared Temperature/Tint solver against
  independent photographic/numerical references; cover camera calibration,
  alpha/clipping/geometry, source-versus-display semantics, one-step Undo,
  persistence/export, stale-result/cancel/shutdown handling and native UI in all
  languages. The result is Custom WB, not a live Auto mode. See
  [WB behavior and limits](PHASE9_DEVELOPMENT.md#auto-white-balance-and-neutral-eyedropper).

## Eight functional source blocks completed

Presence, Point Color, Color Grading, Post Crop Vignette/Grain, Smart Collections,
Compare, Auto Adjust and advanced import now have source implementations. See
[the ledger](FUNCTIONAL_COMPLETION.md) and [ADR 0030](adr/0030-functional-completion.md).
Their completion does not close any qualification gate. In the final campaign:

- Build all targets and reconcile fixtures with catalog 21, preset codecs 8/9,
  snapshot codec 2, the expanded import dialog and new Library/QML dependencies.
- Qualify zero-setting equivalence, texture/detail halos, haze/color shifts,
  grayscale/transparent/HDR inputs, grading overlap, output-space vignette/grain,
  preview/full-resolution/export agreement and deterministic rendering.
- Cover old/new catalog/preset/snapshot round trips, migration backup/restore,
  creative batch conflicts/Undo, history, cancellation and stale picker/Auto results.
- Cover nested rule semantics, keyword inequality, escaped searches, empty results,
  metadata refresh, rename races, limits, all eight languages and accessibility.
- Cover Compare Fit/100%, different aspect ratios, linked center/zoom, swap,
  unsaved edits, missing/changed sources, close/reopen and memory-pressure behavior.
- Fault-inject advanced import at every journal/copy/backup/commit/Move boundary;
  cover date fallbacks, filename collisions, symlinks, changed sources, full disks,
  metadata/develop snapshots, duplicates, cancellation, retained originals and
  recovery after rename/unlink. Verify both image backups and catalog invariants.

## Final campaign

Use a new candidate branch; the implementation branch intentionally skips CI.
Build all application, CLI and test targets first and fix integration/compiler
issues. Run formatting/static analysis then the existing tests and extended
coverage for every new workflow in phases 0.6–0.9. Preserve historical failure
evidence, update obsolete fixture assumptions explicitly and never reinterpret
SKIP/NOT_RUN/blocked cases as passing.

Build the offline models before configuring the final app. Qualify the pinned
Python/PyTorch/ONNX/Core ML conversion path, source-to-Core-ML numerical agreement,
model tensor contracts and toolchain compatibility. Bundle v4 now passes the
synthetic source-quality, native CPU/ALL and bounded photographic/runtime checks;
cross-platform and complete photographic/runtime qualification remain open.
Use representative photographs to assess denoise texture/color/highlights/tiles
and Sky boundaries/no-sky errors, then CPU fallback, cancellation and 8/16 GB
resource use. Exercise first launch without network, installation in Applications,
app moves, custom override/reset, upgrade/relaunch and both models in the final
signed ZIP/DMG. Do not treat checksum/provenance checks as model-quality evidence.

The complete campaign must include requirements §100, all 23 open 0.5.1
qualification gaps, LUMA-BUG-035, image/CPU–Metal comparisons, trained-model
quality, export/ICC/metadata, migration/rollback/downgrade, fault-injected recovery,
native dialogs/keyboard/focus/sleep/wake, all eight languages, 8/16 GB Apple
Silicon resource tests and 10k/100k/500k catalog/load/stress cases. Record exact
source/build identity, machine/OS, fixtures and per-case evidence. Fix failures
and repeat the affected regression coverage before final acceptance.

Startup recovery and local Diagnostics now have source implementations. Include
missing/locked/corrupt catalogs, interrupted-session detection, worker shutdown,
restore-without-replacement and diagnostic-report privacy in this campaign.
Qualify the separate local crash reporter, signal/exception capture, bounded
binary/text reports, queue overflow, rotation/retention, inactive-session locks,
multiple processes, debugger/acceptance opt-outs and disk/permission failures.
Preserve matching final application/library dSYMs before stripping or removing
build objects and demonstrate offline symbolication against their Mach-O UUIDs.
Crash reports contain binary paths/process details; never automatically append
them to the privacy-limited diagnostic JSON or upload them.

## Distribution

User decision, 2026-10-22: Apple Developer Program is unavailable. A full release
means open-source publication on GitHub, with standalone DMG/ZIP and the signed
Sparkle update channel. Developer ID/notarization are now optional and are no
longer release blockers. This supersedes the previous signing requirement only;
it does not waive the final functional, image, UX, recovery or load qualification.
See [the current distribution policy](OPEN_SOURCE_RELEASE.md).

The existing release workflows and Sparkle signing checks remain in place.
The 0.5.1 limited-release exception does not authorize a 1.0 exception. Final
distribution needs standalone Applications installation/launch, DMG/ZIP checksums, updater signature
verification, bell notification, upgrade/relaunch and data preservation. Apple
Developer signing credentials are neither required nor configured for this release.
The [Developer ID packaging path](DEVELOPER_ID_RELEASE.md) is now implemented
in source, including isolated Actions credential setup and accepted/stapled
app/DMG tickets. This optional path is unexecuted. The selected ad-hoc path must
retain explicit `notarized: false` provenance and be tested with a quarantined
download using the documented per-app macOS approval; no global security change.

The available local Mac has 8 GiB RAM. The user has no 16 GiB Mac and can connect
an external monitor. Neither the 16 GiB configuration nor external-display checks
are marked passed by that availability statement.

Publish only after all required results are documented and accepted. Keep the
post-1.0 Impeccable/Taste frontend redesign outside this qualification scope.
