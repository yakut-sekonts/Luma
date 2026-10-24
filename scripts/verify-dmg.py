"""Verify the actual read-only installer, its portable branding and exact app version."""
import json
import os
from pathlib import Path
import plistlib
import shutil
import time
import subprocess
import sys
import tempfile
import zipfile
from model_assets import digest, verify_artifacts
from release_identity import verify_bundle_matches_zip

root = Path(__file__).resolve().parents[1]
image = Path(sys.argv[1]).resolve()
version = image.name.removeprefix("Luma-").removesuffix("-macos-arm64.dmg")

def run(*args, **kwargs):
    result = subprocess.run(list(map(str, args)), capture_output=True,
                            text=True, timeout=120, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{Path(str(args[0])).name} failed ({result.returncode}): {result.stdout}{result.stderr}")
    return result

directory = Path(tempfile.mkdtemp(prefix="luma-installer-"))
mountpoint = directory / "volume"
mounted = False
try:
    run("hdiutil", "verify", image)
    run("hdiutil", "attach", "-readonly", "-nobrowse", "-noautoopen", "-mountpoint", mountpoint, image)
    mounted = True
    app = mountpoint / "Luma.app"
    with (app / "Contents/Info.plist").open("rb") as file:
        info = plistlib.load(file)
    assert info["CFBundleShortVersionString"] == info["CFBundleVersion"] == version
    assert info["CFBundleIconFile"] == "Luma.icns"
    icon = app / "Contents/Resources/Luma.icns"
    assert icon.read_bytes() == (root / "resources/branding/Luma.icns").read_bytes()
    assert (mountpoint / ".VolumeIcon.icns").read_bytes() == icon.read_bytes()
    assert "C" in run("xcrun", "GetFileInfo", "-a", mountpoint).stdout.strip()
    assert (mountpoint / "Applications").is_symlink()
    assert os.readlink(mountpoint / "Applications") == "/Applications"
    assert (mountpoint / "READ_ME.md").read_bytes() == (root / f"docs/releases/v{version}.md").read_bytes()
    provenance = json.loads((app / "Contents/Resources/BUILD_INFO.json").read_text())
    assert provenance["version"] == version
    assert provenance["source_commit"] == run("git", "rev-parse", "HEAD", cwd=root).stdout.strip()
    models = app / "Contents/Resources/Models"
    model_manifest = verify_artifacts(models)
    assert provenance["bundled_models"] == model_manifest["models"]
    assert provenance["models_manifest_sha256"] == digest(models / "manifest.json")
    release_info = json.loads((image.parent / "BUILD_INFO.json").read_text())
    with zipfile.ZipFile(image.with_suffix(".zip")) as archive:
        identity = verify_bundle_matches_zip(app, archive, release_info)
    print("Installer/archive identity: " + json.dumps(identity, sort_keys=True))
    run("codesign", "--verify", "--deep", "--strict", app)
    executable = app / "Contents/MacOS/Luma"
    env = os.environ.copy()
    for name in ("QT_QPA_PLATFORM", "QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
        env.pop(name, None)
    env["QT_QUICK_BACKEND"] = "software"
    assert run(executable, "--version", env=env).stdout.strip() == f"Luma {version}"
    run(executable, "--smoke-test", "--catalog", directory / "catalog.sqlite", env=env)
    print(f"DMG {version}: read-only mount, custom volume/app icons, Applications link, notes, source commit, strict signature and actual binary version/startup passed")
finally:
    if mounted or mountpoint.is_mount():
        for attempt in range(4):
            command = ["hdiutil", "detach"]
            if attempt == 3:
                command.append("-force")
            result = subprocess.run([*command, str(mountpoint)], capture_output=True, text=True, timeout=120)
            if result.returncode == 0:
                break
            if attempt < 3:
                time.sleep(2)
        else:
            raise RuntimeError(f"Cannot detach verification image; preserving {directory}: {result.stderr}")
    # A failed detach must preserve the workspace, never recurse into a mount.
    shutil.rmtree(directory)
