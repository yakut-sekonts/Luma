"""Shared, dependency-free provenance rules for bundled model artifacts."""
import hashlib
import json
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "resources/models/sources.json"
RECIPE_FILES = (
    "resources/models/sources.json", "resources/models/NOTICE.md",
    "scripts/convert-models.py", "scripts/models/drunet.py", "scripts/model_assets.py",
    "scripts/model-requirements.txt",
)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_sources():
    data = json.loads(SOURCES.read_text())
    if data["schema"] != 1 or data["delivery"] != "bundled-offline":
        raise ValueError("Unsupported bundled model source manifest.")
    if [model["role"] for model in data["models"]] != ["denoise", "sky"]:
        raise ValueError("Both denoise and sky models must be supplied.")
    return data


def recipe_digest():
    value = hashlib.sha256()
    for relative in RECIPE_FILES:
        value.update(relative.encode() + b"\0" + bytes.fromhex(digest(ROOT / relative)))
    for model in read_sources()["models"]:
        license_path = ROOT / "resources/models/licenses" / model["license_file"]
        actual = digest(license_path)
        if actual != model["license_sha256"]:
            raise ValueError("A model license differs from the pinned source manifest.")
        value.update(model["license_file"].encode() + b"\0" + bytes.fromhex(actual))
    return value.hexdigest()


def inventory(folder):
    folder = Path(folder)
    if folder.is_symlink() or not folder.is_dir():
        raise ValueError("Model artifacts must be a real directory.")
    entries, total = {}, 0
    for item in sorted(folder.rglob("*")):
        if item.is_symlink():
            raise ValueError("Model artifacts must not contain symbolic links.")
        if item.is_dir():
            continue
        if not item.is_file():
            raise ValueError("Model artifacts must contain regular files only.")
        relative = item.relative_to(folder).as_posix()
        if relative == "manifest.json":
            continue
        size = item.stat().st_size
        total += size
        if total > 1024 ** 3 or len(entries) >= 4096:
            raise ValueError("Bundled models exceed their file/size budget.")
        entries[relative] = {"size": size, "sha256": digest(item)}
    return entries


def describe_models():
    return [{key: model[key] for key in ("role", "id", "display_name", "bundle", "license")}
            | {"source_sha256": model["source"]["sha256"]}
            for model in read_sources()["models"]]


def verify_artifacts(folder):
    folder = Path(folder)
    path = folder / "manifest.json"
    if path.is_symlink() or path.stat().st_size > 2 * 1024 ** 2:
        raise ValueError("Invalid bundled model manifest.")
    manifest = json.loads(path.read_text())
    if (manifest.get("schema") != 1 or manifest.get("recipe_sha256") != recipe_digest()
            or manifest.get("source_manifest_sha256") != digest(SOURCES)
            or manifest.get("models") != describe_models()):
        raise ValueError("Bundled models do not match this source/conversion recipe.")
    actual = inventory(folder)
    if not actual or manifest.get("files") != actual:
        raise ValueError("Bundled model contents differ from their manifest.")
    for model in manifest["models"]:
        prefix = model["bundle"] + "/"
        if not (folder / model["bundle"]).is_dir() or not any(name.startswith(prefix) for name in actual):
            raise ValueError("A required compiled model is missing.")
    return manifest


def verify_zip(archive, expected_manifest_digest):
    """Read the final update ZIP without extracting or trusting member paths."""
    prefix = "Luma.app/Contents/Resources/Models/"
    names = [item.filename for item in archive.infolist()]
    if len(names) != len(set(names)):
        raise ValueError("Duplicate files in release ZIP.")
    manifest_entry = archive.getinfo(prefix + "manifest.json")
    if manifest_entry.file_size > 2 * 1024 ** 2:
        raise ValueError("Oversized release model manifest.")
    raw = archive.read(manifest_entry)
    if hashlib.sha256(raw).hexdigest() != expected_manifest_digest:
        raise ValueError("Release model manifest differs from BUILD_INFO.")
    manifest = json.loads(raw)
    if (manifest.get("schema") != 1 or manifest.get("recipe_sha256") != recipe_digest()
            or manifest.get("source_manifest_sha256") != digest(SOURCES)
            or manifest.get("models") != describe_models()):
        raise ValueError("Release models differ from the pinned models.")
    actual_names = {name[len(prefix):] for name in names if name.startswith(prefix)
                    and not name.endswith("/") and name != prefix + "manifest.json"}
    if actual_names != set(manifest["files"]) or len(actual_names) > 4096:
        raise ValueError("Release model file set is incomplete.")
    for model in manifest["models"]:
        if not any(name.startswith(model["bundle"] + "/") for name in actual_names):
            raise ValueError("A required compiled release model is missing.")
    total = 0
    for name, value in manifest["files"].items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts or "\\" in name:
            raise ValueError("Invalid model member path.")
        entry = archive.getinfo(prefix + name)
        total += entry.file_size
        if (total > 1024 ** 3 or entry.file_size != value["size"]
                or (entry.external_attr >> 16) & 0o170000 == 0o120000):
            raise ValueError("Invalid model member size or type.")
        with archive.open(entry) as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != value["sha256"]:
                raise ValueError("Release model checksum mismatch.")
    return manifest
