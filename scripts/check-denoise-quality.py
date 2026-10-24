"""Reject destructive source-model predictions against analytical clean images.

This is a bounded synthetic quality gate, independent of Core ML agreement.
It does not qualify photographic detail/color, runtime tiling or model performance.
"""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import runpy
import sys

from model_assets import ROOT, digest, read_sources, verify_artifacts


def samples(size):
    import numpy as np
    y, x = np.mgrid[:size, :size].astype(np.float32) / (size - 1)
    ramp = np.stack((x, y, (x + y) / 2))[None]
    noise = np.random.default_rng(2301).normal(0, .025, ramp.shape).astype(np.float32)
    yield "gradient-noise", np.clip(ramp + noise, 0, 1), ramp, "denoise"
    dark = ramp * .001
    yield "near-black-noise", np.clip(dark + noise * .001, 0, 1), dark, "denoise"
    black = np.zeros_like(ramp)
    impulse = black.copy()
    impulse[0, :, size // 2, size // 2] = .001
    yield "black-with-impulse", impulse, black, "denoise"
    for name, color in (("uniform-black", (0, 0, 0)),
                        ("uniform-near-black", (.0001, .0001, .0001)),
                        ("uniform-gray", (.18, .18, .18)),
                        ("uniform-color", (.15, .4, .8))):
        clean = np.broadcast_to(np.array(color, dtype=np.float32).reshape(1, 3, 1, 1),
                                ramp.shape).copy()
        yield name, clean.copy(), clean, "preserve"


def evaluate(noisy, clean, actual, mode, halo):
    import numpy as np
    if (mode not in ("denoise", "preserve") or not isinstance(halo, int) or halo < 0
            or noisy.ndim != 4 or noisy.shape[:2] != (1, 3)
            or min(noisy.shape[2:]) <= 2 * halo):
        raise ValueError("Invalid synthetic case contract.")
    for tensor in (noisy, clean, actual):
        if (tensor.shape != noisy.shape or tensor.dtype != np.float32
                or not np.isfinite(tensor).all()):
            raise ValueError("Quality inputs/outputs must be matching finite Float32 RGB tensors.")
    regions = {}
    for name, margin in (("full", 0), ("runtime-crop", halo)):
        crop = (..., slice(margin, -margin), slice(margin, -margin)) if margin else (...,)
        reference = clean[crop].astype(np.float64)
        before = noisy[crop].astype(np.float64) - reference
        after = actual[crop].astype(np.float64) - reference
        input_mse, output_mse = float(np.mean(before * before)), float(np.mean(after * after))
        input_peak, output_peak = float(np.abs(before).max()), float(np.abs(after).max())
        criteria = ({"noise_error_reduced": output_mse < input_mse,
                     "peak_error_not_amplified": output_peak <= input_peak}
                    if mode == "denoise" else
                    {"noiseless_input_unchanged": bool(np.array_equal(actual[crop], clean[crop]))})
        regions[name] = {"margin": margin, "input_mse": input_mse, "output_mse": output_mse,
                         "input_peak_error": input_peak, "output_peak_error": output_peak,
                         "criteria": criteria,
                         "status": "PASS" if all(criteria.values()) else "FAIL"}
    return {"status": "PASS" if all(r["status"] == "PASS" for r in regions.values()) else "FAIL",
            "regions": regions, "input_max": float(noisy.max()), "output_max": float(actual.max()),
            "input_sha256": hashlib.sha256(noisy.tobytes()).hexdigest(),
            "clean_sha256": hashlib.sha256(clean.tobytes()).hexdigest(),
            "output_sha256": hashlib.sha256(actual.tobytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--cache", type=Path, default=ROOT / ".tools/model-sources")
    args = parser.parse_args()
    if args.report.exists():
        raise SystemExit("Report already exists; choose a fresh evidence path.")
    report = {"schema": 1, "scope": "synthetic-denoise-source-vs-analytical-clean",
              "status": "RUNNING", "photographic_quality": "NOT_RUN", "compute_units": "TORCH_CPU",
              "python": platform.python_version(), "platform": platform.platform(),
              "validator_sha256": digest(Path(__file__)), "cases": []}
    try:
        manifest = verify_artifacts(args.artifacts)
        report["manifest_sha256"] = digest(args.artifacts / "manifest.json")
        report["recipe_sha256"] = manifest["recipe_sha256"]
        report["toolchain"] = {name: importlib.metadata.version(name) for name in manifest["toolchain"]}
        if report["toolchain"] != manifest["toolchain"]:
            raise ValueError("Source quality requires the pinned conversion environment.")
        import torch
        torch.set_num_threads(2)
        torch.manual_seed(0)
        model = next(m for m in read_sources()["models"] if m["role"] == "denoise")
        weights = args.cache / model["source"]["filename"]
        if (weights.is_symlink() or weights.stat().st_size != model["source"]["size"]
                or digest(weights) != model["source"]["sha256"]):
            raise ValueError("Source weights differ from the pinned model.")
        report["model"] = model["id"]
        report["weights_sha256"] = model["source"]["sha256"]
        conversion = runpy.run_path(str(ROOT / "scripts/convert-models.py"))
        network = conversion["denoise_network"](weights)
        for name, noisy, clean, mode in samples(model["size"]):
            with torch.inference_mode():
                actual = network(torch.from_numpy(noisy)).numpy()
            result = {"sample": name, "mode": mode, **evaluate(noisy, clean, actual, mode, model["halo"])}
            report["cases"].append(result)
            print(name + ": " + result["status"], flush=True)
        report["status"] = "PASS" if report["cases"] and all(
            case["status"] == "PASS" for case in report["cases"]) else "FAIL"
    except Exception as error:
        report["status"] = "FAIL"
        report["error"] = str(error)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open("x") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(report["status"] + ": " + str(args.report))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
