"""Compare compiled native predictions with the pinned adapted source networks.

This synthetic numerical check does not qualify photographic quality, runtime
tiling, device placement, fallback behavior or installer/update integration.
Run with .tools/model-venv/bin/python after building the bundled models.
"""
import argparse
import gc
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import runpy
import subprocess
import sys
import tempfile

from model_assets import ROOT, digest, read_sources, verify_artifacts


def samples(size, role):
    import numpy as np
    y, x = np.mgrid[:size, :size].astype(np.float32) / (size - 1)
    gradient = np.stack((x, y, (x + y) / 2))[None]
    noise = np.random.default_rng(2301).normal(0, .025, gradient.shape).astype(np.float32)
    yield "gradient-noise", np.clip(gradient + noise, 0, 1)
    yield "black", np.zeros_like(gradient)
    if role == "denoise":
        extended = gradient.copy()
        extended[0, 0, :16, :] = -.25
        extended[0, 2, -16:, :] = 1.5
        yield "negative-hdr", extended
        yield "uniform-near-black", np.full_like(gradient, .0001)
        yield "uniform-gray", np.full_like(gradient, .18)
        yield "uniform-color", np.broadcast_to(
            np.array([.15, .4, .8], dtype=np.float32).reshape(1, 3, 1, 1), gradient.shape).copy()
        yield "near-black-noise", np.clip(gradient * .001 + noise * .001, 0, 1)
        impulse = np.zeros_like(gradient)
        impulse[0, :, size // 2, size // 2] = .001
        yield "black-with-impulse", impulse
    else:
        yield "white", np.ones_like(gradient)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--cache", type=Path, default=ROOT / ".tools/model-sources")
    parser.add_argument("--compute-units", choices=("cpu", "cpu-and-gpu", "all"), default="cpu")
    args = parser.parse_args()
    if args.report.exists():
        raise SystemExit("Report already exists; choose a fresh evidence path.")
    manifest = verify_artifacts(args.artifacts)
    import numpy as np
    import torch
    conversion = runpy.run_path(str(ROOT / "scripts/convert-models.py"))
    quality_source = ROOT / "scripts/check-denoise-quality.py"
    quality = runpy.run_path(str(quality_source))
    torch.set_num_threads(2)
    torch.manual_seed(0)
    # Fixed absolute tolerances on normalized Float32 outputs. Never adjust
    # these automatically from observed results or silently use FP16 fallback.
    limits = {"max_absolute_error": .0005, "rmse": .00005}
    units = {"cpu": "CPU_ONLY", "cpu-and-gpu": "CPU_AND_GPU", "all": "ALL"}[args.compute_units]
    report = {"schema": 1, "scope": "synthetic-adapted-source-vs-coreml-native",
              "status": "RUNNING", "photographic_quality": "NOT_RUN",
              "python": platform.python_version(), "platform": platform.platform(),
              "machine": platform.machine(), "compute_units": units,
              "manifest_sha256": digest(args.artifacts / "manifest.json"),
              "recipe_sha256": manifest["recipe_sha256"], "limits": limits, "cases": []}
    report["quality_validator_sha256"] = digest(quality_source)
    temporary = tempfile.TemporaryDirectory(prefix="luma-model-numerics-")
    work = Path(temporary.name)
    try:
        report["xcode"] = conversion["require_xcode"]()
        report["toolchain"] = {name: importlib.metadata.version(name) for name in manifest["toolchain"]}
        if report["toolchain"] != manifest["toolchain"]:
            raise ValueError("Numerical comparison requires the same pinned model environment as conversion.")
        bridge_source = ROOT / "tests/models/CoreMLPrediction.mm"
        bridge = work / "coreml-prediction"
        subprocess.run(["xcrun", "clang++", "-std=c++20", "-fobjc-arc", "-O2",
                        "-Wall", "-Wextra", "-Werror", "-framework", "Foundation",
                        "-framework", "CoreML", str(bridge_source), "-o", str(bridge)],
                       check=True, timeout=120)
        report["prediction_bridge_sha256"] = digest(bridge_source)
        report["prediction_api"] = "native-MLModel-owned-MLMultiArray"
        for model in read_sources()["models"]:
            weights = args.cache / model["source"]["filename"]
            if (weights.is_symlink() or weights.stat().st_size != model["source"]["size"]
                    or digest(weights) != model["source"]["sha256"]):
                raise ValueError("Source weights differ from the pinned model.")
            factory = conversion["denoise_network" if model["role"] == "denoise" else "sky_network"]
            network = factory(weights)
            quality_cases = ({name: (noisy, clean, mode) for name, noisy, clean, mode
                              in quality["samples"](model["size"])}
                             if model["role"] == "denoise" else {})
            for name, sample in samples(model["size"], model["role"]):
                print(model["id"] + ": " + name, flush=True)
                with torch.inference_mode():
                    reference = network(torch.from_numpy(sample)).numpy()
                sample.tofile(work / "input.bin")
                result = subprocess.run([str(bridge), str((args.artifacts / model["bundle"]).resolve()),
                    str(work / "input.bin"), str(work / "output.bin"), model["input"], model["output"],
                    str(model["size"]), "3" if model["role"] == "denoise" else "1", args.compute_units],
                    capture_output=True, text=True, timeout=180)
                if result.returncode:
                    raise RuntimeError(f"Native prediction exited {result.returncode}: {result.stderr}")
                actual = np.fromfile(work / "output.bin", dtype=np.float32).reshape(reference.shape)
                if (actual.shape != reference.shape or actual.dtype != np.float32
                        or not np.isfinite(actual).all() or not np.isfinite(reference).all()):
                    raise ValueError("Invalid prediction shape, dtype or finite-value contract.")
                error = actual.astype(np.float64) - reference.astype(np.float64)
                maximum, rmse = float(np.abs(error).max()), float(np.sqrt(np.mean(error * error)))
                passthrough = True
                uniform_passthrough = None
                native_quality = None
                if model["role"] == "denoise":
                    outside = np.broadcast_to(((sample < 0) | (sample > 1)).any(axis=1, keepdims=True), sample.shape)
                    passthrough = bool(np.array_equal(actual[outside], sample[outside]))
                    if np.array_equal(sample, np.broadcast_to(sample[:, :, :1, :1], sample.shape)):
                        uniform_passthrough = bool(np.array_equal(actual, sample))
                        passthrough = passthrough and uniform_passthrough
                    quality_name = "uniform-black" if name == "black" else name
                    if quality_name in quality_cases:
                        noisy, clean, mode = quality_cases[quality_name]
                        if not np.array_equal(sample, noisy):
                            raise ValueError("Numerical and analytical quality inputs differ.")
                        native_quality = quality["evaluate"](sample, clean, actual, mode, model["halo"])
                else:
                    passthrough = bool((actual >= 0).all() and (actual <= 1).all())
                report["cases"].append({"model": model["id"], "sample": name,
                    "input_sha256": hashlib.sha256(sample.tobytes()).hexdigest(),
                    "output_shape": list(actual.shape), "max_absolute_error": maximum, "rmse": rmse,
                    "domain_contract": passthrough, "uniform_input_passthrough": uniform_passthrough,
                    "analytical_quality": native_quality,
                    "status": "PASS" if maximum <= limits["max_absolute_error"]
                    and rmse <= limits["rmse"] and passthrough
                    and (native_quality is None or native_quality["status"] == "PASS") else "FAIL"})
            del network
            gc.collect()
        report["status"] = "PASS" if all(case["status"] == "PASS" for case in report["cases"]) else "FAIL"
    except Exception as error:
        report["status"] = "FAIL"
        report["error"] = str(error)
    finally:
        temporary.cleanup()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open("x") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(report["status"] + ": " + str(args.report))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
