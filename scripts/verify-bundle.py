"""Verify generated public workflows with the developer Qt SDK unavailable."""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import sqlite3
import subprocess
import sys
import tempfile
from model_assets import digest, verify_artifacts

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tests/assets"))
from generate_dng import create_dng

sdk = root / ".tools/qt/6.10.3/macos"
hidden = sdk.with_name("macos.luma-bundle-verification")
app = root / "dist/Luma.app"
with (app / "Contents/Info.plist").open("rb") as file:
    version = plistlib.load(file)["CFBundleShortVersionString"]
info = json.loads((app / "Contents/Resources/BUILD_INFO.json").read_text())
assert info["version"] == version
assert info["source_commit"] == subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
models = app / "Contents/Resources/Models"
model_manifest = verify_artifacts(models)
assert info["bundled_models"] == model_manifest["models"]
assert info["models_manifest_sha256"] == digest(models / "manifest.json")
assert sdk.is_dir() and not hidden.exists()
env = os.environ.copy()
for name in ("QT_QPA_PLATFORM", "QT_QUICK_BACKEND", "QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
    env.pop(name, None)
env["QT_QUICK_BACKEND"] = "software"
executable = app / "Contents/MacOS/Luma"
fixture_root = root / "dist/verification-image-fixtures"
subprocess.run([str(root / os.environ.get("LUMA_BUILD_DIRECTORY", "build") / "luma_raster_tests"), "--fixtures", str(fixture_root)], check=True, capture_output=True)
sdk.rename(hidden)
sparkle_sources = []
try:
    for source in (root / ".tools/sparkle", root / "build/_deps/sparkle-src", root / "build-verify/_deps/sparkle-src"):
        if source.is_dir():
            destination = source.with_name(source.name + ".luma-bundle-verification")
            assert not destination.exists()
            source.rename(destination)
            sparkle_sources.append((source, destination))
    with tempfile.TemporaryDirectory(prefix="luma-bundle-") as temporary:
        folder = Path(temporary)
        sources = folder / "sources"
        sources.mkdir()
        raw = sources / "first.DNG"
        create_dng(raw)
        other = bytearray(raw.read_bytes())
        other[-2:] = b"\0\0"
        (sources / "second.DNG").write_bytes(other)
        checksum = hashlib.sha256(raw.read_bytes()).hexdigest()
        catalog = folder / "catalog.sqlite"
        for arguments in (
            ["--test-library", str(sources)],
            ["--test-edit", str(raw), "--test-exposure", "0.65", "--test-develop", "--test-advanced", "--test-local-masks", "--test-export", str(folder / "first.png")],
            ["--test-restore", "0.65", "--test-develop", "--test-advanced", "--test-local-masks", "--test-export", str(folder / "second.png")],
        ):
            result = subprocess.run([str(executable), "--catalog", str(catalog), *arguments], env=env, capture_output=True, text=True, timeout=120)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            assert "ReferenceError" not in result.stderr and "Unable to assign" not in result.stderr, result.stderr
        assert (folder / "first.png").read_bytes() == (folder / "second.png").read_bytes()
        assert hashlib.sha256(raw.read_bytes()).hexdigest() == checksum
        mask_folder=folder/"complete-masks";mask_folder.mkdir()
        for arguments in (["--test-edit",str(raw),"--test-exposure","0.35","--test-complete-masks","--test-export",str(mask_folder/"first.png")],
                          ["--test-restore","0.35","--test-complete-masks","--test-export",str(mask_folder/"second.png")]):
            result=subprocess.run([str(executable),"--catalog",str(mask_folder/"catalog.sqlite"),*arguments],env=env,capture_output=True,text=True,timeout=130)
            assert result.returncode==0,result.stdout+result.stderr
            assert not any(token in result.stderr for token in ("ReferenceError","Unable to assign","Binding loop","TypeError")),result.stderr
        assert (mask_folder/"first.png").read_bytes()==(mask_folder/"second.png").read_bytes()
        with sqlite3.connect(mask_folder/"catalog.sqlite") as db:
            catalog_schema = db.execute("PRAGMA user_version").fetchone()[0]
            assert catalog_schema == 21, f"Unexpected installed catalog schema: {catalog_schema}"
            masks = db.execute("SELECT document FROM edit_local_masks").fetchone()[0]
            assert masks.startswith("LUMA_LOCAL_MASKS 3 2\n"), "Installed mask document lost its version or two saved masks"
        assert hashlib.sha256(raw.read_bytes()).hexdigest()==checksum
        xmp_folder = folder / "xmp"; xmp_folder.mkdir()
        xmp_file = xmp_folder / "Sparse.XMP"
        xmp_file.write_text("<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
                            "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
                            "<rdf:Description xmlns:crs='http://ns.adobe.com/camera-raw-settings/1.0/' "
                            "crs:Exposure2012='1.3' crs:Texture='30'/></rdf:RDF></x:xmpmeta>")
        xmp_catalog = xmp_folder / "catalog.sqlite"
        for arguments in (["--test-edit", str(raw), "--test-exposure", "0.2", "--test-xmp-preset", str(xmp_file), "--test-export", str(xmp_folder / "first.png")],
                          ["--test-restore", "1.3", "--test-export", str(xmp_folder / "repeat.png")]):
            result = subprocess.run([str(executable), "--catalog", str(xmp_catalog), *arguments], env=env, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, result.stdout + result.stderr
            assert not any(token in result.stderr for token in ("ReferenceError", "Unable to assign", "Binding loop")), result.stderr
        assert (xmp_folder / "first.png").read_bytes() == (xmp_folder / "repeat.png").read_bytes()
        with sqlite3.connect(xmp_catalog) as db:
            assert db.execute("SELECT exposure, contrast FROM edit_documents").fetchone() == (1.3, 37.0)
        imported = json.loads((xmp_folder / "presets.json").read_text())
        assert imported["schemaVersion"] == 9, f"Unexpected installed preset-library schema: {imported['schemaVersion']}"
        sparse_preset = imported["presets"][0]["preset"]
        assert sparse_preset["schemaVersion"] == 9 and sparse_preset["parameters"] == ["exposure"]
        assert hashlib.sha256(raw.read_bytes()).hexdigest() == checksum
        thumbnails = list((folder / "catalog.sqlite.luma-cache/thumbnails").glob("*.jpg"))
        assert len(thumbnails) >= 2 and all(p.name.endswith("-edited-v4.jpg") for p in thumbnails)
        lens_source = folder / "calibrated.DNG"
        create_dng(lens_source, maker="NIKON CORPORATION", model="NIKON D7500",
                   lens="AF-S DX Nikkor 18-140mm f/3.5-5.6G ED VR", focal=18, aperture=5.6)
        lens_hash = hashlib.sha256(lens_source.read_bytes()).hexdigest()
        lens_catalog = folder / "lens.sqlite"
        for arguments in (["--test-edit", str(lens_source), "--test-exposure", "0.25", "--test-lens", "--test-transform", "--test-filter", "builtin:warm-film", "--test-export", str(folder / "lens-first.png")],
                          ["--test-restore", "0.25", "--test-export", str(folder / "lens-second.png")]):
            result = subprocess.run([str(executable), "--catalog", str(lens_catalog), *arguments], env=env, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, result.stdout + result.stderr
            assert "ReferenceError" not in result.stderr and "Unable to assign" not in result.stderr, result.stderr
        assert (folder / "lens-first.png").read_bytes() == (folder / "lens-second.png").read_bytes()
        assert hashlib.sha256(lens_source.read_bytes()).hexdigest() == lens_hash
        with sqlite3.connect(lens_catalog) as db:
            assert db.execute("SELECT enabled FROM edit_documents").fetchone()[0] & 32768
            assert db.execute("SELECT transform_rotate FROM edit_documents").fetchone()[0] == 8
        neutral_catalog = folder / "lens-neutral.sqlite"
        result = subprocess.run([str(executable), "--catalog", str(neutral_catalog), "--test-edit", str(lens_source), "--test-exposure", "0.25", "--test-export", str(folder / "lens-neutral.png")], env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, result.stdout + result.stderr
        assert (folder / "lens-first.png").read_bytes() != (folder / "lens-neutral.png").read_bytes()
        notices = app / "Contents/Resources/ThirdParty/Lensfun"
        assert (notices / "LICENSE.txt").stat().st_size > 10000
        assert (notices / "LensDatabase.h").stat().st_size > 100000
        assert len(list((notices / "db").glob("*.xml"))) > 40
        queue_raw = folder / "queue.DNG"
        create_dng(queue_raw, maker="NIKON CORPORATION", model="NIKON D7500", width=512, height=384)
        queue_hash = hashlib.sha256(queue_raw.read_bytes()).hexdigest()
        queue_output = folder / "queue-output"; queue_output.mkdir()
        queue_catalog = folder / "queue.sqlite"
        result = subprocess.run([str(executable), "--catalog", str(queue_catalog), "--test-edit", str(queue_raw), "--test-export-queue", str(queue_output)], env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "ReferenceError" not in result.stderr and "Unable to assign" not in result.stderr, result.stderr
        dng = next(queue_output.glob("*.dng"))
        import struct
        data = dng.read_bytes(); endian = "<" if data[:2] == b"II" else ">"
        def ifd(offset):
            count = struct.unpack_from(endian + "H", data, offset)[0]
            return {struct.unpack_from(endian + "H", data, offset + 2 + i*12)[0]: struct.unpack_from(endian + "HHII", data, offset + 2 + i*12) for i in range(count)}
        root_ifd = ifd(struct.unpack_from(endian + "I", data, 4)[0])
        raw_ifd = ifd(root_ifd[330][3])
        assert raw_ifd[262][3] == 34892 and raw_ifd[256][3] == 512 and raw_ifd[257][3] == 384
        assert b"Luma Rendered Linear RGB v1" in data
        assert hashlib.sha256(queue_raw.read_bytes()).hexdigest() == queue_hash
        settings = json.loads((folder / "export-settings.json").read_text())
        assert settings["schema"] == 1 and settings["last"]["format"] == 4
        assert "This product includes DNG technology under license by Adobe." in (app / "Contents/Resources/ThirdParty/THIRD_PARTY_LICENSES.md").read_text()
        for language in ("en", "ru", "zh_CN", "hy", "de", "es", "it", "fr"):
            locale_catalog = folder / language / "catalog.sqlite"
            result = subprocess.run([str(executable), "--smoke-test", "--test-window-width", "1000", "--test-language", language, "--catalog", str(locale_catalog)], env=env, capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            assert not any(error in result.stderr for error in ("ReferenceError", "TypeError", "Binding loop", "Unable to assign")), result.stderr
            assert json.loads((locale_catalog.parent / "language.json").read_text())["language"] == language
        image_pair = folder / "images"; image_pair.mkdir()
        import shutil
        for name in ("rgba.png", "opaque.jpg"):
            shutil.copyfile(fixture_root / name, image_pair / name)
        image_catalog = folder / "images.sqlite"
        def image_run(*args):
            result = subprocess.run([str(executable), "--catalog", str(image_catalog), *map(str, args)], env=env, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, result.stdout + result.stderr
            assert not any(token in result.stderr for token in ("ReferenceError", "Unable to assign", "Binding loop")), result.stderr
        image_run("--test-library", image_pair)
        hashes = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in image_pair.iterdir()}
        for source in [image_pair / "rgba.png", fixture_root / "ramp16.tif", root / "tests/assets/images/opaque.heic", root / "tests/assets/images/quadrants.webp"]:
            first = folder / (source.name + "-first.png")
            repeat = folder / (source.name + "-repeat.png")
            image_run("--test-edit", source, "--test-exposure", ".58", "--test-export", first)
            image_run("--test-restore", ".58", "--test-export", repeat)
            assert first.read_bytes() == repeat.read_bytes()
        image_queue = folder / "image-queue"; image_queue.mkdir()
        image_run("--test-edit", image_pair / "rgba.png", "--test-export-queue", image_queue)
        assert next(image_queue.glob("*.dng")).stat().st_size > 1000
        assert all(hashlib.sha256(path.read_bytes()).hexdigest() == checksum for path, checksum in hashes.items())
        print(f"Standalone Luma {version}: complete Brush/Erase/range/boolean masks, Library, Develop, Lens Corrections, Transform/Filter/Undo, sparse XMP import/apply/Undo/restart/export, edited thumbnails, advanced queue/processed DNG, eight runtime languages, ordinary-image Library/alpha/HEIC/WebP/16-bit edit/export/restart/queue, export/restart and source integrity passed without Qt/Sparkle SDKs")
finally:
    for source, destination in reversed(sparkle_sources):
        destination.rename(source)
    hidden.rename(sdk)
