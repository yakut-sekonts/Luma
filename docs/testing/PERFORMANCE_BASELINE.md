# Performance baseline records

`tests/ux/performance-baseline.schema.json` defines version 1 of a measured baseline
candidate using JSON Schema Draft 2020-12. Validation uses the pinned `jsonschema`
package in the repository toolchain virtual environment. The schema has no remote
references and validation does not fetch network resources.

Each record retains raw samples, nearest-rank p50/p95/p99, hardware and rendering
configuration, fixture identity, source/binary hashes and hashes of the original
evidence. The validator rejects non-finite values, mismatched statistics, missing
provenance and unknown fields. Comparisons require the same case, measurement,
hardware and workload. Changed source versions remain distinguishable in provenance.
Observed OS memory pressure stays in the hashed measurement evidence; review those
conditions before accepting a candidate as a regression reference.

Run the bounded format contract check:

```sh
.tools/venv/bin/python tests/ux/performance_baseline.py
```

The official UX-P09-PERF-001 adapter checks the checked-in schema and rejection
contracts. Its synthetic examples are explicitly not measured performance. This
check alone does not qualify latency, throughput, hardware or the final release.

Import actual PRG001 results after three complete successful repetitions:

```sh
.tools/venv/bin/python tests/ux/performance_baseline.py \
  --progressive-report build-verify/ux-progressive-parameter-cache/UX_REPORT.json \
  --output build-verify/clarity-baseline-candidate.json
```

Import refuses failed or incomplete runs, source/provenance/integrity failures,
incompatible rendering configurations, differing fixtures and invalid samples.
The output is created exclusively: an existing baseline is never overwritten.
The measurement is input to a synchronized frame queued for presentation; physical
display scanout and the subsequent image readback are not included. `candidate`
never means release approval. Other measurement adapters still require their own
actual measurements and acceptance thresholds.

Validator reference: [jsonschema validation documentation](https://python-jsonschema.readthedocs.io/en/stable/validate/).
