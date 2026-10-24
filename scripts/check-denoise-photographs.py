"""Bounded photographic/runtime qualification, not a RAW or full release gate.

Uses licensed repository photographs as clean references with deterministic
added sRGB Gaussian noise. The real native provider is compared to independently
assembled reflected PyTorch tiles. A clean-reference control is observational:
these already-developed photographs are not noise-free sensor ground truth.
"""
import argparse
import importlib.metadata
import json
import math
from pathlib import Path
import platform
import runpy
import subprocess
import sys

import numpy as np

from model_assets import ROOT, digest, read_sources, verify_artifacts


def linear(srgb):
    return np.where(srgb <= .04045, srgb / 12.92,
                    ((srgb + .055) / 1.055) ** 2.4).astype(np.float32)


def display(image):
    bounded = image.clip(0, 1)
    return np.where(bounded <= .0031308, bounded * 12.92,
                    1.055 * bounded ** (1 / 2.4) - .055).astype(np.float32)


def quality(noisy, clean, actual, step):
    """Require improved color and edge errors, including actual tile seams."""
    if (clean.ndim != 3 or clean.shape[2] != 3 or min(clean.shape[:2]) < 2
            or not isinstance(step, int) or step < 1):
        raise ValueError("Invalid photographic quality dimensions.")
    for value in (noisy, clean, actual):
        if (value.shape != clean.shape or value.dtype != np.float32
                or not np.isfinite(value).all()):
            raise ValueError("Expected matching finite Float32 RGB images.")
    regions = {}
    for space, values in (("linear", (noisy, clean, actual)),
                          ("srgb", tuple(display(v) for v in (noisy, clean, actual)))):
        before, reference, after = (value.astype(np.float64) for value in values)
        input_mse = np.mean((before - reference) ** 2, axis=(0, 1))
        output_mse = np.mean((after - reference) ** 2, axis=(0, 1))
        regions[space] = {"input_mse_rgb": input_mse.tolist(), "output_mse_rgb": output_mse.tolist(),
                          "all_channels_improved": bool(np.all(output_mse < input_mse))}
    edges = {}
    before, reference, after = (display(v).astype(np.float64) for v in (noisy, clean, actual))
    for axis, label in ((0, "vertical"), (1, "horizontal")):
        input_error = np.diff(before, axis=axis) - np.diff(reference, axis=axis)
        output_error = np.diff(after, axis=axis) - np.diff(reference, axis=axis)
        selections = {"all": np.arange(clean.shape[axis] - 1),
                      "tile-seams": np.arange(step - 1, clean.shape[axis] - 1, step)}
        for region, indices in selections.items():
            if not indices.size:
                continue
            old = float(np.mean(np.take(input_error, indices, axis=axis) ** 2))
            new = float(np.mean(np.take(output_error, indices, axis=axis) ** 2))
            edges[label + "-" + region] = {"input_mse": old, "output_mse": new,
                                           "improved": new < old}
    passed = all(r["all_channels_improved"] for r in regions.values()) and all(
        r["improved"] for r in edges.values())
    return {"status": "PASS" if passed else "FAIL", "color": regions, "edge_errors": edges}


def source_tiled(network, image, size, halo):
    import torch
    height, width = image.shape[:2]
    step = size - 2 * halo
    rows, columns = math.ceil(height / step), math.ceil(width / step)
    # NumPy reflection is independent of the native reflect()/stride loops.
    padded = np.pad(image, ((halo, rows * step - height + halo),
                            (halo, columns * step - width + halo), (0, 0)), mode="reflect")
    result = np.empty_like(image)
    for y in range(0, height, step):
        for x in range(0, width, step):
            tile = np.ascontiguousarray(padded[y:y + size, x:x + size].transpose(2, 0, 1)[None])
            with torch.inference_mode():
                output = network(torch.from_numpy(tile)).numpy()[0].transpose(1, 2, 0)
            h, w = min(step, height - y), min(step, width - x)
            result[y:y + h, x:x + w] = output[halo:halo + h, halo:halo + w]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = {"schema": 1, "scope": "three-developed-photographs-added-awgn-native-tiling",
              "status": "RUNNING", "platform": platform.platform(), "cases": [],
              "sensor_noise_quality": "NOT_RUN", "visual_review": "PENDING",
              "compute_policy": "ALL; runtime CPU fallback possible, actual placement unobserved",
              "validator_sha256": digest(Path(__file__)), "probe_sha256": digest(args.probe),
              "provider_source_sha256": digest(ROOT / "platform/macos/CoreMLDenoise.mm")}
    limits = {"max_absolute_error": .0005, "rmse": .00005}
    report["numerical_limits"] = limits
    try:
        import torch
        from PIL import Image, ImageDraw
        torch.set_num_threads(2)
        torch.manual_seed(0)
        manifest = verify_artifacts(args.artifacts)
        report["manifest_sha256"] = digest(args.artifacts / "manifest.json")
        report["recipe_sha256"] = manifest["recipe_sha256"]
        report["toolchain"] = {name: importlib.metadata.version(name) for name in manifest["toolchain"]}
        if report["toolchain"] != manifest["toolchain"]:
            raise ValueError("Photographic comparison requires the conversion environment.")
        report["pillow"] = importlib.metadata.version("Pillow")
        model = next(m for m in read_sources()["models"] if m["role"] == "denoise")
        report["model"] = model["id"]
        weights = ROOT / ".tools/model-sources" / model["source"]["filename"]
        if weights.is_symlink() or weights.stat().st_size != model["source"]["size"] or digest(weights) != model["source"]["sha256"]:
            raise ValueError("Source checkpoint differs from its pinned bytes.")
        network = runpy.run_path(str(ROOT / "scripts/convert-models.py"))["denoise_network"](weights)
        fixtures = ROOT / "tests/assets/filters"
        report["fixture_manifest_sha256"] = digest(fixtures / "manifest.json")
        for index, fixture in enumerate(json.loads((fixtures / "manifest.json").read_text())):
            path = fixtures / fixture["file"]
            if digest(path) != fixture["sha256"]:
                raise ValueError("Photographic fixture differs from its pinned bytes.")
            with Image.open(path) as decoded:
                if decoded.mode != "RGB":
                    raise ValueError("Expected an already-developed RGB fixture.")
                srgb = np.array(decoded, dtype=np.float32) / 255
            clean = linear(srgb)
            for sigma in (.01, .05, 0):
                name = path.stem + "-sigma-" + str(sigma)
                folder = args.output / name
                folder.mkdir()
                noise = np.random.default_rng(5101 + index).normal(0, sigma, srgb.shape).astype(np.float32)
                noisy = linear((srgb + noise).clip(0, 1))
                noisy.tofile(folder / "input.bin")
                command = [str(args.probe.resolve()), str((args.artifacts / model["bundle"]).resolve()),
                           str((folder / "input.bin").resolve()), str((folder / "output.bin").resolve()),
                           str(clean.shape[1]), str(clean.shape[0]), "1"]
                if index == 0 and sigma == .01:
                    command.append("contracts")
                native = subprocess.run(command, capture_output=True, text=True, timeout=240)
                (folder / "native.log").write_text(native.stdout + native.stderr)
                if native.returncode:
                    raise RuntimeError(f"Runtime bridge exited {native.returncode}: {native.stderr}")
                actual = np.fromfile(folder / "output.bin", dtype=np.float32).reshape(clean.shape)
                reference = source_tiled(network, noisy, model["size"], model["halo"])
                error = actual.astype(np.float64) - reference.astype(np.float64)
                maximum, rmse = float(np.abs(error).max()), float(np.sqrt(np.mean(error * error)))
                numerical = np.isfinite(error).all() and maximum <= limits["max_absolute_error"] and rmse <= limits["rmse"]
                native_metadata = json.loads(native.stdout)
                step = model["size"] - model["halo"] * 2
                expected_tiles = math.ceil(clean.shape[0] / step) * math.ceil(clean.shape[1] / step)
                if len(native_metadata["progress"]) != expected_tiles:
                    raise ValueError("Runtime progress count differs from the actual tile grid.")
                score = quality(noisy, clean, actual, step) if sigma else {
                    "status": "OBSERVATIONAL", "srgb_rmse_change": float(np.sqrt(np.mean((display(actual) - srgb) ** 2))),
                    "reason": "Developed photograph is not certified noiseless sensor ground truth."}
                record = {"sample": name, "sigma_srgb": sigma, "fixture_sha256": fixture["sha256"],
                          "shape": list(clean.shape), "native": native_metadata,
                          "numerics": {"status": "PASS" if numerical else "FAIL", "max_absolute_error": maximum, "rmse": rmse},
                          "quality": score, "input_sha256": digest(folder / "input.bin"),
                          "output_sha256": digest(folder / "output.bin"),
                          "status": "PASS" if numerical and score["status"] in ("PASS", "OBSERVATIONAL") else "FAIL"}
                report["cases"].append(record)
                sheet = Image.new("RGB", (clean.shape[1] * 3, clean.shape[0] + 24), (24, 24, 24))
                draw = ImageDraw.Draw(sheet)
                for column, (label, picture) in enumerate((("Reference", srgb), ("Noisy", display(noisy)), ("Native Denoise", display(actual)))):
                    sheet.paste(Image.fromarray(np.rint(picture.clip(0, 1) * 255).astype(np.uint8)), (column * clean.shape[1], 24))
                    draw.text((column * clean.shape[1] + 8, 5), label, fill="white")
                sheet.save(folder / "comparison.png")
                print(name + ": " + record["status"] + " quality=" + score["status"], flush=True)
        report["status"] = "PASS" if report["cases"] and all(c["status"] == "PASS" for c in report["cases"]) else "FAIL"
    except Exception as error:
        report["status"] = "FAIL"
        report["error"] = str(error)
    with (args.output / "report.json").open("x") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(report["status"] + ": " + str(args.output / "report.json"))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
