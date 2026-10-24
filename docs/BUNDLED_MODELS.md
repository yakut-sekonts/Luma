# Bundled offline model delivery

Decision: the user requested installer-bundled models on 2026-10-21. This is a
source implementation under final qualification. Bundle v4 compiled on
2026-10-21 with Xcode 27.1 and passed synthetic source-quality and native CPU/ALL
comparisons. Complete photographic, runtime and distribution qualification remain open.
No installer was published. Stable remains 0.5.1.

The bounded v4 runtime campaign passes six noisy photographic cases and nine
source/native tiled comparisons. See the scope and remaining gaps below; this
does not qualify real sensor noise or the complete 1.0 release.

## Runtime

The app resolves `Contents/Resources/Models/LumaDenoise.mlmodelc` and
`LumaSky.mlmodelc` relative to its own executable. The first launch needs neither
a download nor manual model configuration. Models and photographs stay local.
Settings exposes a custom compiled model override and a return to the included
model, translated into all eight languages. Only custom paths are persisted;
moving the app or installing a Sparkle update resolves bundled paths again.

Missing resources disable the corresponding feature. Present but invalid models
fail explicitly on the worker; no fallback download or fabricated result is used.
Prediction falls back from available compute units to CPU on supported failures.

## Sources and adaptation

`resources/models/sources.json` pins both upstream revisions, source bytes,
license hashes and model contracts. `resources/models/NOTICE.md` documents
upstream attribution and Luma's changes. Both notices and full upstream licenses
travel with the compiled artifacts.

- DRUNet color: MIT, 512×512 Float32 tensors
  and a 64-pixel halo. The sRGB-trained model is wrapped by linear↔sRGB conversion.
  Any RGB pixel outside [0,1] passes through unchanged; no RAW-trained quality
  claim is made. The adaptation preserves spatially uniform RGB tiles exactly and
  uses checkpoint-equivalent spatial slicing/1×1 convolution to avoid the
  observed native stride-2 convolution error. Non-overlapping upsampling is
  expressed as 1×1 convolution/PixelShuffle with the original coefficients.
  The fourth input channel carries a per-tile sRGB Gaussian-noise estimate from
  the lower median of absolute orthonormal Haar HH coefficients divided by
  0.6744897501960817. Bundle v4 replaces v3's mean-based estimate: sparse
  high-frequency texture/edge outliers no longer dominate the noise level.
  The lower median uses topk/amax for Core ML export. Dense texture and actual
  sensor noise still require qualification; no brightness/noise floor is added.
  Model ID `drunet-color-haar-mad-v1` is recorded with denoised DNG provenance;
  original weights are unchanged. The prior NAFNet candidate was retired after
  it failed the fixed dark-input quality and numerical gates.
- U²-Net Sky: MIT, the author's ONNX model from the pinned attributed mirror,
  320×320 sRGB input with ImageNet normalization and the first prediction output.
  No per-image min/max stretching is applied to the probability map.

## Final build preparation only

On Apple Silicon with Xcode and Python 3.12, run:

```sh
bash scripts/build-bundled-models.sh build/models
```

The recipe uses pinned direct conversion dependencies in a separate build-only
virtual environment. All resolved package versions, Python and Xcode versions
are recorded in the generated manifest. Python packages do not ship in the app.
PyTorch 2.10 uses `weights_only=True` for the hash-pinned checkpoint; arbitrary
checkpoint objects and upstream training code are not loaded. The ONNX adapter
refuses external initializer files and nonstandard operator domains.

The recipe downloads exact-sized, SHA-256-pinned source weights into ignored
`.tools/model-sources`, converts Float32 ML Programs for macOS 14+, invokes the
Core ML compiler and atomically installs a complete artifact directory. Changed
weights/recipe/license files require rebuilding models. Existing outputs are
never silently overwritten; use a fresh output directory for a new recipe.

CMake's default build installs the verified artifact directory into `Luma.app`.
Configuring version 1.0 or later without model artifacts fails. `package.sh`
requires both models and copies them before signing; `BUILD_INFO.json` records
their identities and manifest hash. A custom artifact root can be selected with
the CMake cache setting and packaging environment variable
`LUMA_BUNDLED_MODELS_DIR`; both must point to the same recipe's output.

The release workflow builds models before configuring the app. ZIP publication
checks the pinned recipe, complete file inventory and hashes without extraction.
Models are signed as application resources and included in both the DMG's app
and Sparkle ZIP; there is no independently downloaded model/update channel.

## Qualification

The conversion recipe and Core ML compilation have run. The v4 synthetic
CPU and ALL comparisons pass for both models; see
[the qualification record](FINAL_1_0_READINESS.md#completed-baseline-and-current-blockers).
In particular, coremltools 9.0 documents
testing with an earlier PyTorch version than this recipe's patched 2.10 loader;
conversion accuracy must be established during final preparation.

Run the numerical comparison with:

```sh
.tools/model-venv/bin/python scripts/check-model-conversion.py build/models --report build/model-numerics.json
```

This uses the adapted source networks and a standalone native Core ML CPU probe.
It checks eight Denoise inputs (including uniform colors, dark noise and an
isolated impulse) and three Sky inputs, finite Float32 tensor contracts, Sky
bounds, and exact Denoise passthrough for uniform tiles/out-of-range pixels.
Use `--compute-units all` or `--compute-units cpu-and-gpu` to check another Core
ML execution policy. The policy does not establish actual device placement.
The local v4 result is 11/11 PASS on CPU and 11/11 with ALL at unchanged limits.
Where analytical clean inputs exist, the native output must also pass the same
quality oracle as the source prediction. This prevents small numerical errors
from concealing relative quality regressions on dark inputs.
It is an additional CI/release gate, not photographic or tiling qualification. Native
prediction failures and signals fail the report; reports are never overwritten.
CI continues independent application checks after a failed comparison, then
requires its successful outcome in a final mandatory step. The release workflow
still stops immediately; neither path permits a failed model gate to pass.

Source quality has a separate mandatory gate:

```sh
.tools/model-venv/bin/python scripts/check-denoise-quality.py build/models --report build/denoise-quality.json
```

It compares predictions with analytical clean ramps/colors and a known impulse
corruption, independently of Core ML. For noisy inputs, squared error must
decrease and peak error must not exceed the input's peak error; noiseless uniform
colors must remain exact. Both the full tensor and the runtime's 64-pixel crop
must pass. These are fixed criteria, not limits fitted to the current model.
Identity, bright-output and detail-destroying predictors are explicitly rejected
by oracle tests. The v4 source result is **7/7 PASS**, including both previously
failing nonuniform dark cases and their interior crop. Historical v1/v2 failures
remain in the qualification record; they are not reclassified as passing.
CI preserves both reports and requires both outcomes; release packaging stops
on either failure.

The bounded photographic/runtime gate runs after building the native probe:

```sh
.tools/model-venv/bin/python scripts/check-denoise-photographs.py build/models --probe build/luma_denoise_runtime_probe --output build/denoise-photographs
```

It uses the three licensed portrait/landscape/night PNG fixtures with seeded
sRGB Gaussian noise at sigma 0.01 and 0.05. Each RGB channel must improve in
linear and sRGB squared error; gradient error must improve along both axes,
including actual tile seams. The production provider's reflected tiling is
compared with an independent NumPy/PyTorch assembly at the unchanged numerical
limits. Native contracts cover Amount 0/0.5/1, unchanged input/alpha, progress,
pre-cancel, mid-tile-loop cancellation and rejected invalid amounts. Six oracle
tests reject identity, bad color channels, hidden seams and destroyed detail.
Three unmodified photographs are observational controls, not noiseless sensor
ground truth or three extra quality passes. CI uploads reports/contact sheets
and requires this gate; release packaging stops on failure. A fresh output
directory is required to preserve prior evidence. This remains bounded evidence,
not acceptance of all camera noise, visual detail or full-resolution resource use.

Sky has a separate bounded photographic/runtime gate:

```sh
.tools/model-venv/bin/python -m unittest discover -s tests/models -p test_sky_photographs.py
.tools/model-venv/bin/python scripts/check-sky-photographs.py build/models --probe build/luma_sky_runtime_probe --output build/sky-photographs
```

`tests/assets/filters/sky-regions.json` records conservative day/night sky and
non-sky interiors, including an entire indoor portrait as a negative sample.
Regions were selected before prediction; unknown boundaries are not treated as
background. Source and native output must independently pass the fixed mean and
selected-area limits. Actual native RGBA preprocessing and uint8 output are
compared with independent PyTorch bilinear resize and the pinned source network.
The maximum-error bound is 0.0005 plus half a uint8 quantization step (0.5/255);
the separate Float32 conversion thresholds are unchanged. The probe also checks
progress, unchanged input, unsupported selections, invalid buffers, cancellation
and repeatability after cancellation. Seven oracle tests reject degenerate masks,
hidden false positives, weak confidence and invalid region/plane inputs.

The initial local result is 3/3 PASS for source quality, native quality and
runtime numerical agreement. CI requires this outcome; release packaging stops
on failure. Both workflows retain reports, logs and comparison sheets. A fresh
output directory prevents overwriting evidence. This qualifies only the annotated
interiors; it does not establish dense boundary accuracy or full scene quality.
See [Sky evidence](qualification/2026-10-22/sky-followup.json).

Require representative RAW/ordinary-image Sky scenes, denoise texture/color/noise
comparisons, HDR/negative-sample preservation, tiled edges, no-sky false positives,
orientation/crop alignment, CPU fallback, cancellation and 8/16 GB resource tests.
Verify first offline launch, app moves, custom override/reset, signed Applications
installation and Sparkle upgrade with both models. A generated model manifest
records `qualification: NOT_RUN`; model quality is established by separate final
release evidence, not conversion success. See [final readiness](FINAL_1_0_READINESS.md).
