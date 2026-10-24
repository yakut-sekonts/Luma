"""Bounded Sky interior-region quality and actual native preprocessing checks.

Regions are independent annotations of the licensed source photographs. Unknown
boundaries are excluded; this does not measure dense segmentation IoU or qualify
RAW, all scenes, accelerator placement or CPU fallback.
"""
import argparse
import importlib.metadata
import json
from pathlib import Path
import platform
import runpy
import subprocess
import sys

import numpy as np

from model_assets import ROOT, digest, read_sources, verify_artifacts


def quality(probability, fixture, limits):
    if (probability.ndim != 2 or min(probability.shape) < 1 or probability.dtype != np.float32
            or not np.isfinite(probability).all() or (probability < 0).any()
            or (probability > 1).any()):
        raise ValueError("Expected a finite Float32 Sky probability plane in [0,1].")
    width, height = fixture["size"]
    if any(type(n) is not int or n <= 0 for n in (width, height)) or not fixture["regions"]:
        raise ValueError("Sky annotations need positive dimensions and nonempty regions.")
    x = (np.arange(probability.shape[1]) + .5) * width / probability.shape[1]
    y = (np.arange(probability.shape[0]) + .5) * height / probability.shape[0]
    records = []
    for region in fixture["regions"]:
        x0, y0, x1, y1 = region["rect"]
        if (any(type(n) is not int for n in (x0, y0, x1, y1))
                or not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height)
                or type(region["sky"]) is not bool):
            raise ValueError("Invalid Sky region annotation.")
        values = probability[np.ix_((y >= y0) & (y < y1), (x >= x0) & (x < x1))]
        if not values.size:
            raise ValueError("Sky region contains no output samples.")
        mean, selected = float(values.mean(dtype=np.float64)), float(np.mean(values >= .5))
        passed = ((mean >= limits["sky_min_mean"] and selected >= limits["sky_min_fraction_over_half"])
                  if region["sky"] else
                  (mean <= limits["non_sky_max_mean"] and selected <= limits["non_sky_max_fraction_over_half"]))
        records.append({"name": region["name"], "sky": region["sky"], "rect": region["rect"],
                        "pixels": int(values.size), "mean_probability": mean,
                        "fraction_over_half": selected, "status": "PASS" if passed else "FAIL"})
    return {"status": "PASS" if all(r["status"] == "PASS" for r in records) else "FAIL", "regions": records}


def source_input(rgb, size):
    import torch
    # Independent interpolation library; half-pixel centers and edge clamping
    # implement the contract, without sharing the native provider's loops.
    tensor = torch.from_numpy(np.ascontiguousarray(rgb.transpose(2, 0, 1)[None])).float() / 255
    return torch.nn.functional.interpolate(tensor, (size, size), mode="bilinear",
                                           align_corners=False, antialias=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    annotations = ROOT / "tests/assets/filters/sky-regions.json"
    report = {"schema": 1, "status": "RUNNING", "scope": "three-photographs-conservative-interior-regions",
              "platform": platform.platform(), "cases": [], "dense_boundary_quality": "NOT_RUN",
              "visual_review": "PENDING", "compute_policy": "ALL; actual device and CPU fallback unobserved",
              "validator_sha256": digest(Path(__file__)), "probe_sha256": digest(args.probe),
              "provider_source_sha256": digest(ROOT / "platform/macos/CoreMLSky.mm"),
              "annotations_sha256": digest(annotations)}
    # Existing Float32 comparison bound plus unavoidable 8-bit mask rounding.
    numerical_limit = .0005 + .5 / 255
    report["runtime_max_absolute_error_limit"] = numerical_limit
    try:
        import torch
        from PIL import Image, ImageDraw
        torch.set_num_threads(2)
        manifest = verify_artifacts(args.artifacts)
        report["manifest_sha256"] = digest(args.artifacts / "manifest.json")
        report["recipe_sha256"] = manifest["recipe_sha256"]
        report["toolchain"] = {name: importlib.metadata.version(name) for name in manifest["toolchain"]}
        if report["toolchain"] != manifest["toolchain"]:
            raise ValueError("Sky comparison requires the recorded conversion environment.")
        model = next(m for m in read_sources()["models"] if m["role"] == "sky")
        report["model"] = model["id"]
        weights = ROOT / ".tools/model-sources" / model["source"]["filename"]
        if (weights.is_symlink() or weights.stat().st_size != model["source"]["size"]
                or digest(weights) != model["source"]["sha256"]):
            raise ValueError("Sky weights differ from the pinned source.")
        network = runpy.run_path(str(ROOT / "scripts/convert-models.py"))["sky_network"](weights)
        fixtures = ROOT / "tests/assets/filters"
        fixtures_manifest = json.loads((fixtures / "manifest.json").read_text())
        annotation = json.loads(annotations.read_text())
        if annotation["schema"] != 1 or len(annotation["fixtures"]) != 3:
            raise ValueError("Unexpected Sky annotation inventory.")
        report["quality_limits"] = annotation["limits"]
        report["fixture_manifest_sha256"] = digest(fixtures / "manifest.json")
        for index, fixture in enumerate(annotation["fixtures"]):
            path = fixtures / fixture["file"]
            pinned = next(f for f in fixtures_manifest if f["file"] == fixture["file"])
            if digest(path) != pinned["sha256"]:
                raise ValueError("Sky photograph differs from its licensed pinned bytes.")
            with Image.open(path) as decoded:
                if decoded.mode != "RGB" or list(decoded.size) != fixture["size"]:
                    raise ValueError("Sky annotation dimensions disagree with the photograph.")
                rgb = np.array(decoded)
                picture = decoded.copy()
            folder = args.output / path.stem
            folder.mkdir()
            rgba = np.concatenate((rgb, np.full((*rgb.shape[:2], 1), 255, dtype=np.uint8)), axis=2)
            rgba.tofile(folder / "input.bin")
            command = [str(args.probe.resolve()), str((args.artifacts / model["bundle"]).resolve()),
                       str((folder / "input.bin").resolve()), str((folder / "mask.bin").resolve()),
                       str(rgb.shape[1]), str(rgb.shape[0])]
            if index == 0:
                command.append("contracts")
            native = subprocess.run(command, capture_output=True, text=True, timeout=180)
            (folder / "native.log").write_text(native.stdout + native.stderr)
            if native.returncode:
                raise RuntimeError(f"Sky runtime probe exited {native.returncode}: {native.stderr}")
            metadata = json.loads(native.stdout)
            if (metadata["width"] != model["size"] or metadata["height"] != model["size"]
                    or (folder / "mask.bin").stat().st_size != model["size"] ** 2):
                raise ValueError("Native Sky mask violates its compiled size contract.")
            actual = np.fromfile(folder / "mask.bin", dtype=np.uint8).reshape(model["size"], model["size"]).astype(np.float32) / 255
            with torch.inference_mode():
                source = network(source_input(rgb, model["size"])).numpy()
            if source.shape != (1, 1, model["size"], model["size"]):
                raise ValueError("Unexpected source Sky output shape.")
            reference = source[0, 0]
            source_quality = quality(reference, fixture, annotation["limits"])
            native_quality = quality(actual, fixture, annotation["limits"])
            maximum = float(np.max(np.abs(actual.astype(np.float64) - reference)))
            numerical = np.isfinite(maximum) and maximum <= numerical_limit
            record = {"sample": path.stem, "fixture_sha256": pinned["sha256"],
                      "native": metadata, "source_quality": source_quality, "native_quality": native_quality,
                      "numerics": {"status": "PASS" if numerical else "FAIL", "max_absolute_error": maximum},
                      "input_sha256": digest(folder / "input.bin"), "mask_sha256": digest(folder / "mask.bin"),
                      "status": "PASS" if numerical and source_quality["status"] == native_quality["status"] == "PASS" else "FAIL"}
            report["cases"].append(record)
            mask_image = Image.fromarray(np.rint(actual * 255).astype(np.uint8)).resize(picture.size, Image.Resampling.BILINEAR)
            overlay = Image.blend(picture, Image.composite(Image.new("RGB", picture.size, (40, 160, 255)), picture, mask_image), .5)
            sheet = Image.new("RGB", (picture.width * 3, picture.height + 24), (24, 24, 24))
            draw = ImageDraw.Draw(sheet)
            for column, (label, value) in enumerate((("Source + annotated interiors", picture), ("Native Sky mask", mask_image), ("Overlay", overlay))):
                sheet.paste(value, (column * picture.width, 24))
                draw.text((column * picture.width + 8, 5), label, fill="white")
            for region in fixture["regions"]:
                x0, y0, x1, y1 = region["rect"]
                draw.rectangle((x0, y0 + 24, x1 - 1, y1 + 23), outline="cyan" if region["sky"] else "red", width=2)
            sheet.save(folder / "comparison.png")
            print(path.stem + ": " + record["status"] + " source=" + source_quality["status"] + " native=" + native_quality["status"], flush=True)
        if digest(args.probe) != report["probe_sha256"] or digest(annotations) != report["annotations_sha256"]:
            raise ValueError("Sky probe or annotations changed during the campaign.")
        report["status"] = "PASS" if all(c["status"] == "PASS" for c in report["cases"]) else "FAIL"
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
