# Current-scope UX qualification

Current execution policy, 2026-10-20: the user subsequently deferred **all**
intermediate QA, test runs and static checks until the final 1.0.0 campaign.
The records below are historical; they do not qualify new development.
New user-reported defects are recorded in
[the development backlog](../DEVELOPMENT_BACKLOG.md).

Status: **full qualification incomplete; 0.5.1 publication authorized after CI
with explicitly accepted coverage gaps**.
Work is tracked in [PR #47](https://github.com/yakut-sekonts/Luma/pull/47),
linked to [Issue #46](https://github.com/yakut-sekonts/Luma/issues/46);
remaining qualification continues in [Issue #48](https://github.com/yakut-sekonts/Luma/issues/48).
Source/release scope is 0.5.1. The latest GitHub Release remains authoritative
for publication status.

User direction, 2026-10-20: "Выпустить 0.5.1 после CI с указанными ограничениями".
This explicitly permits this release with the 23 remaining functional contracts
listed in [the release qualification record](../releases/v0.5.1-quality.md).
It supersedes the previous requirement to finish every current functional check
before publishing 0.5.1. It does not turn missing/failed checks into PASS, waive
CI or artifact verification, complete the remaining qualification, qualify the entire phase, or
authorize later phases to bypass their quality checks. Historical checkpoints
below retain their original publication restrictions and evidence.

User direction, 2026-10-19: run functional quality checks only. Stress, soak,
chaos and performance workloads are reserved for the pre-release campaign of 1.0.0.
Historical load-test evidence stays historical; deferred work does not count as PASS.
Current runs use explicit functional cases, excluding large-catalog and repeated
stress fixtures even when their inventory mode is REGRESSION.

The execution policy classifies 369 compiled native adapters: 257 reviewed bounded
adapters and 112 deferred implemented workloads, plus the independent integrity
adapter. These are separate from the 51 deferred checks among unadapted scenarios.
LIB009 is included in those deferrals because its unchanged REGRESSION contract
requires 50 resize cycles; this deferral adds no adapter or PASS.
The policy preserves inventory modes and marks deferral SKIPPED. New adapters
require workload review. Default CTest uses 256 Library metadata rows; the original
100k contract remains opt-in with `LUMA_ENABLE_SCALE_CONTRACT_TESTS=OFF` by default.

## Verified checkpoint, 2026-10-20

- 369 native scenario adapters and one suite-wide original-integrity adapter.
  Of 445 scenarios through the current phase, conditional mask duplication
  (MSK-005) is absent. Of the 74 remaining scenarios without executable adapters,
  23 are functional regressions and 51 are deferred load/performance/chaos checks.
  Prepared, uncompiled adapters are not counted.
- 34 production defects are reproduced and fixed with focused native/software
  evidence in [UX_DEFECTS.md](UX_DEFECTS.md). Raw original failures remain retained.
  Three outcomes from a native mask run with confirmed external interaction
  remain individually unattributed. Their FAIL records and subsequent controlled
  diagnostic PASS are retained separately; full release qualification is incomplete.
- 35 additional adapters cover 500-edit/history and 1,000-gesture stress,
  preset CRUD/round trips/restarts, queued preview races, retained decoded-image
  recovery, partial import failures, 50 cancel/reimport cycles, update/batch
  isolation and current performance measurements. The three new production fixes
  address preset hover ownership, double-tap slider reset and denied import children.
- A further 13 adapters cover WB semantics/restart, 80 numeric-label checks,
  wheel after slider release, missing startup source, fullscreen/scroll retention,
  repeated export Cancel, protected path aliases, immutable queued edits,
  failed autosave/Retry and real scrolling during a 1,000-item export. The new
  formatting fix preserves near-zero edits while removing displayed negative zero.
  Ten cases passed three times in `window-wb-export-final-report`; its three
  numeric fixture failures remain retained. The corrected numeric fixture passed
  three times in `numeric-scroll-settled-report`, and the two native fullscreen
  cases each passed 50 final-source round trips in `fullscreen-fixed-final-report`.
  This resolves **35 Metal results across 13 cases**; provenance, source/binary
  consistency, original SHA and catalog integrity passed for each evidence run.
- Seven more adapters cover deep scroll after WB relayout, photo/language changes,
  actual Tab reachability, held Slider photo/Library transitions and Optics-only
  Copy/Paste/presets. The new fix scrolls focused adjustments into view without
  changing manual scroll or edit history. These and twelve affected existing
  scenarios passed three times: **57 Metal PASS**, `gesture-import-final-report`,
  run `20261017T123506Z-16828`, with provenance, source consistency, original SHA
  and catalog integrity all passing.
- Eleven more adapters cover 255-character filenames, 1,000-character paths,
  large import/export reports, status/layout stability, closed-popup hitboxes,
  disabled actions, actual ToolTips, dynamic scrollbar geometry and localized
  failed-save/close recovery. EXP-011 explicitly reuses the complete VIS-004
  real 100-file failure/readability contract. Manual screenshot review exposed a
  missing sidebar-action oracle; three native failures reproduced the overflow,
  and three more established the intermediate scrollbar/text overlap.
  The complete sidebar now scrolls within clipped bounds and reserves its bar
  gutter, retaining full filename text and real pointer/Tab/Space access to Copy.
  Final evidence: **12 Metal PASS**, `visual-driver-final-report`, run
  `20261017T131309Z-27651`, plus **6 Metal PASS** for VIS-001/DSC-001 three times,
  `sidebar-driver-native-final-report`, run `20261017T132106Z-30385`.
  Nine affected keyboard/preset/layout cases also passed three times before the
  final gutter hardening (`sidebar-focused-final-report`, 27 Metal results).
  Every report passed provenance, source consistency, original SHA and SQLite.
- The hosted auxiliary-window timeout exposed a test-driver ownership error:
  requesting a probe-window update waited on the main-window polish counter.
  Frame acknowledgment now observes the requested window itself. Besides the
  Cocoa repeats, **10 local offscreen/software repeats passed**,
  `driver-frame-offscreen-final-report`, run `20261017T132123Z-27569`.
  This is a local CI-environment simulation, not a successful hosted run.
- Six further Library adapters cover 4↔5-column reflow around the same logical
  asset, filter changes during an actual drag, cleared/empty result ranges,
  ungated physical import progress during scrolling and held Select all completion
  overlapping durable insertion. The import/reload regression reproduced a real
  viewport jump three times. The fix resolves the same visible asset in the new
  filtered/sorted result on the metadata worker and restores its bounded offset,
  while filters, superseded reloads and user input invalidate old restoration.
  Final evidence: **18 Metal PASS**, `library-viewport-final-report`, run
  `20261017T174218Z-45300`, plus **39 affected Metal PASS**,
  `library-viewport-affected-report`, run `20261017T174723Z-45228`.
  Each import/scroll repeat executes separate 2,048-source insertion waves before
  and after the old assets, then an actual import reload while a scrollbar is
  held. All three observed that real held input suppresses restoration and that
  post-release scrolling works. Provenance, source consistency, original SHA and
  SQLite integrity passed for every result.
- Eight further adapters cover repeated batch Cancel, editor/batch history
  arbitration, 100 full Apply/Undo cycles over 100 assets, idle update popup,
  update-check failure, notification during a held Slider and simultaneous
  save/export close with overlapping genuine closeReady delivery. Final evidence:
  **24 Metal PASS**, `batch-update-final-report`, run `20261017T180206Z-54876`,
  plus **30 affected Metal PASS**, `batch-update-affected-report`, run
  `20261017T180407Z-56461`. All provenance, source consistency, original SHA and
  catalog-integrity checks passed. Batch growth assertions cover permanent
  row/page footprint for the fixed workload, not allocator RSS or worker TEMP tables.
  UPD-005/006 replace only the NativeUpdater SDK interface, deliver its actual
  callbacks to the real UpdateController, and record that Sparkle network,
  appcast, package download and install were not exercised. Those two cases also
  passed three times in a **local offscreen/software simulation**,
  `batch-update-offscreen-report`, run `20261017T180522Z-57220` (six PASS).
- Three more adapters cover immutable image-watermark overwrite/deletion after
  enqueue, complete named-profile versus last-used settings across two real
  processes, and all four metadata privacy policies on actual PNG outputs.
  Primary ImageIO readers independently check literal GPS/IPTC/EXIF/attribution
  tags, both metadata/properties channels and forbidden serialized XMP values.
  Full settings and profile UUID are checked against independent literals;
  watermark geometry/pixels and all source/document identities remain exact.
  Final evidence: **9 Metal PASS**, `export-settings-unlocked-final-report`, run
  `20261017T222930Z-65253`, plus **18 affected Metal PASS**,
  `export-settings-affected-report`, run `20261017T223110Z-65856`. The same cohorts
  passed in a local offscreen/software simulation (9 + 18 PASS). All provenance,
  source consistency, original SHA and SQLite checks passed. Full application
  build, runner-accounting CTest (25 tests, 5.66 seconds), format and four focused
  clang-tidy checks passed. Nine preceding native activation failures were
  caused by a locked Mac session and remain retained in the original report;
  the user unlocked the Mac before the successful new report was recorded.
- Two more adapters execute 500 byte-distinct private inputs with a persisted
  seeded pause/resume schedule and 100 actual Start/Cancel cycles. Final evidence:
  **6 Metal PASS**, `export-stress-final-report`, run `20261017T225237Z-70937`,
  plus **18 affected Metal PASS**, `export-stress-affected-report`, run
  `20261017T225511Z-71714`. A local offscreen/software simulation passed both
  complete workloads once. Provenance, source consistency, original SHA and
  SQLite integrity passed throughout. Cancel overlaps a genuinely installed and
  committed first output while its original completion remains held; committed
  files survive and unstarted files and temporary artifacts remain absent.
  This does not qualify cancellation inside an encoder or restart persistence.
  Full build, 25 runner-accounting tests, format and two focused tidy checks passed.
- Three more adapters execute all 200 Export open/close cycles, all 200 Import
  open/Cancel cycles and 200 real Detail keyboard edits followed immediately by
  Export. Independent full documents cover every Undo and Redo step. A genuine
  held final catalog write proves Export waits for the last natural autosave;
  exact PNG pixels, three originals and durable import/catalog state remain
  correct. Final evidence: **9 Metal PASS**, `panel-detail-final-report`, run
  `20261018T072955Z-79020`, plus **21 affected Metal PASS**,
  `panel-detail-affected-report`, run `20261018T073124Z-79528`. All provenance,
  source consistency, original SHA and SQLite checks passed. The complete three
  workloads also passed once in a local offscreen/software simulation. Full
  application build, 25 runner tests (5.67 seconds), format and focused tidy passed.
  RSS/object counts are raw observations without a leak or plateau assertion;
  QML Import Cancel does not qualify native file-panel Cancel.
- Three more adapters cover the actual two-retry thumbnail cap and recovery,
  saved thumbnail invalidation while the asset is offscreen, and native Retina
  2x pointer/render mapping. Exact JPEG cache pixels, all visible Image-node
  readbacks, original hashes and complete durable documents remain required.
  Retina uses independent AppKit corner conversions, actual Metal frame pixels,
  real Slider/mask/Crop inputs, rendered handle centroids and exact history.
  Final evidence: **9 Metal PASS**, `thumbnail-retina-final-report`, run
  `20261018T074529Z-82693`, plus **21 affected Metal PASS**,
  `thumbnail-retina-affected-report`, run `20261018T074623Z-82997`.
  The two Library workloads also passed once in local offscreen/software;
  Retina has no software/forced-scale fallback. All provenance, source stability,
  original SHA and SQLite checks passed. Full build, 25 runner tests (5.65
  seconds), format and focused tidy passed. Initial fixture failures remain
  retained; no production behavior was changed by this block.
- One scoped seeded history-chaos adapter executes all 10,000 semantic actions
  through three real Basic Sliders and Undo/Redo. Independent full typed-document
  hashes and a capped timeline model check every action; natural durable saves,
  exact Ready pixels and SQLite integrity are checked every 100 actions and at
  completion. The default seed, an immutable replay in a new process and seed0
  all passed on Cocoa/Metal: **30,000 native actions**, in
  `history-chaos-first-native-report`, `history-chaos-replay-report` and
  `history-chaos-seed-zero-report`. Replay bytes and full checkpoint measurements
  were identical. Another **10,000 actions** passed in a local offscreen/software
  simulation; **18 affected native attempts** passed in
  `history-chaos-main-affected-report`, run `20261018T082544Z-92058`.
  Every run passed provenance, source stability, original SHA and SQLite checks.
  A structurally valid replay with another app version was rejected by the actual
  native binary before Context/window/catalog/seed creation; its raw expected
  rejection remains distinct from UX PASS. The broad Chaos Monkey action pool,
  other CHAOS IDs and randomized worker delays remain outside this adapter.
- One further export adapter covers the supported mutually exclusive resize
  modes and retained scalar: seven modes on landscape/portrait inputs produce
  14 actual PNGs per attempt. Independent literal dimensions and direct 2D
  area/bilinear resampling check the complete output, allowing one RGBA8 level
  for float accumulation; CPU development and ICC encoding are shared and not
  independently qualified. Full document/history, catalog metadata/journal,
  immutable originals and prior outputs remain protected. Final evidence:
  **3 Metal PASS**, `resize-shutdown-final-native-report`, run
  `20261018T191631Z-7773`, plus **18 affected Metal PASS**,
  `resize-shutdown-affected-native-report`, run `20261018T191745Z-8438`.
  A local offscreen/software cohort passed the same 14-output case three times
  and nine genuine SIGKILL/restart cases (12 PASS). All proof, source stability,
  original SHA and SQLite checks passed. Four infrastructure CTests passed
  (8.38 seconds), including 30 runner tests; format and focused tidy passed.
  Test-only shutdown markers preserve natural owner destruction and observed
  zero wrapped SQLite files in all 39 normally exiting child snapshots. This
  is an observation, not a worker-drain barrier or a root-cause verdict for the
  earlier hosted abnormal exit. No production behavior changed in this block.
- The preceding Cocoa block ran 18 scenarios three times: **54 PASS**, all using
  Metal, including actual Cmd+Q, restart, recovery and all three new fixes.
  Build provenance, unchanged source/binary, original SHA-256 and catalog
  integrity passed. Evidence: `cocoa-quit-final-report`, run
  `20261016T222117Z-92749`.
- A separate local CI-environment simulation ran 15 exit/restart scenarios three
  times: **45 offscreen/software PASS** (`offscreen-quit-fixed-report`, run
  `20261016T221827Z-91621`). This is not a hosted GitHub Actions result. The fixture
  now resolves the actual StandardKey.Quit binding; offscreen declares Exit,
  while the Cocoa block separately asserts and exercises Cmd+Q.
- Four isolated idle-host performance scenarios passed in MEASURE_ONLY mode:
  `checkpoint-perf-guard-final-report`, run `20261016T222708Z-94930`. Exact document
  and provider pixels remain required; only scene readback allows one RGBA8 level
  of rounding. These measurements are not performance thresholds or a baseline.
- Latest full CTest: **40 PASS, one optional private Nikon SKIP**, 87.51 seconds
  (`history-chaos-full-ctest.log`). This includes the independent history model/
  replay-plan target, 26 Python runner tests and five replay-routing tests.
  Full application build, format/diff and focused clang-tidy for HistoryChaosPlan,
  HistoryChaosScenarios, HistoryChaosModelTests and main passed. The preceding
  production viewport correction separately passed full CTest (86.59 seconds),
  including new Catalog/filter/rank and superseded-model-reload unit cases.
- Earlier native failures and fixture corrections remain retained and explained
  in [UX_ENVIRONMENT.md](UX_ENVIRONMENT.md). Hosted ca38254 finished with 281
  blocking PASS / one missed import-Cancel input window. The normalized physical
  Copy checkpoint passed nine local offscreen repeats and the native regressions.
  Hosted `3e4fc19` then passed 281 blocking attempts, including those import cases,
  but retained one auxiliary-window polish timeout. The correction passed the
  Cocoa/offscreen repeats above. Hosted `56e4e28` then completed with **282 blocking
  PASS**, passing build, unit/integration, format and clang-tidy. Its source
  consistency and original SHA passed; its build-provenance file was absent,
  so those functional results do not qualify a formally attested release build.
  CI/release workflows now build through the attestation wrapper, require matching
  source/binary hashes before UX execution and retain the proof in their artifacts.
  All 25 runner unit tests passed (5.468 seconds), including 22 mocked CLI matrix
  cases for strict/ad hoc modes and source/binary drift. A real stale-build
  preflight rejected execution with exit 2 before fixture/child execution.
  Hosted `3183ff9` then passed **282 blocking attempts** and all build/unit/static
  steps (run `39056014400`). Its uploaded source/binary proof, source consistency,
  original SHA and 279 native catalog-integrity results all passed. Its source hash is
  identical to the local clean-commit snapshot. This confirms the CI attestation
  path; the blocking subset is still not full current-scope release qualification.
- Earlier hosted `29074c6` run `39110825283` passed build, unit/integration and
  attestation but failed the blocking subset: **281 PASS, one SLD-020 FAIL**,
  with 92 future NOT_IMPLEMENTED. Only its first verify process terminated
  abnormally after the visible quit path; repeats2/3 passed. The original runner
  omitted the exit code and retained an empty message, so a product root cause
  is not yet established. Raw artifact and its SHA are preserved; supervisor
  diagnostics are strengthened before targeted restart reproduction. This CI
  failure remains unresolved and cannot qualify a release.
- Latest hosted `5af765f` run `39156735777` completed all steps successfully:
  build, unit/integration, attestation, **282 blocking PASS**, format and tidy.
  Downloaded artifact SHA, source/binary proof, original SHA and 279 reported
  catalog-integrity results passed. All three SLD-020 prepare/verify pairs exited0;
  this does not retrospectively establish the cause of the earlier failed run.
- APP-014 remains unadapted: programmatic QWindow resize bypasses the native user
  minimum constraint, so it cannot prove the specified lower-width acceptance.
  The original failed fixture is retained, without asserting a production defect.

- MAC-007 now covers real owned disk-image source detach/remount after actual
  Add import. Full UUID/document/revision/metadata/journal/source SHA, actual
  missing-source read error, retained control session and exact main/thumbnail
  pixels survive recovery. This is a two-source functional fixture, with no
  physical USB claim. It reproduced LUMA-UX-024: first-page requests were refused
  synchronously during populated modelReset. Publishing readiness before that
  signal fixes the blank cards; a one-shot startup/refresh/filter unit fails on
  the original ordering and passes after the fix. Full build and **40 CTest PASS,
  one optional Nikon SKIP** passed (101.67 seconds). Three complete native
  candidates passed in `volume-source-qualified-candidate-native-report`;
  previous blank-card, thumbnail-fixture and two unexplained polish failures
  remain preserved and separately described in UX_ENVIRONMENT.md. The registered
  final source passed **21 functional Cocoa/Metal attempts** (MAC-007 and six
  affected cases, three repeats), `volume-source-final-native-report`,
  `20261019T071449Z-17632`, plus **3 local software PASS**,
  `volume-source-final-software-report`, `20261019T071619Z-18703`.
  All 27 actual child stages exited0 with raw PASS; provenance, source stability,
  original SHA and catalog integrity passed. Each of four final own-volume runs
  accepted two attachment generations and checked final ordinary detach.
  Format, 30 runner-accounting tests and focused changed-source tidy passed;
  baseline advisory warnings remain separate. No load workloads ran in this
  final cohort.

## Remaining qualification

Pending functional quality checks include storage/import/export races and native
OS/input boundaries. Long editing/idle/Library sessions, large input storms,
seeded chaos/replay and performance workloads are reserved for the pre-1.0.0
campaign. Native file-panel opening is covered but its
Cancel input remains unqualified. Trackpad/momentum and unavailable hardware
must be reported honestly, never counted as simulated passes.

This checkpoint is not the full release gate. The Metal UI backend used by Qt
also does not implement the planned 0.6 Metal image-processing backend.
The 2026-10-20 user decision above permits 0.5.1 publication after CI despite the
listed gaps. Full qualification and phase 0.6 work remain separate from that
release exception.


## Removable destination and keyboard checkpoint, 2026-10-19

MAC008 requires both Copy and Export in fresh processes/catalogs for every repeat;
a failed Copy prevents Export and a failed Export cannot inherit Copy PASS. Each
branch has three preceding native candidate PASS, real owned-image detach/remount,
source/document/catalog identity and checked final detach. The official aggregate
also passes in the focused local software cohort. This covers ordinary loss at the
established file/host-WAL boundaries; it does not claim physical USB or mid-encoder
loss. The new single-link unit fails the original Copy and passes the fix.

The selected-photo keyboard regression has a separate supplemental domain and a
single generated PNG. Separate fresh processes cover Return and keypad Enter;
each requires one exact UUID/open/load with zero acceptance pointer events. Native
chooser, OS repeat cadence and full KEY015 remain unqualified. CI runs these two
supplemental adapters separately from the 770-ID specification.

Complete build and CTest pass: 42 PASS and one optional private Nikon SKIP (97.32s),
`quality-fixed-final-ctest.log`. Runner accounting passes 37 tests and
quality-policy accounting passes seven. Focused local software covers seven official
cases plus the supplemental keyboard case. Source consistency, attested build,
original SHA and SQLite checks pass. Latest preceding hosted commit `7c2c59c`
completed build, CTest, its blocking UX subset, format and tidy successfully
(run `39215333610`); that run predates this checkpoint's new changes.

The original full reviewed software cohort has 248 PASS and one DSC006 FAIL; passive
traces reproduce its layout clamp three times. The fixed-source full software rerun
(`quality-reviewed-fixed-local-software-report`, `20261019T083331Z-35766`) has
248 PASS, including DSC006, and one DSC003 FAIL. These are local offscreen/software
runs with matching source/binary proof, original SHA and 248 catalog checks each;
the global original-integrity adapter has no catalog. They are not hosted CI or
native Metal qualification.

DSC003 diagnostics preserve two further original FAIL and one PASS in
`dsc003-original-oracle-confirm-local-software-report`, `20261019T084551Z-38913`.
Both failures observe y=0 after one animation tick, then natural y=33 about 15 ms
later without another input, focus change, document change or layout change.
This proves an early test oracle, not a production wheel-routing defect. The
fixture now acknowledges actual displacement from that same single wheel within
one second, then checks settled positive in-range offset, unchanged geometry and
exact full document. DSC003, DSC006 and two related Slider cases pass three times
in `dsc003-acknowledged-final-local-software-report` (12 PASS). The earlier full
report retains its FAIL; the focused result resolves that case without rewriting it.

The earlier native rerun has one PASS and 20 FAIL while the Mac becomes locked
(`mac008-quality-final-native-report`, `20261019T080838Z-30627`). Its original OS
lock evidence and raw failures remain retained. A later read-only OS probe confirms
IOConsoleLocked=false, permitting a fresh native run without changing Mac security
settings. The final affected cohort passes **30 Cocoa/Metal results** across ten
bounded scenarios, including native Retina, Copy/Export remount, import rollback,
selection/keyboard and DSC003/006: `quality-checkpoint-final-native-report`,
`20261019T085424Z-41974`. Both supplemental keyboard routes pass three times
(**6 Metal PASS**), `keyboard-two-routes-final-native-report`,
`20261019T085602Z-43302`. The same final code passes 27 official and six supplemental
local offscreen/software results; those are not hosted CI. Every final report
passes build provenance, unchanged source/binary, original SHA and SQLite checks.

The removable-destination and keyboard checkpoint used source snapshot
`06a50ce703137085eed3e2a7f791dc7fb570574b80150a1d2d5fbec41031f4d9`;
the tested binary SHA-256 is
`76aae6657370eb14b34b12b4353f1551fef8ea8a61d18295c3c55ac87dec5d0b`.
Later documentation-only edits record these outcomes. Full app build, 37 runner
tests, seven policy tests, format and three affected-source tidy checks pass;
pre-existing advisory tidy warnings remain separate. The native lock blocker is
resolved for this cohort, while the 31 functional adapter gaps above remain open.
No tag, production release or phase 0.6 work has started.


## Hosted functional artifact audit, preceding the gradient fix

The downloaded artifact for hosted head `dceb3fbff0ee899ef91d0bd185f1180637fac117`,
run `39226491030`, records **195 specification-domain PASS**, **29 deferred SKIPPED**
and **92 future NOT_IMPLEMENTED**, with no FAIL. The PASS total comprises three
repeats of 64 implemented blocking cases plus three suite-wide original-integrity
results. The 29 deferrals are inventory records, not 87 completed attempts.
A separate supplemental keyboard report records **6 PASS** across Return and
keypad Enter. Both reports use local runner offscreen/software execution on the
hosted macOS machine; neither is a Cocoa/Metal result.

Downloaded archive SHA-256 is
`8137c52ddc94d81c24b327c1da6f80e960d5fb9c7221a3582f63f9d5356e0ac9`.
Build provenance, source consistency and original hashes pass in both reports;
the main report has 192 passing catalog checks, and the keyboard report has six.
GitHub checked out synthetic merge commit
`c5582cf26df9208ea3c3336bf832e9c6af49f0d2`; its attested source tree matches the
clean PR head, SHA-256
`2387256288f548fb6f8df2afcc1dba2e8691969ce76cc5589899a099ca12964b`.
The original archive is retained. The first local audit's incorrect requirement
that merge checkout commit equal PR head was corrected; that audit correction is
not a product fix or a UX result. Both reports retain `release_gate=NOT_QUALIFIED`.
This hosted snapshot predates LUMA-UX-028 and contains no gradient-handle
supplemental result.


## Gradient-handle access checkpoint, 2026-10-19

LUMA-UX-028 is fixed and its bounded functional regression is verified. Existing
Linear and Radial mask geometry remains unchanged by Crop/Done, selection and
handle layout. The local overlay now reserves at least 84×84 logical pixels,
keeps the actual photo rectangle explicit and places all three distinct display
handles inside its input surface. Raw mask coordinates remain unchanged; clipped
raw guides and separate hollow proxy circles do not depict a moved mask. Proxy
press records a frozen source/input anchor, and motion applies relative source
deltas, including rotated radial axes, without an initial snap or a dead area in
thin-photo padding. Ordinary brush, range sampling and empty-canvas redraw retain
their photo and coverage bounds.

Two supplemental cases each use one generated PNG and genuine Crop/Done plus
actual pointer input. Normal and thin Crop variants require all six Linear/Radial
handles to accept real press/drag/release in base, H, HV and V states, with an
independent integer-Crop/source-delta oracle, exact unrelated full-document
fields, one Undo/Redo/Undo, current Ready pixels and natural durable saves. The
thin variant also rejects ordinary Linear redraw, Brush, Luminance and Color
input on an actual empty padding pixel. Original bytes, UUID/path/catalog
membership and complete documents remain required throughout.

Final focused evidence: **6 Cocoa/Metal PASS** in
`mask-edge-verified-final-native-report`, run `20261019T093709Z-53404`, and
**2 local offscreen/software PASS** in
`mask-edge-verified-final-local-software-report`, run `20261019T093545Z-52990`.
Affected official cases have **51 Cocoa/Metal PASS** in
`mask-edge-verified-affected-native-report`, run `20261019T094024Z-55062`,
and **16 local offscreen/software PASS** in
`mask-edge-verified-affected-local-software-report`, run
`20261019T093648Z-53260`. All final child results exit0; build provenance,
unchanged source/binary, original SHA and SQLite integrity pass in every report.
The native affected cohort includes the existing Retina mapping case; the
software results do not qualify that native boundary.

Final application build and CTest pass: **42 PASS and
one optional private Nikon SKIP**, 80.89 seconds,
`mask-edge-verified-full-ctest.log`. Runner accounting has
`40` passing tests and policy accounting has
`seven`; format and the five focused changed-source tidy commands
complete successfully. Advisory diagnostics, including test cleanup and optional-access warnings, remain
recorded separately in the retained tidy logs; none is a command error. The
attested final source tree SHA-256 is `dc3fe1ec0c5189e5223aa20991a94832a6acdbd76d65bc9de514f85577e7d173`; the tested
binary SHA-256 is `7f74fbeab250b70a496ba98cc0a82e0f670d1db6ea5fcaf5ffc617d76901bba5`.

The original one software FAIL and three Cocoa/Metal FAIL remain unchanged in
`msk023-crop-original-local-software-report` and
`msk023-crop-original-native-report`. The first fixed-source thin fixture failure
also remains in `msk023-edge-proxy-first-local-software-report`: its supposed
empty padding point overlapped a legitimate proxy. The corrected negative control
finds a real empty pixel without excluding valid handles, changing production
callbacks or weakening the no-edit/no-sampling/no-extra-history assertions.
The initial CTest readiness failure in `mask-edge-full-ctest.log` is retained
separately from the completed final CTest report.

These two cases run through `run_mask_handle_regression.py` in a separate
supplemental inventory domain. They do not register official UX-MSK-023 or change
the 770-ID specification. At that checkpoint the official adapter count stayed
361 and the functional adapter gaps stayed 31; the later official Auto-completion
adapter is accounted for below.
The bounded plain-Crop/flip matrix does not qualify every zoom, Transform,
projective, pan/recenter or display boundary in full MSK023. Exact CPU reference
pixels use the shared renderer/ICC path and are a UI/state-coherence oracle,
not independent numerical qualification of the renderer. The final local cohorts
are not hosted CI for the new source. No load workload, release tag, production
publication or phase0.6 work is implied by this checkpoint; full current-scope
release qualification remains incomplete.


## Genuine Auto completion checkpoint, 2026-10-19

Official UX-RACE-005 now verifies both genuine successful OLD Auto results:
actual Cancel, and actual filmstrip A→B switch before the original result's
queue/delivery. The test-only worker hold starts after the real solver succeeds;
logical revisions, source UUID/path and retained actual JobData identify the same
result at its original success callback and stale rejection. One delivery and one
rejection are required for each operation. No fabricated callback or solver refusal
can satisfy the successful-result controls. Both stale callbacks are rejected by
the existing first Auto-revision guard; the observed load-revision change does not
independently exercise the later load guard. No production defect or change in
normal Auto behavior is implied by this adapter.

The two imported small assets have independently known complete documents and
nonneutral pixels. Actual controls must finish their seed gestures before Auto;
Cancel preserves the original one-entry Undo/Redo history, while B and the return
to A load fresh histories at their persisted documents. Passive state observations,
current Image Ready/source/provider pixels, natural catalog saves, exact saved
revisions, source SHA and membership remain required. The CPU/ICC reference checks
state coherence through the shared renderer; it does not independently qualify
its numerical implementation or physical displayed-frame pixels. The file-chooser
result is a declared setup seam; native chooser acceptance is not claimed.

Final registered evidence: **27 Cocoa/Metal PASS**, nine bounded cases three times,
`race005-registered-final-native-report`, run `20261019T095737Z-61847`.
That includes three complete UX-RACE-005 attempts and three ordinary successful
Auto Apply/Undo/Redo controls in UX-UND-007, plus manual/guided Transform, Cancel
and photo-switch scroll checks. The same nine cases pass once in local
offscreen/software, `race005-registered-final-local-software-report`, run
`20261019T095722Z-61703`. Every child exits 0 and every report passes build proof,
unchanged source/binary, original hashes and catalog integrity. Initial candidate
runs also passed once per backend in separate preserved supplemental reports;
they are not added to the official result totals.

Full build and CTest pass: **42 PASS, one optional private Nikon SKIP**, 81.56s,
`race005-registered-final-full-ctest.log`. Forty runner tests, eight quality-policy
tests, format and four focused tidy commands pass. Advisory tidy diagnostics
remain in the original logs, separate from command errors. The tested source tree
before documentation-only result recording is
`1130e2e1d50029faa2434662817d0dd3cea180a2066cb71e310ea11affda4164`;
the binary SHA-256 is
`3f8781cbe55f545c0c2d5929f9f6a28c9a794deaf38d75e1f239f7466503bf26`.

CI/release workflows execute this REGRESSION case in a separate focused quality
step and retain its own report and operation JSON; the blocking subset alone does
not select it. The one-PNG focused fixture also generates two small DNGs, imports
only two assets and starts Auto twice, with no large fixture or TRN014 cycles.
The mixed local cohort shares ten small PNGs; none becomes a bulk import.
The probe is private to luma_ux_tests and adds no production gate or QObject ABI.

This closes one functional adapter gap: **362 native adapters**, 250 reviewed
bounded adapters and 112 deferred compiled workloads. Of 81 unadapted applicable
scenarios, **30 remain functional gaps** and 51 remain deferred load/performance/
chaos checks; conditional absent MSK005 and independent original-integrity remain
separate. Full MSK023 remains unqualified. The official 770-ID inventory, 28 fixed
production defects, stable0.4.14 and release NOT_QUALIFIED status are unchanged.
No load test, phase0.6 work, tag or production release ran in this checkpoint.


## PhotoCard and real thumbnail-response checkpoint, 2026-10-19

LUMA-UX-029 fixes hardcoded Pick/Reject captions through the existing translation
keys. Its preserved original software failure is followed by a focused software
PASS for all eight actual Settings language choices, with exact independent
literals, current card Image-node/cache pixels, typed documents, metadata, natural
persistence and Undo/Redo. Original and fixed evidence are separate. Native
original was not run; fixed Cocoa/Metal acceptance is pending because the Mac
session remains locked. No complete VIS007/VIS008/LOC qualification is claimed.

A separate supplemental LIB004 candidate observes real QQuickImageResponse
objects and their original lifecycle in the UX target only. Its selected worker
holds a successfully computed OLD image before the original completion queue,
after catalog and provider/cache locks are released. A real scrollbar press must
still own the current visible OLD Loading binding, with no prior reset/cancel/
finish. One real coarse move reveals a previously unrequested NEW tail card;
its genuine request, saved revision, successful result, finished signal, texture
consumption and current Ready pixels must precede explicit OLD release. Actual
processed cancellation is allowed to finish/destroy OLD without a GUI reply.
The same visible UUIDs, viewport, per-card independent pixels and all32 pinned
neutral documents/revisions/metadata/originals must survive that cleanup.

The first software candidate passes in
`card-lib004-fixed-first-lib004-local-software-report`, run
`20261019T103956Z-68969`. Actual grid1079×382, four columns, cell269.75×210 and
cacheBuffer420 require the minimal reviewed32 small RGB PNGs; no large image or
bulk workload is generated. Eight visible cards have own-reference mean RGB
error0 before/after release; the nearest wrong-card error is at least23. The real
OLD response is cancelled by Qt, then emits its genuine finished signal and is
destroyed; no OLD GUI delivery is claimed. This pending-completion boundary does
not establish active disk I/O, exhaustive transient frames or physical scanout.
Official UX-LIB-004 stays unregistered until further acceptance; official counts
remain362 adapters,250 bounded,112 deferred and30 functional gaps.

Mask and Auto diagnostic documents now also retain canonical
`local_masks_serialized`, because preset encoding omits masks. The repeated normal/
thin Crop software cases pass in
`card-lib004-fixed-first-mask-local-software-report`, run
`20261019T103959Z-69021`, and official RACE005 passes in the corresponding
`race-local-software-report`, run `20261019T104102Z-69547`. Typed runtime oracles,
numeric tolerance, full-document/history/pixel/durable checks are unchanged.
This is instrumentation completeness, not a production fix or extra official
adapter. Historical incomplete artifacts remain unchanged.

These first software results share tested source SHA
`151e312372a5471bf20b086420fc2eb57ef855c99ff689c245c2d0ff9fed51c4`
and binary SHA
`6e88661927cf16eeabc9af99e7a822b7ee3868da1ccd53434ae0ac3b24d5d5c9`.
Separate supplemental wrappers preserve both new regressions for CI without
aliasing official IDs. Workload-policy tests reject bulk/duplicate fixture counts
before runner setup and pin exact bounded RGB-only inputs. The full release gate
remains NOT_QUALIFIED; no phase0.6, load test, release tag or production publication.

### Hosted ec51a5d artifact audit

The independently audited archive SHA is
`b0a3893f77e9aa57ca40a092f5af96d950e0d1e51939ac814bc6ef2adf3e1c7e`.
Its four reports contain195 blocking-subset PASS,29 implemented deferred SKIPPED
and92 future NOT_IMPLEMENTED, plus six keyboard, six mask and three RACE005 PASS.
The main195 includes three runner-only SHA observations. There are231 normal
exit0 processes and six expected SIGKILL persistence prepare stages. All normal
raw children report offscreen/software, not Cocoa/Metal. Archived proof agrees
with clean ec51 source SHA
`9fe69d8ffacdd3adb8fba7fc9d3e1234faa33c4a6ca9b958da54dfc6117dcb8f`;
CI checkout is the synthetic merge `f76d4735ba9c88682bbb629410687f2e2cf95179`.
All four release gates remain NOT_QUALIFIED. This archive audit does not establish
completion of remaining workflow steps or independently recheck SQLite/original
bytes, which are absent. The historical mask JSON contains all144 typed-check
markers but omits numerical local-mask deltas; those cannot be reconstructed from
that archive. Historical runtime PASS is retained with this explicit evidence limit.


## Native acceptance and interrupted-run evidence, 2026-10-19

After the Mac was observed unlocked, final PhotoCard captions and real thumbnail
responses each passed three times on Cocoa/metal DPR2 and three times in local
offscreen/software. These are twelve supplemental results, not twelve new
specification adapters. Reports are `card-thumbnail-retina-card-native-report`
(`20261019T105445Z-73292`), `card-thumbnail-retina-card-software-report`
(`20261019T105433Z-73165`), `card-thumbnail-retina-lib004-native-report`
(`20261019T105417Z-73045`) and `card-thumbnail-retina-lib004-software-report`
(`20261019T105410Z-72993`). Their exact tested source is
`5a1a35eac6846e715c67f77bf2b8d1e8a49ca214b9ed9611448336d67624fd69`;
binary is `c4613c871f61c148d5fb7da0ed805bbb449518fd8287814cb8c353e0259935d4`.
All proof, original SHA and catalog checks pass. Card captions now have focused
native acceptance; full VIS007/VIS008/LOC remains outside that result.

Two fixture corrections retain their original failures. The caption fixture's
single wheel event failed to move the clipped grid in three software attempts;
its cause is unexplained. One real scrollbar drag now reveals the same strict
caption bounds with acquired press, directional displacement and legal settled
offset; no setter/retry or translation-oracle change is involved. The first three
native thumbnail attempts failed before the gate because Qt requests768×768 on
Retina for the declared384×384 source. The test now independently predicts
`QSize(384,384) * window.effectiveDevicePixelRatio()` before arming, verifies the
logical sourceSize separately, and requires that exact physical size on both
actual OLD/NEW tuples. No response wildcard or production size change is used.
The retained native failure trace proves the size mismatch; corrected DPR1/DPR2
runs pass with genuine current OLD Loading and real terminal cleanup.

The same source's expanded mask regression produced **three PASS and three FAIL**
in `card-thumbnail-retina-mask-native-report`, run `20261019T105606Z-73698`.
Normal repeat2 timed out on the existing conjunction localMode/localCanvasReady/
not-localSampling after a final Undo; the full typed document assertion had
already passed. Thin repeats1/2 timed out on window polish and evidence capture.
The Mac was unlocked. These are **unresolved native outcomes**, not proven render
races, fixture failures or extra production defects. The old messages do not
separate the predicate terms, current error, window exposure or other input.
Original reports, actions, screenshots and mixed counts remain unchanged.

A passive bounded readiness timeline now records distinct public controller and
window states on genuine stateChanged/afterAnimating, with a final document before
Context cleanup. It makes no render, cancellation, input, wait or acceptance
change. Both software cases pass in `mask-ready-passive-mask-software-report`,
run `20261019T110201Z-75351`; all six native attempts pass in
`mask-ready-passive-mask-native-report`, run `20261019T110304Z-75610`.
The six native timelines have no dropped/failed samples, active exposed windows,
no preset preview and an unchanged physical cursor. This does **not** explain or
fix the three prior failures: observation can affect scheduling, and the old
outcomes lack those fields. The user subsequently confirmed using the Mac during
the first mask run. External interaction could affect native focus/input, but the
individual causes are not established. Those original outcomes are retained as
an externally interrupted run; they neither prove a production defect nor become
PASS. The later six passing native timelines independently establish their own
bounded functional acceptance, without a further production fix.

Canonical mask artifacts now independently expose all192 accepted geometry edits
and exact restored documents across those eight attempts, including base/H/HV/V
for both shapes. Saved actual/expected numerical differences are at most
1.6653345369377348e-16, within the unchanged less-than1e-12 runtime tolerance.
This repairs future artifact completeness; the old CI mask omission remains an
explicit historical limit. Full official MSK023 stays unqualified.

Four ordinary no-Session provider controls (REC009 clean corruption/restart,
SEL002 card activation, SEL017 filmstrip switch and LOC005 Unicode flow), plus
RACE005, pass five times once in software and fifteen times across three native
repeats. See `mask-ready-passive-provider-control-software-report` and
`mask-ready-passive-provider-control-native-report`. REC009 has an independent
Ready/cache oracle; the activation/filename cases are not relabelled thumbnail
race or pixel qualification. Auto still requires its genuine solver completions.

Full build/CTest pass:42 PASS plus one optional private Nikon SKIP,82.26s, in
`mask-ready-passive-full-ctest.log`. That includes40 runner and10 workload-policy
unit tests. Format and all seven focused tidy commands exit0; advisory diagnostics
remain in logs. This last tested source before documentation-only recording is
`cedef5a1ab8e28090de5b091f928a31cd5d9129098705a25c3704caff0c00e03`;
binary is `8af6e5692443bc7452a3c78d6d6206288ac298f430035919b7dc272c3135b18d`.
The private trace define is absent from both ordinary production/thumbnail-unit
provider compile commands and present only in the UX target.

GitHub subsequently confirms the earlier clean ec51a5d workflow run39234353511
completed successfully, including clang-tidy. Its uploaded evidence audit and
historical mask limitations remain as recorded above. This is not CI completion
for the newly changed source. Official counts remain362/250/112 and30 functional
gaps. LIB004 remains a supplemental candidate pending generic fixture/inventory
integration; a prepared RACE002 draft has no runtime result and is not counted.
No load test, phase0.6 work, release tag, merge or production publication occurred.
Full current-scope release qualification is still NOT_QUALIFIED.

## Official thumbnail response regressions and complete bounded pass

The next checkpoint registers official `UX-LIB-004` and `UX-RACE-002`, keeping
their existing specification modes, scopes and severities. There are now 364
compiled native adapters: 252 reviewed bounded and 112 deferred implemented
workloads, plus the global integrity adapter. Of 445 applicable scenarios,
conditional MSK005 is absent and 79 remain unadapted: 28 functional and 51 deferred
load/performance/chaos checks. No production defect is added to the 29 reproduced
fixes by this checkpoint.

LIB004 uses exactly 32 fresh opaque RGB sources; RACE002 uses two. Separate
run-owned case directories preserve ordinary fixture counts and bytes for every
other selected case. Their path-based SHA union is checked after every case and
at suite end. Equal contents at different owned paths remain distinct originals.
The manifest records the actual base/extra/total counts. Alias, duplicate-path,
symlink, digest, missing-file, mutation and ineligible-selection tests fail closed;
an explicitly ineligible profile-only request never falls back to 1000/Large.
All 67 combined Python infrastructure tests pass, including 17 dedicated fixture
tests, 40 runner tests and 10 workload-policy tests.

RACE002 observes a genuine successful OLD request held before its original
completion queue. A real Exposure gesture naturally saves revision 1 and emits
one targeted A invalidation. The real NEW response must read that revision and
reach Ready with its independent saved-document pixels while OLD is still held.
Explicit OLD release, finished and destruction preserve current NEW pixels and
the untouched B asset. Separate actual Undo/Redo produce revisions2/3. Full typed
documents, metadata, row fields, session/import journal, source SHA and history
are pinned independently. This is request/completion ordering and UI/data
coherence, not an independent numerical renderer or physical scanout proof.

The first candidate attempt, `lib004-official-race002-first-race002-software-report`,
run `20261019T112549Z-79939`, crashed in Driver::type when the declared FileDialog
selection seam left no active text focus. Its crash report is retained. The next
attempt, `race002-dialog-focus-software-report`, honestly timed out waiting for
focus return. Both failed before the race boundary; neither proves a production
race defect. Setup now explicitly reactivates its own window before installing
the Session, then requires actual focus. Native picker selection and platform
focus return remain unqualified. Driver::type checks null before sendEvent.
The unchanged race contract then passed three times per backend as a candidate;
those original supplemental results are not relabelled official results.

Official final reports use prefix `official-thumbnail-final`: six software and
six Cocoa/Metal results across LIB004/RACE002, nine mixed results with SEL002,
IMP009 and UND018, eight affected controller/input results and two supplemental
card-caption results. All 31 rows PASS, with one global-integrity row included.
Mixed SHA unions are 34/46/56 as planned. Independent trace/pixel/SQLite/SHA audit
found no discrepancy across all 18 thumbnail executions. Full build, format and
four focused tidy commands exit 0; advisory diagnostics remain in their logs.
CTest passes 44 tests plus one optional private Nikon SKIP, 45 total, 84.50s.

The broader local offscreen/software run, `all-current-quality-281a591f-report`,
run `20261019T114156Z-86851`, selects all 444 current rows except native-only
MAC001. It passes 251 real application scenarios plus the global SHA check:
**252 PASS, 191 SKIPPED, one NOT_IMPLEMENTED, zero FAIL**, 398.638s. SKIPPED comprises
112 implemented deferred workloads, 51 unadapted deferred checks and 28 functional
gaps; MSK005 is the single absent conditional capability. Its raw exit 1,
execution INCOMPLETE and release NOT_QUALIFIED are expected and preserved. It
creates only 20 base PNGs, two small DNGs and 34 dedicated RGB sources, 56 originals
in total, with no 24MP/large-catalog fixture and no deferred execution.

MAC001 separately passes its strict native AppKit/Metal/Retina contract in
`official-thumbnail-final-retina-native-report`. Thus all 252 currently implemented
bounded native adapters have execution evidence for this source: 251 in software
and one native-only Retina case. This does not close the 28 unadapted functional
scenarios or qualify deferred workloads. The final official, broad and Retina
cohorts attest source
`281a591fe832665bdfe87ef8d1094324ebab206c452b0c60ae16280ba917680c`
and executable `67a284b38a337c174d2be0a7a460aa9f71f6fee638ac0d2f786af2bfd21429e5`.
Provenance, source consistency, original SHA and catalog integrity pass. Local
GITHUB_ACTIONS simulation is not hosted CI.

The earlier 50f6e16 hosted run 39242877166 has completed its functional/supplemental,
artifact-upload and format steps successfully; clang-tidy was still in progress
at this recording. Its downloaded 47,360,440-byte ZIP matches API SHA
`722646f2a5c5661465b7c1cfe4be6c00e9fc0146ca9c3b0897913571e94d92e3`.
Actual checkout051586954bbb2bdcc5319baa8468ef53b32c48f8 is the verified synthetic
PR merge, whose second parent is50f6e16 and whose Git tree equals that head tree.
That archive is evidence for the prior source7085381e checkpoint, not current281a.
No workflow completion, release, tag, main merge or phase0.6 work is claimed here.

## Official missing-watermark execution regression

Official `UX-EXP-028` is now registered without changing inventory mode, scope or
severity. There are365 native adapters:253 reviewed bounded and112 implemented
deferred workloads, plus global integrity. Of445 applicable rows, conditional
MSK005 is absent and78 remain unadapted:27 functional and51 deferred. This adds
no production defect to the29 reproduced fixes and does not qualify the release.

The actual three-photo queue first commits A with independently verified source
and red-watermark pixels. Before B starts, the test unlinks only the sole verified
private runtime snapshot inside its own canonical0700 temporary root. Original
watermark/source files and live original-path options remain intact. Actual B and
C each fail with the exact missing-watermark message; C starts after B, the queue
finishes3/3 with1 success and no global error, and only unchanged A remains. All
three full neutral documents/revisions, metadata, membership, selection, session
and import journal are pinned. Per-job snapshot-path tracing is not claimed.

The separate launcher environment is installed before Popen/Qt and never mutates
the parent environment. Validated wrapper/child PID/nonce, UID/mode/inode and
sidecar hashes are embedded in the narrow uploaded evidence. The four generated
RGB originals include three64x48 photos and one32x32 red mark. Dedicated profiles
preserve existing generic/LIB/RACE inputs and exact50/60/38 mixed SHA unions.
All78 combined Python infrastructure tests pass. The first supplemental software
run and subsequent3software+3native candidate runs remain supplemental evidence;
independent PNG/SQLite/SHA/ownership audits found no discrepancy.

The final cohort `official-exp028-final` attests source
`77cd3c5e1e20924b73d27488fd7230df67bc73f04c28164ff04286e99b876e3f`
and executable `e905e3b75b785d12add3861cc9856d890b7cfb3f71afd753aa1a06348bf01f0f`.
All25 rows PASS:24 official and1 historical supplemental EXP row. These include
3software+3Cocoa/Metal EXP repeats,3software+3native RACE002 repeats and three
mixed LIB/RACE/EXP selections with SEL002, IMP009 and global UND018. Independent
audit verifies all25 rows,10 actual EXP outputs/owned runtimes,9 RACE executions,
3 LIB executions and shared originals/catalogs. Native LIB is not newly claimed
by this cohort; its three mixed executions use software. All local RACE immediate
session observations and both expected JSON forms match the exact nested row.

Full build passes; CTest passes45 tests with one optional private Nikon SKIP,
46 total,84.41s. Format and five focused tidy commands exit0; advisory diagnostics
remain visible. The previous broad251-application/software plus native Retina
evidence remains historical source281a evidence, not a rerun of every adapter on
source77cd. No load, stress, soak, chaos or performance qualification was run.

## Preserved hosted setup failures and native lifecycle gaps

Hosted6413ca5 run39248009895/job118044714145 completed with failure: all three
`UX-RACE-002` attempts stopped at the initial Import/session baseline before any
response Session/race boundary. Blocking195, keyboard6, gradient6, card3 and
LIB0043 attempts passed. Later Auto Transform/static steps were skipped. Its
47,929,429-byte artifact12185576864 matches SHA
`e2254fad7605edad9b0d48da337f2e14c97ca785b271110f7a1fd98473c1127c`.
The actual synthetic merge858b990 is independently verified to have the same Git
tree as6413ca5; hosted source8a0d00d5 is that exact clean head, with its own binary
4d05b149. The earlier50f6e16 workflow was subsequently cancelled by the new push;
its successful individual steps are not full workflow success.

All three failure SQLite snapshots independently contain one completed Add, two
committed items and the correct active UUID; they are snapshots after failure,
not the missing immediate guard observation. The exact failing clause/cause is
still UNKNOWN. SessionService commits selection before photoLoaded, so a late
session commit is not established. Local AppleClang21/Qt6.10.3 constructor probe
produces the intended nested JSON for both old and explicit forms; hosted uses
AppleClang17.0.0.17000013. This compiler difference does not prove causation.
RACE002 now forms expected nesting explicitly and saves actual/old/new values and
all three clause outcomes before requiring the unchanged strict contract. Local
passes do not replace the three hosted FAILs; a new hosted check is required.

The supplemental native lifecycle candidate observes genuine held Exposure5,
real separate Finder activation, native DidResignActive/key/workspace counters,
Qt ApplicationInactive/WindowDeactivate and history=false/Slider.pressed=false
before manual release or cleanup. Its UX-only plain C++ history getter observes
without finishing a gesture; ordinary application/moc builds have no getter
define. The underlying core const observation changes no state or class layout.

The first default run `app-deactivation-first-native-report` preserves its10s
own-return FAIL. On source77cd, `app-deactivation-external-first-native-report`
preserves a distinct45s external return FAIL: one CUA Finder Cmd+Tab dispatch did
not yield actual own activation. The unique-name capability run
`app-deactivation-external-unique-name-native-report` also fails; CUA returned
`Invalid app: luma_ux_tests` for the live unbundled process. Native bundle ID is
empty; no installed Luma was selected. Every accepted deactivation observation
still precedes release and naturally preserves Exposure5/revision1, but return,
new gesture, Undo/Redo depth and post-return pixels remain unqualified. These are
test transport gaps, not demonstrated product defects or official APP017/SLD009
passes. All original non-PASS artifacts remain intact; no automatic retry or
security/activation-policy change converted them into a PASS.

Phase0.5 functional qualification remains incomplete. Work stays in draft PR47;
there is no release/tag/main merge, phase0.6 or production publication.

## Official away/save/back thumbnail and test bundle checkpoint

`UX-LIB-005` is now official with unchanged severity/scope/mode. The current
inventory has366 native adapters:254 reviewed bounded and112 deferred, plus
global integrity. Of445 applicable rows, conditional MSK005 remains absent and
77 are unadapted:26 functional and51 deferred. No production defect is added to
the29 reproduced fixes. Deferred workloads have not been executed or qualified.

The bounded fixture owns32 distinct opaque256x192 RGB originals. Actual Library
bar input scrolls away before OLD completion, actual Develop input produces one
natural Exposure5 edit/revision1, and actual Library bar input returns to the
current NEW Image before OLD is released. Exact source/revision tracing, all
eight returned visible identities/pixels, targeted invalidation, full document
and metadata/journal/SQLite/SHA guards and one Undo/Redo to revisions2/3 pass.
All observed OLD responses are naturally cancelled; no uncancelled branch is
claimed. OLD is held after successful computation before finished delivery:
this is stale-completion coverage, not active CPU computation overlap. The
source-domain trace does not identify ownership by an individual consumer.

The official cohort attests source
`b326cdaf1375aa92cefa1f227ffe13c137bec2ec88e710278ed690e495dc38bb`
and binary `e56db157877bb10eb0cc03acba18b4fff0b94b2c260cfd8575f91b30fa65d2dc`.
All28 runner rows PASS:27 official and one historical supplemental LIB005 row,
including one global integrity row and27 actual child executions. LIB005 has
3software+3Cocoa/Metal repeats and three software mixed runs. The mixed profiles
contain70 distinct LIB004/LIB005/RACE002/EXP028 paths, with exact82/92/70 SHA
unions for SEL002/IMP009/global integrity. Additional EXP028/RACE002/KEY001
controls pass once on each backend. The separate completed-artifact audit checks
all10 LIB005 executions against literal fixtures, full SQLite rows and real
trace order. JSON pixel metrics are audited; raw per-node readbacks are not
archived and the CPU/ICC/JPEG reference pipeline is shared.

All102 combined infrastructure tests pass. Full build passes; CTest has47 PASS
and one optional private Nikon SKIP out of48,87.77s. Format and two focused tidy
commands exit0 with advisory diagnostics retained. The old full software sweep
remains historical evidence, not a new sweep of every adapter on this source.
The first cohort supervisor misclassified the historical LIB005 ID as EXP for
its final post-run verifier and failed after that actual child had passed.
That harness failure and raw results are retained; a read-only audit and a
separate static/unit-check continuation completed without retrying app cases.

The UX target is now a real build-only macOS bundle with the unique native
`app.luma.ux-tests` identity. Generated Info.plist SHA
`300c1d5531728371ef2444f49eff853e5494e69064fe9ab67bdf8ecdd27d885a`
and canonical executable identity are part of build/suite provenance. The
compatibility CLI is an exact absolute symlink. Legacy flat migration refuses
unattested/different files; configure must precede build_ux for that one-time
migration. This adds no shipping app packaging, signing or distribution claim.

The first bundled APP candidate, source653280ca/binary7068d21e, proved actual
bundle ID/window/PID and genuine deactivation, but CUA Raise/title input did not
produce native own activation. Its45s return FAIL remains separate. Dock access
then timed out without another candidate run. On2026-10-19 the user deferred the
manual Cmd+Tab return check. APP017/SLD009 remain unqualified; no full history
acceptance or new official PASS is inferred from deactivation alone.

The user approved exactly three scope clarifications: APP014 native subminimum
requests clamp to1000; CRP010 uses the existing Done-only interaction; VIS003 uses
the230-UTF8-byte expanded stem plus extension with intentional middle elision.
Original text and exact limits are preserved in UX_SCOPE_CLARIFICATIONS.md.
Those changes alone add no adapter or PASS, and authorize no other scope change.

## Hosted RACE002 oracle diagnosis on a94eac6

Hosted run39253840073/job118064040756 completed successfully, including all
functional/supplemental, artifact upload, format and tidy steps. Its
49,280,866-byte artifact12187549851 matches API SHA
`7f8bd09fc8c406824614ec9e6a106d55aadd9a5e797618d6448d39efba0809fd`.
Fetched synthetic merge156ea2d has a94eac6 as its second parent and the exact same
Git tree. Hosted attested source9b02f0ab equals the clean a94eac6 checkpoint,
not the newer local bundle/LIB005 source.

All three immediate setup JSON records contain one completed Import, two items
and the exact nested actual/explicit expected session `[["1", uuid]]`; every
strict clause is true. In the same records the former constructor expression
produces flat `["1", uuid]`. This directly reproduces a test-oracle portability
defect in the hosted build; the local AppleClang21 probe remains nested.
The archive alone does not expose its compiler version. Later completed job logs
confirm AppleClang17.0.0.17000013/Xcode16.4 in this run. The exact compiler/Qt
overload-selection cause is not established by these JSON observations.
Explicit construction removes the ambiguous initialization without weakening
the session assertion. The three older hosted FAILs remain unchanged and their
missing immediate values are not reconstructed. No late commit or product data
corruption is established by them.

The archive contains195 blocking PASS,29 SKIPPED and92 NOT_IMPLEMENTED rows,
plus6 keyboard,6 gradient,3 card,3 thumbnail,3 RACE002,3 watermark and3 Auto
Transform PASS. These counts do not close the remaining adapters or deferred
workloads. Phase0.5 remains incomplete; work stays in draft PR47 without a
release, tag, main merge, production publication or phase0.6 work.

## Official Crop Done and legal current export filename checkpoint

After runtime qualification of the approved CRP010/VIS003 contracts, the inventory
has368 compiled native adapters:256 bounded and112 deferred, plus global integrity.
Of445 applicable cases, conditional MSK005 remains absent and75 have no adapter:
24 functional and51 deferred. The29 reproduced product fixes are unchanged.
APP014 still lacks real native resize input; its approved wording alone is not a
PASS. The user's deferred manual APP017/SLD009 focus return remains unqualified.

CRP010 uses one literal256x192 stripe PNG and real Crop drag/Done input. The actual
composited scene positively contains crop dimming/guides/border before Done and
matches an independently mapped source/letterbox afterward. Ordinary post-Done
input and re-entry/Done do not add a hidden edit. Full typed documents, natural
revisions0/1/1/1/2/3, three saves and one Undo/Redo pass. A separate read-only
audit recomputed35 scene PNGs across six official native children and one
historical supplemental child:28 follow the first Done, including seven Undo
scenes. All independently predicted flat pixels have zero mismatches/maxRGBA
delta0; predetermined interpolation/border bands are excluded, not qualified.
Active-overlay controls and neutral negative controls pass in every child. No
Crop Cancel, physical pointer or display scanout claim is made.

VIS003 owns four literal64x48 PNGs: short stem, ASCII230 bytes, Cyrillic230 bytes
and231-byte overlimit stem. Actual export actions expose the full logical output
basename while working; bounded middle-elided text and adjacent controls remain
stable. Three legal outputs per child have exact literal RGB/sRGB content; the
231-byte request fails planning before any file item executes. Source/document/
catalog identities, settings bytes, existing outputs and sentinel remain intact.
Independent audit covers ten official children and one historical supplemental
child:33 output PNGs,44 real enqueue attempts and44 preservation checkpoints.
This qualifies the PNG representative and UTF-8 boundary, not every output format
or OCR/physical display rendering.

The first software and native VIS003 drafts each failed their layout assertion.
They mistakenly measured QQuickText's painted boundingRect as allocated geometry.
Recorded label/action positions were stable while that virtual paint width changed.
The correction observes item size for allocated geometry and retains separate
paint containment and exact layout comparisons; it changes no product layout or
tolerance. Both original FAIL reports and the corrected first two PASS remain
preserved on their own older source/binary proofs. A draft syntax failure and an
offline audit's incorrect iCCP-only assumption are also retained as tooling errors;
the actual PNGs contain valid sRGB chunks. No new product defect is inferred.

Final attested source is
`21c484100e3f69c2fd6858e110f84962c567176b02f0766c262d8feb1175d2fe`,
binary `3b61161fd35a1b6588dead8ad9bb9f36d5ed6cdcc293cb4c600fe489dcee0034`.
The12 reports contain39 PASS and3 SKIPPED across42 rows:40 official and two
supplemental,38 actual children (28 Cocoa/Metal and10 offscreen/software), one
global integrity result and three policy SKIPs without child execution. VIS003
passes3software+3native repeats, three native mixed runs and one software mixed
run; CRP010 passes3native repeats and three native mixed runs. Native MAC001
also passes once. All child exits, source/binary/bundle provenance, original SHA
and catalog guards pass. Independent audit reopens38 owned catalog copies and
checks371 original paths, plus all four watermark ownership/PID/nonce records.

The existing hosted/software route cannot meet MAC001/CRP010 native contracts.
Unsupported-only --runs3 requests each emit one truthful SKIPPED result, with
zero fixtures/children and no fabricated actual backend. Mixed software retains
six actual PASS and one CRP010 SKIP. All three raw unsupported reports stay
INCOMPLETE, CLI exit1 and NOT_QUALIFIED; the supervisor's successful classification
check does not turn them into case PASS. Six native dedicated profiles contain75
distinct sources, giving exact87/97/75 unions for SEL002/IMP009/global integrity;
software excludes the one Crop fixture and has86 sources/74 profile paths.

Full build, format and three focused tidy commands exit0; advisory diagnostics
remain in the logs, including optional accesses guarded by the throwing test
require helper. CTest has49 PASS and one optional private Nikon SKIP out of50,
94.42s. The combined Python infrastructure suite has122 PASS. Both workflows now
run VIS003 three times and retain its JSON/label/active-export artifacts. The
ordinary CI time budget is60 minutes because the preceding successful job used
about49; this is not a performance threshold. Only documentation changed after
this attested cohort, with the original proof retained separately.

No load/performance/soak/chaos workloads ran. This focused cohort is not a new full
adapter sweep, a completed phase 0.5 or release qualification. Work remains in
draft PR47; stable public download is0.4.14 and source scope is0.5.1. No release,
tag, main merge, production publication or phase0.6 begins at this checkpoint.


## Transparent-edge handles and official partial-render race checkpoint

LUMA-UX-030 is reproduced and fixed: an existing visible Radial radius handle
rejects input over a transparent Transform edge. The original native FAIL remains
in `transparent-handle-initial-reproduction-native-report`; the first fixed PASS
remains in `transparent-handle-fixed-v2-native-report`. See UX_DEFECTS.md for the
independent geometry/pixel/data oracles and the unchanged ordinary-input limits.
The following `transparent-ready-regression` cohort has **45 PASS**: 31 native
and 14 software, covering transparent/normal/thin handles, affected Image-node
captures and local controls. It does not qualify the full UX-MSK-023 matrix.

Image-node captures now use `readyItemGrab`: actual QQuickItemGrabResult `ready`
acknowledgment precedes pixel access. A failed pending capture stays owned until
Context engine/window teardown; another readback is refused. Existing binding,
geometry and pixel assertions remain unchanged. This is test infrastructure,
not a production renderer defect. The original missing-include build failure
executed no app children and remains separate from the original product FAIL.

UX-RACE-007 is now an official bounded adapter. A genuine OLD first converted
RGBA row stays held across an actual UI batch commit. Literal independent row
controls reject the wrong source/revision, and current NEW Image Ready/pixels
must be observed before OLD release. The actual cancellation terminal is checked;
all audited runs observed Aborted, not Returned. This demonstrates the declared
partial-render interleaving, not simultaneous OLD CPU instructions during COMMIT.
Pure CPU-row equivalence and shipping-symbol isolation are separate CTest checks.
The UX-only observer is excluded from the shipping renderer path.

Final attested source:
`4a8154f13e200998362fba1fdbb30a7b5da9e23fe5c572640e64bb9bc87a61a3`;
binary: `8d0a08fbfb3ce0690ef33a0e4e866e9c82d2a050b0e9f5e9f4d82417ef1c34f9`.
The eight `race007-official-final-*report` reports contain **41 PASS**: 37 official
results and four supplemental results; 40 actual children (29 Cocoa/Metal and
11 local offscreen/software) plus one global integrity result. RACE007 passed
three dedicated native and three software repeats, three native mixed runs and
one software mixed run. The legacy candidate passed once in software; transparent
handles passed three times natively. Provenance, original SHA and catalog guards
pass. An independent read-only audit rechecked 11 actual RACE children, 187 PNGs
and 55 complete SQL snapshots. NEW references share production CPU/ICC logic;
this is not independent color-science, physical-scanout or native-chooser proof.

The full build passed. The original 54-test CTest invocation has **52 PASS,
one timeout and one optional private Nikon SKIP**; it remains FAIL in
`race007-official-final-FAILURE.json` and its original LastTest log is preserved.
`synthetic-raw-restart` printed its success result but exceeded the CTest wall
limit while the recorded session entered Clamshell Sleep. Three restart tests
overlap the documented sleep/wake periods. A separately attested focused rerun
of exactly those three passed: synthetic 1.78 s, complete-mask 14.31 s and
local-mask 6.78 s. This does not rewrite the original full invocation as green.
Format and nine focused clang-tidy commands passed in
`race007-static-resume-SUMMARY.json`; advisory warnings remain recorded.
The Python infrastructure suite has 141 PASS. Both workflows run official
RACE007 three times and retain its actual-row/binding/pixel artifacts.

A separate native Cancel experiment observed one real NSOpenPanel AX Cancel:
rejected1, accepted0, closed panel, no selected file, unchanged logical catalog.
Its original run still FAILs on natural focus return and never reaches its full
positive control. It does not qualify NAV004 or establish a product focus defect.
The user-deferred manual APP017/SLD009 return check remains unqualified.

The resulting inventory is **369 adapters: 257 bounded and 112 deferred**,
plus global integrity. Of 445 through-phase scenarios, conditional MSK005 is
absent; 74 applicable scenarios remain unadapted: **23 functional and 51 deferred**.
No load/stress/soak/performance work ran in these cohorts. Later Graphify setup
and this documentation change do not receive the older attested source hash.
Phase0.5 and the stable-release quality gate remain incomplete; no tag, main
merge, production publication or phase0.6 work is authorized by these results.

## APP016 candidate, 2026-10-20 — native result pending

The new supplemental `CANDIDATE-APP016-MINIMIZED-EXPORT` compiles with a test-only
AppKit lifecycle observer. Its bounded contract has two literal 64x48 PNGs,
genuine first-output completion held while minimizing, real second-output
completion while minimized, then natural restore and data/history checks.
Production behavior and the official adapter inventory are unchanged.

The initial `app016-first-native-report` is FAIL: the standard minimize button's
`performClick` route did not satisfy the actual native/Qt minimization predicate.
That run reached a genuine committed first output, but it did not establish the
required background lifecycle boundary. It is not a demonstrated product defect.
The input driver now uses NSWindow's public `performMiniaturize` action and
records the native state on scenario failure. The subsequent
`app016-native-action-first-report` is also FAIL, at initial window activation;
the actual session was observed with `IOConsoleLocked=true`. It never executed
the candidate and gives no evidence about the revised minimize action.

Both UX-target builds succeeded. On the revised source, format and three focused
clang-tidy commands exit0; existing header advisories remain in the logs.
An independent Pillow read verified every pixel of both literal fixture PNGs.
Five affected local offscreen/software cases (LIB004, LIB005, RACE002, RACE007,
VIS003) pass in `app016-integration-software-report`, with original SHA,
catalog and provenance guards. Their attested source is
`5f3aa86477235b5bcc1015e3b73d094fedf81a2ab8cc196eb0afbc46a09e37da`;
binary `8babd7bb2260eff74ace7205d6ffc9526701ebe9cc530dd95a0954831e9ab9b1`.
This later documentation does not inherit that source hash.

APP016 remains among the 23 unadapted functional scenarios: no official
registration, native PASS, new product defect or release acceptance is claimed.
The separately deferred APP017/SLD009 manual focus-return check is unchanged.

### APP016 unlocked diagnostics

After the user unlocked the Mac, `app016-unlocked-first-report` failed before
export because the visible case window was no longer active. This report does
not establish why focus was lost. The user then confirmed a free input interval.
`app016-free-first-report` reached a key, active native window and a genuine first
output, with no intervening application focus notifications, but timed out waiting
for actual minimization. `app016-eventloop-first-report` also failed at that
boundary despite using a bounded QEventLoop/NSApplication event loop. That
experiment was removed; the original state assertions remain unchanged.

Independent owned AppKit-only windows, with no Qt or Luma production code,
reproduced the minimization timeout through `performMiniaturize`, both when
spawned directly and when packaged/launched through Launch Services. A separate
`miniaturize` probe also timed out. Each ran an actual NSApplication main loop,
reached an active key window and received no DidMiniaturize. Their retained
`app016-cocoa-*-result.json` artifacts are diagnostics, not application test PASS.
A one-second read-only Dock sample shows a waiting run loop, not proof of a hung
Dock. No system process or security/appearance setting was changed.

The added lifecycle observer records native focus notifications, frontmost bundle
and PID, and NSApplication running/active state. One intermediate diagnostic
build failed on a GNU conditional extension under Werror; the explicit standard
conditional build passed. APP016 remains unqualified; no product fix or native
background-export success is inferred from these diagnostic runs.

## Minimum-window keyboard workflow, 2026-10-20

Two additional production defects were reproduced in both local software and
Cocoa/Metal: at 1000×640 the partially visible PhotoCard was excluded from Tab,
then after that fix the lower Export controls gained focus while clipped.
LUMA-UX-031 retains `keyboard-workflow-minimum-first-software-report` and
`keyboard-workflow-minimum-original-native-report`; LUMA-UX-032 retains
`keyboard-workflow-card-fixed-software-report` and
`keyboard-workflow-card-fixed-native-report`. All four original results are FAIL.

The fixes retain keyboard eligibility when the card's center is inside the
vertical viewport, and apply existing focus visibility scrolling to Export.
The first complete fixed workflow passes once on each backend in
`keyboard-workflow-fixed-first-software-report` and
`keyboard-workflow-fixed-first-native-report`. Each uses one 256×192 PNG and actual
keyboard Import→selection→Develop→0.01 EV edit→PNG export, with zero pointer events,
exact durable document/UUID/selection/source SHA and output pixels. The native
file chooser result is a declared fixture input, so this is supplemental, not full
KEY015 or a new official adapter. Geometry setup is not an OS resize test.

The earlier `keyboard-workflow-first-software-report` is a retained harness FAIL:
its QImage equality included color-space metadata in the pixel comparison.
The reference now explicitly targets sRGB, independent of the display profile;
sample bytes and color-space declaration are checked separately. Independent
Pillow readback confirms that the original output already has exactly the correct
sample bytes. Both fixed outputs also match exactly and declare PNG sRGB intent 1.
The readback audit retains its two incorrect intermediate assertions (requiring
iCCP, then intent 0); neither is a product failure. Expected processing shares the
CPU renderer and does not independently qualify color science.

The first fixed attestation is source
`5abe6a78ccc039916788accdc53db07c5a4b6006bd2ef11aee932c8e362c0b21`,
binary `b50c6a3ba37c0548238b2f7a4a8dce231bf76ad6593ee8c56092f051ab14d334`.
Subsequent workflow/documentation changes do not inherit that source hash.
Both CI workflows now include this bounded supplemental regression.
The prior `97413a2` hosted run 39399309108 completed successfully, including
clang-tidy; it does not attest these newer changes.

### Repeated keyboard qualification

The frozen `keyboard-workflow-final` cohort passes **26 UX results: 13 native
Cocoa/Metal DPR2 and 13 local offscreen/software**. Each backend has three complete
minimum-window workflow repeats, both existing selected-asset keyboard variants,
and KEY008/009/010/016/017, EXP002/004 and DSC001. All children exit0, with source
consistency, build provenance, original SHA and catalog integrity passing.
The independent `keyboard-workflow-final-forensic-audit.json` validates all 26
guards plus exact decoded PNG samples, a valid sRGB declaration, pointer absence,
lower-control visibility and SQLite integrity in all six new workflow repeats.

Full application build and format pass. Functional CTest has **53 PASS and one
optional Nikon D7500 SKIP**, no failures, 95.51 seconds. Python infrastructure has
**141 PASS**. Three focused clang-tidy commands exit0; a new widening-conversion
advisory in the test's QImage row stride was then corrected with explicit
`qsizetype` arithmetic. Existing shared-header advisories remain recorded.
The follow-up checks after that test-only type change are recorded separately.

This cohort's attestation is source
`a76af48d5f24a173b45d6078f75b6e933cea0f77f7950dc165c9275dc0a87faa`,
binary `b50c6a3ba37c0548238b2f7a4a8dce231bf76ad6593ee8c56092f051ab14d334`.
The explicit stride type and this later documentation do not inherit that hash.
The official inventory remains 369 adapters / 257 bounded / 112 deferred, with 23
unadapted functional scenarios. Full phase 0.5 and stable release remain incomplete.

After the explicit stride-type correction, `keyboard-workflow-post-tidy` adds
three Cocoa/Metal and three local offscreen/software PASS. Each child exits 0 and
passes source/provenance/original/catalog guards. Format and focused clang-tidy
pass, with no remaining warning in KeyboardActivationCandidate.cpp. The full
53 PASS/one SKIP CTest cohort above is not relabeled as a run of this later binary.
Follow-up attestation: source
`c103a0dec6d0064033020ab2f7cc8bd94a9bd4246ffa2955617b59b31bdcbc64`,
binary `34cbc4877ebeb4f4ee4f28b223da21870d369fcb9a846f89be3f258bfec5f64f`.
This final results paragraph is later documentation. No stable release, main
merge, version tag, load workload or phase 0.6 transition was performed.

## APP016 UI comparison and manual probe, 2026-10-20

The user reports that ordinary Finder/Safari windows minimize normally.
Two owned AppKit UI probes still timed out: one used the native UI tool's
minimize accessibility control, the other screenshot-grounded button input.
The screenshots show a purple control indicator replacing the traffic lights
after UI control begins; this is an observation, not an established cause.
Separate probes without UI-tool attachment also failed after three seconds of
startup stabilization, both with and without a standard application/Window menu.
Their windows were key/main, miniaturizable, on the active Space, with an enabled
visible minimize button and default presentation options. No DidMiniaturize came.

After the user confirmed readiness for a manual click, the owned
`App016HumanProbe.app` waited 180 seconds and timed out without Will/DidMiniaturize
or DidDeminiaturize. The retained result is
`app016-human-result-20261020T105026Z.json`. Whether the user actually clicked
remains unconfirmed at this checkpoint; this is not evidence of a failed
human-input action. No Luma product defect, globally broken Dock, native lifecycle
PASS or release qualification follows. The separately deferred APP017/SLD009
manual check is unchanged.

The user's subsequent answer confirms that the diagnostic window minimized.
This is a positive human observation, retained separately from the instrumented
timeout; the original report remains unchanged. The discrepancy between the
visible result and the probe's notification/state log is unresolved. It does
not qualify the complete Luma APP016 export/minimize/restore scenario or justify
attributing the timeout to a Luma defect. No repeat manual action is requested.

## Localized preset dialogs, 2026-10-20

The bounded supplemental dialog slice reproduced LUMA-UX-033/034 in local
offscreen/software and Cocoa/Metal. Both original footer-overlap reports and both
intermediate Armenian-caption failures remain retained. The first draft also
clicked Copy before scrolling the sidebar; that timeout is a harness error.
A later body-allocation-only heuristic was too strict and was replaced by actual
footer overlap plus real ancestor clipping, without weakening text/visibility
assertions. Neither harness correction counts as a production defect.

The dialog now bounds its height, scrolls its content, reveals focused descendants
and wraps the long capture caption. First complete verification:
`localized-dialog-wrap-software-report` and `localized-dialog-wrap-native-report`,
one PASS each. Every repeat covers eight languages and four scene families with
256 explicit text bindings, 32 screenshots, genuine Tab/Space/wheel/Cancel input,
unchanged preset bytes/source SHA/full documents/revisions/metadata/selection/
history and exact retained preview pixels. Expected strings come from the
declared language resource; this does not qualify linguistic correctness.

The official inventory stays 369 adapters / 257 bounded / 112 deferred, including 23
unadapted functional scenarios. VIS007/VIS008 require further surfaces; this
finite slice is not promoted to either official ID. Full phase 0.5 and stable
release remain incomplete. Both workflows now run three supplemental dialog
repeats and retain their trace, binding ledger and screenshots.

The `localized-dialog-final` cohort passes 24 UX results: 12 Cocoa/Metal and 12
local offscreen/software. Each backend runs the new dialog slice three times,
the keyboard import/edit/export workflow once, and eight affected preset,
localization and focus scenarios once. Every child exits 0; source/provenance,
original SHA and catalog checks pass. The six dialog ledgers contain 1,536
explicit text observations and 192 screenshots. Independent report auditing
confirmed exact locale/binding counts; its first attempt misread the
`source_consistency` string as an object and raised TypeError, then was corrected.
That is an audit-script error, not a product or scenario failure.

Full application build, 53 functional CTest PASS plus one optional Nikon SKIP,
141 Python infrastructure tests and format pass. CTest took 93.96 seconds with
no failures. Two focused clang-tidy commands exit 0; new optional-access warnings
in the supplemental adapter were subsequently corrected with an explicit
missing-asset branch. Existing shared-header advisories remain retained.
Attestation: source
`4fe4d2344fe85ba6b10e00a1829979e436c1a85438324044ef0fd284fe43d186`,
binary `be1d925793a81d9cd78fe34c74465c5e8c330d4929c7a2d75f136e99252c56f3`.

The subsequent test-only strengthening uses a genuine non-neutral Exposure edit,
an independent expected preview and final real Undo/Redo with complete durable
documents. Its verification is recorded separately, not attributed to the
earlier cohort or binary. Hosted CI run 39407429384 for preceding commit
`b9a089a9681bf29ee6392b9f34ea240a69283672` completed successfully; it does not
cover these later dialog changes.

The strengthened `localized-dialog-history-final` adapter passes three
Cocoa/Metal and three local offscreen/software repeats. Every child exits 0
with passing source/provenance/original/catalog guards. Independent artifact
auditing confirms 256 distinct bindings, 32 screenshots, a positive Exposure
edit and completed real Undo/Redo in each of the six runs. Format passes;
focused clang-tidy exits 0 with no warning in the new adapter (existing shared
header advisories remain). Follow-up attestation: source
`a895295d07dc3f986b154c931fde47c9b39b3a726e46214fe84d60ae7ff80f56`,
binary `20a199e309ce9e10a66fa203bf77212691311862e0a3534b40b5758c670d8837`.
The full CTest cohort above is not relabeled as a run of this later test binary.
This results paragraph is later documentation. The combined cohorts have
30 UX PASS; official inventory counts and the incomplete stable release gate
remain unchanged.
