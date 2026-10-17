# Native UX regression

This separate Qt Test executable loads production Main.qml and the real controllers.
It sends real clicks, drags, keys and ordinary wheel events. A native file-panel
selection may be prepared as a fixture boundary; this does not qualify the native
file chooser itself. It never opens private photographs or the user's real catalog.

Build with the pinned toolchain:

```sh
.tools/venv/bin/python tests/ux/build_ux.py --build build-verify
.tools/venv/bin/cmake --build build-verify --target luma_ux_metric_tests --parallel 3
.tools/venv/bin/ctest --test-dir build-verify --output-on-failure
.tools/venv/bin/python tests/ux/run_ux_suite.py --release-qualification
```

All native UI runs share an exclusive screen/focus lock. Do not interact with the
test window or run other native UI suites concurrently. Use a focused selection
while investigating a reproducer:

```sh
.tools/venv/bin/python tests/ux/run_ux_suite.py --case UX-PRV-013 --runs 3
```

Release qualification repeats blocking scenarios three times and runs other scenarios
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
an interaction FPS baseline. Trackpad/momentum, soak, seeded chaos/replay and tests
of packaged app bundles still need executable adapters. Unsupported --app/--replay
and chaos requests are rejected, never silently ignored. Reports keep this work
separate from missing product capabilities.

The macOS and release workflows run every applicable blocking adapter three times
and retain generated-fixture evidence. This hosted CI regression gate supplements
the complete local qualification; it does not replace performance/soak/hardware
coverage or claim the current unadapted scenarios passed.

Updater tests use the public native SDK installation-callback boundary with a
non-installing probe, real controller readiness and real QML operations. They do
not download/install Sparkle packages. The Cmd+O scenario checks chooser opening
in Library and Develop; direct reject is fixture cleanup, not native cancel-input
or file-selection qualification. Copy crash recovery uses an intentional process
kill after publishing a complete file but before its catalog commit, followed by
the real LibraryModel startup recovery in a distinct process.
