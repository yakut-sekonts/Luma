"""Develop, export metadata, durable restart and destination-integrity acceptance."""
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "assets"))
from generate_dng import create_dng


def png_info(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    chunks = {}
    offset = 8
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        chunks[kind] = data[offset + 8:offset + 8 + size]
        offset += size + 12
    width, height, depth = struct.unpack_from(">IIB", chunks[b"IHDR"])
    assert b"iCCP" in chunks or b"sRGB" in chunks, "Missing embedded sRGB profile"
    return width, height, depth


def tiff_info(path):
    data = path.read_bytes()
    endian = "<" if data[:2] == b"II" else ">"
    assert struct.unpack_from(endian + "H", data, 2)[0] == 42
    offset = struct.unpack_from(endian + "I", data, 4)[0]
    count = struct.unpack_from(endian + "H", data, offset)[0]
    tags = {}
    for index in range(count):
        entry = offset + 2 + index * 12
        tag, kind, length, value = struct.unpack_from(endian + "HHII", data, entry)
        size = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 7: 1}.get(kind, 1) * length
        content = data[entry + 8:entry + 12] if size <= 4 else data[value:value + size]
        tags[tag] = (kind, length, content)
    def number(tag):
        kind, length, content = tags[tag]
        return struct.unpack_from(endian + ("H" if kind == 3 else "I"), content)[0]
    assert 34675 in tags and tags[34675][1] > 100, "TIFF ICC missing"
    assert number(258) == 16, "TIFF was reduced to 8-bit"
    return number(256), number(257)


def main():
    cli = Path(sys.argv[1])
    gui = cli.parent.parent / "Luma.app/Contents/MacOS/Luma"
    with tempfile.TemporaryDirectory(prefix="luma-develop-") as temporary:
        directory = Path(temporary)
        source = directory / "Мой RAW.DNG"
        create_dng(source)
        original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        catalog = directory / "catalog.sqlite"
        def run(*args, expected=0):
            result = subprocess.run([str(cli), *map(str, args), "--catalog", str(catalog)], capture_output=True, text=True, timeout=100)
            assert result.returncode == expected, f"{args}: exit={result.returncode}: {result.stderr}"
            return json.loads(result.stdout) if expected == 0 else result
        run("open", source)
        run("develop", "exposure=0.5", "temperature=5350", "tint=15", "contrast=20", "highlights=-35", "shadows=25", "whites=5", "blacks=-7", "vibrance=30", "saturation=12")
        saved = run("restore")
        assert saved["customWhiteBalance"] and saved["editSchemaVersion"] == 8
        run("develop", "contrast=50", "tint=nan", expected=1)
        assert run("restore")["contrast"] == 20, "Invalid batch partially committed"
        run("disable", "highlights")
        assert run("restore")["highlights"] == -35
        run("enable", "highlights")
        png = directory / "Export.png"
        run("export", png)
        assert png_info(png) == (128, 96, 8)
        png_bytes = png.read_bytes()
        run("export", png, expected=1)
        assert png.read_bytes() == png_bytes, "Refused overwrite modified destination"
        run("export", png, "--overwrite")
        assert png.read_bytes() == png_bytes, "Restart export is not deterministic"
        jpeg = directory / "Export.jpg"
        run("export", jpeg, "--quality", 90)
        assert jpeg.read_bytes()[:2] == b"\xff\xd8" and b"ICC_PROFILE\0" in jpeg.read_bytes(), "JPEG ICC missing"
        tiff = directory / "Export.tif"
        run("export", tiff)
        assert tiff_info(tiff) == (128, 96)
        run("export", source, "--overwrite", expected=1)
        alias = directory / "Original alias.png"
        os.link(source, alias)
        run("export", alias, "--overwrite", expected=1)
        assert hashlib.sha256(source.read_bytes()).hexdigest() == original_hash
        env = os.environ.copy()
        env["QT_QPA_PLATFORM"] = "offscreen"
        env["QT_QUICK_BACKEND"] = "software"
        gui_first, gui_second = directory / "GUI first.png", directory / "GUI second.png"
        for args in [
            ("--test-edit", str(source), "--test-exposure", "0.75", "--test-develop", "--test-export", str(gui_first)),
            ("--test-restore", "0.75", "--test-develop", "--test-export", str(gui_second)),
        ]:
            result = subprocess.run([str(gui), "--catalog", str(catalog), *args], env=env, capture_output=True, text=True, timeout=100)
            assert result.returncode == 0, f"GUI develop: exit={result.returncode}: {result.stdout} {result.stderr}"
        assert gui_first.read_bytes() == gui_second.read_bytes(), "GUI restart changed exported pixels"
        cancelled = directory / "Cancelled.png"
        result = subprocess.run([str(gui), "--catalog", str(catalog), "--test-restore", "0.75",
            "--test-export", str(cancelled), "--test-cancel-export"], env=env, capture_output=True, text=True, timeout=100)
        assert result.returncode == 0, f"Cancelled export hung or failed: {result.stderr}"
        assert not cancelled.exists(), "Cancelled export installed output"
        close_output = directory / "Closed during export.png"
        result = subprocess.run([str(gui), "--catalog", str(catalog), "--test-edit", str(source),
            "--test-exposure", "0.75", "--test-export", str(close_output), "--test-close-during-export"],
            env=env, capture_output=True, text=True, timeout=100)
        assert result.returncode == 0, f"Close with queued operations stalled: {result.stderr}"
        assert not close_output.exists(), "Close did not cancel export"
        catalog_alias = directory / "Catalog alias.png"
        os.link(catalog, catalog_alias)
        run("export", catalog_alias, "--overwrite", expected=1)
        restored = run("restore")
        assert restored["temperature"] == 5100 and restored["tint"] == 12
        assert restored["contrast"] == 18 and restored["highlights"] == -40 and restored["vibrance"] == 35
        assert restored["shadows"] == 25 and restored["enabledCorrections"] & 16 == 0
        assert run("catalog", "verify")["integrity"] == "ok"
        with sqlite3.connect(catalog) as connection:
            assert connection.execute("PRAGMA user_version").fetchone()[0] == 21
        assert hashlib.sha256(source.read_bytes()).hexdigest() == original_hash
        assert not list(directory.glob(".luma-export-*")), "Export temporary files leaked"
        print("Develop CLI/GUI, Undo/Redo, restart-identical exports, embedded ICC, TIFF16 and original/destination integrity passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
