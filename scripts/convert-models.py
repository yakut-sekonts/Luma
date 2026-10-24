"""Build the two pinned offline models. This is not a quality qualification.

Run only during final release preparation under the project's deferred-QA policy.
No photo input, inference benchmarking, model download in the app, or credentials.
"""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import shutil
import ssl
import subprocess
import sys
import tempfile
import urllib.request

from model_assets import ROOT, SOURCES, describe_models, digest, inventory, read_sources, recipe_digest


def require_xcode():
    try:
        subprocess.run(["xcrun", "--find", "coremlcompiler"], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return subprocess.check_output(["xcodebuild", "-version"], text=True,
                                       stderr=subprocess.PIPE).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise SystemExit("Full Xcode with coremlcompiler is required. Select Xcode.app "
                         "using xcode-select or DEVELOPER_DIR; Command Line Tools alone "
                         "are insufficient.") from error


def source_file(model, cache):
    source = model["source"]
    target = cache / source["filename"]
    if target.exists():
        if target.is_symlink() or target.stat().st_size != source["size"] or digest(target) != source["sha256"]:
            raise ValueError("Cached model weights do not match the lock; remove that generated cache entry before retrying.")
        return target
    import certifi
    context = ssl.create_default_context(cafile=certifi.where())
    request = urllib.request.Request(source["url"], headers={"User-Agent": "Luma-model-packager/1"})
    with tempfile.NamedTemporaryFile(dir=cache, prefix=".download-", delete=False) as output:
        temporary = Path(output.name)
        try:
            with urllib.request.urlopen(request, context=context, timeout=60) as response:
                if not response.url.startswith("https://"):
                    raise ValueError("Model source redirected to an insecure endpoint.")
                count, checksum = 0, hashlib.sha256()
                while chunk := response.read(1024 ** 2):
                    count += len(chunk)
                    if count > source["size"]:
                        raise ValueError("Model source exceeded its pinned size.")
                    checksum.update(chunk)
                    output.write(chunk)
            if count != source["size"] or checksum.hexdigest() != source["sha256"]:
                raise ValueError("Model source checksum mismatch.")
            output.flush()
            os.fsync(output.fileno())
            os.replace(temporary, target)
        finally:
            temporary.unlink(missing_ok=True)
    return target


def denoise_network(path):
    import torch
    from models.drunet import DRUNet, LinearDenoise, NoiseConditioned
    # Never unpickle arbitrary checkpoint objects or run upstream repository code.
    checkpoint = torch.load(path, map_location="cpu", weights_only=True)
    if not isinstance(checkpoint, dict) or not checkpoint:
        raise ValueError("Unexpected DRUNet checkpoint format.")
    model = DRUNet()
    model.load_state_dict(checkpoint, strict=True)
    return LinearDenoise(NoiseConditioned(model)).eval()


def sky_network(path):
    import onnx
    import torch
    from onnx2torch import convert
    source = onnx.load(path, load_external_data=False)
    if (len(source.graph.input) != 1 or not source.graph.output
            or any(item.data_location == onnx.TensorProto.EXTERNAL for item in source.graph.initializer)
            or any(node.domain not in ("", "ai.onnx") for node in source.graph.node)):
        raise ValueError("Unexpected Sky ONNX graph or external model data.")
    shape = [d.dim_value for d in source.graph.input[0].type.tensor_type.shape.dim]
    if shape != [1, 3, 320, 320]:
        raise ValueError("The pinned Sky model must accept [1,3,320,320].")
    # Upstream inference uses the first output. Remove auxiliary supervision
    # outputs before conversion; Core ML eliminates their unused branches.
    while len(source.graph.output) > 1:
        source.graph.output.pop()
    network = convert(source).eval()

    class Sky(torch.nn.Module):
        def __init__(self):
            super().__init__()
            self.network = network
            self.register_buffer("mean", torch.tensor([.485, .456, .406]).view(1, 3, 1, 1))
            self.register_buffer("std", torch.tensor([.229, .224, .225]).view(1, 3, 1, 1))

        def forward(self, image):
            result = self.network((image - self.mean) / self.std)
            if isinstance(result, (list, tuple)):
                result = result[0]
            # Preserve absolute confidence. Per-image min/max stretching can
            # turn an almost uniform no-sky result into a strong false mask.
            return result.clamp(0, 1)

    return Sky().eval()


def convert_model(model, weights, work):
    import numpy as np
    import torch
    import coremltools as ct
    network = denoise_network(weights) if model["role"] == "denoise" else sky_network(weights)
    sample = torch.zeros(1, 3, model["size"], model["size"], dtype=torch.float32)
    with torch.no_grad():
        traced = torch.jit.trace(network, sample, check_trace=False, strict=True)
    converted = ct.convert(traced, source="pytorch", convert_to="mlprogram",
        minimum_deployment_target=ct.target.macOS14, compute_precision=ct.precision.FLOAT32,
        inputs=[ct.TensorType(name=model["input"], shape=sample.shape, dtype=np.float32)],
        outputs=[ct.TensorType(name=model["output"], dtype=np.float32)], skip_model_load=True)
    converted.author = "Luma adaptation; see bundled upstream license and model notice"
    converted.license = model["license"]
    converted.short_description = model["display_name"]
    metadata = {"luma.model.id": model["id"], "luma.model.source.sha256": model["source"]["sha256"]}
    if model["role"] == "denoise":
        metadata |= {"luma.denoise.schema": "1", "luma.denoise.color": "linear-srgb",
                     "luma.denoise.halo": str(model["halo"])}
    else:
        metadata |= {"luma.sky.schema": "1", "luma.sky.color": "srgb", "luma.sky.resize": "stretch"}
    converted.user_defined_metadata.update(metadata)
    package = work / model["bundle"].replace(".mlmodelc", ".mlpackage")
    converted.save(str(package))
    compiled = work / "compiled"
    compiled.mkdir(exist_ok=True)
    subprocess.run(["xcrun", "coremlcompiler", "compile", str(package), str(compiled)], check=True)
    output = compiled / model["bundle"]
    if not output.is_dir():
        raise ValueError("Core ML compiler did not produce the expected model directory.")
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache", type=Path, default=ROOT / ".tools/model-sources")
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise SystemExit("Bundled Core ML models must be built on Apple Silicon macOS.")
    if sys.version_info[:2] != (3, 12):
        raise SystemExit("Model conversion requires Python 3.12.")
    if args.output.exists():
        raise SystemExit("Model output already exists. Use a new build directory.")
    xcode = require_xcode()
    requirements = ROOT / "scripts/model-requirements.txt"
    versions = {}
    for line in requirements.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        name, version = line.split("==")
        if importlib.metadata.version(name) != version:
            raise SystemExit("Model conversion requires the pinned model toolchain.")
        versions[name] = version
    # Pin the recipe before creating output; includes exact notices and weights.
    recipe = recipe_digest()
    args.cache.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    import torch
    torch.set_num_threads(2)
    torch.manual_seed(0)
    with tempfile.TemporaryDirectory(prefix=".luma-model-build-", dir=args.output.parent) as temporary:
        work = Path(temporary)
        bundle = work / "Models"
        bundle.mkdir()
        for model in read_sources()["models"]:
            print("Converting " + model["id"], flush=True)
            weights = source_file(model, args.cache)
            compiled = convert_model(model, weights, work)
            shutil.copytree(compiled, bundle / model["bundle"])
        shutil.copytree(ROOT / "resources/models/licenses", bundle / "licenses")
        shutil.copy2(SOURCES, bundle / "sources.json")
        shutil.copy2(ROOT / "resources/models/NOTICE.md", bundle / "NOTICE.md")
        manifest = {"schema": 1, "recipe_sha256": recipe, "source_manifest_sha256": digest(SOURCES),
                    "models": describe_models(), "files": inventory(bundle), "toolchain": versions,
                    "python": platform.python_version(),
                    "resolved_packages": dict(sorted((item.metadata["Name"], item.version)
                                                     for item in importlib.metadata.distributions())),
                    "xcode": xcode,
                    "qualification": "NOT_RUN"}
        (bundle / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        os.rename(bundle, args.output)
    print("Compiled model artifacts saved. Image quality and performance are not qualified.")


if __name__ == "__main__":
    main()
