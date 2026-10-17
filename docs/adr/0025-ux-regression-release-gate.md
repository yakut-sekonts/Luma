# 0025 — UX regression as a release gate

Issue #46 applies the human-supplied UX specification to actual capabilities,
starting with source phase 0.5. Production fixes require a failing reproducer
first. New phase functionality is published only after its tests and fixes pass.

Keep UX infrastructure under tests/ux, separate from production controllers.
Load the real Main.qml and the real controllers in a Qt Test executable; inject
mouse, wheel, keyboard and lifecycle events into QQuickWindow. Fixture setup may
use catalog/service APIs, but interaction acceptance must use real input. Add
stable objectName selectors without altering behavior. Match delegates by asset
identity, not localized text or recycled indexes.

Collect main-loop heartbeats, frame intervals, input timings and RSS, with a
parent-process watchdog for a permanently blocked main thread. Use condition
waits with explicit deadlines. Every result has machine-readable evidence;
failures retain the first failing screenshot/log/actions/catalog for public data.
Seeded chaos stores exact replay actions. Do not retry failures invisibly.

The Python runner reads a checked-in capability manifest and complete scenario
inventory. Missing future features are NOT_IMPLEMENTED, unsupported external
hardware/OS interactions explicitly SKIPPED. Unexecuted implemented blocking
checks cannot count as PASS or satisfy publication. Local performance is measured
on a recorded Mac fingerprint, never inferred from hosted CI timings. Three clean
blocking repetitions, zero applicable P0/P1 failures, original hashes and SQLite
integrity checks gate release. Keep long stress/soak selectable outside normal CI.

Persist reports and roadmap gaps by subsystem. Phase-aware commands retain all
previous blocking checks. Published artifacts must derive from the tested source
and pass standalone/installer/provenance/signature verification. Do not equate an
ad-hoc signed prerelease with Developer ID/notarized 1.0 qualification.
