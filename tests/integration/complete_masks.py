"""Feature acceptance of phase-0.5 masks in the real Main window."""
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "assets"))
from generate_dng import create_dng

with tempfile.TemporaryDirectory(prefix="luma-complete-masks-") as temporary:
    root = Path(temporary)
    source = root / "Complete.DNG"
    create_dng(source, orientation=6)
    checksum = hashlib.sha256(source.read_bytes()).hexdigest()

    def run(folder, arguments):
        result = subprocess.run(
            [sys.argv[1], "--catalog", str(folder / "catalog.sqlite"),
             "--test-complete-masks", *arguments],
            capture_output=True, text=True,
            env={**os.environ, "QT_QUICK_BACKEND": "software"}, timeout=130,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        assert not any(token in result.stderr for token in (
            "ReferenceError", "Unable to assign", "Binding loop", "TypeError"
        )), result.stderr

    def check_screenshots(folder):
        for suffix in ("", ".range.png", ".color.png", ".radial.png"):
            assert (folder / ("masks.png" + suffix)).stat().st_size > 10000

    run(root, ["--test-edit", str(source), "--test-exposure", ".35",
               "--test-export", str(root / "first.png"),
               "--test-screenshot", str(root / "masks.png")])
    check_screenshots(root)
    with sqlite3.connect(root / "catalog.sqlite") as database:
        assert database.execute("PRAGMA user_version").fetchone() == (21,)
        before = database.execute("SELECT document FROM edit_local_masks").fetchone()[0]
        assert before.startswith("LUMA_LOCAL_MASKS 3 2\n") and "\nS 1 " in before
    run(root, ["--test-restore", ".35", "--test-export", str(root / "second.png")])
    assert (root / "first.png").read_bytes() == (root / "second.png").read_bytes()
    with sqlite3.connect(root / "catalog.sqlite") as database:
        assert database.execute("SELECT document FROM edit_local_masks").fetchone()[0] == before
    for code in ("ru", "zh_CN", "hy", "de", "es", "it", "fr"):
        folder = root / code
        folder.mkdir()
        (folder / "language.json").write_text(json.dumps({"schema": 1, "language": code}))
        run(folder, ["--test-edit", str(source), "--test-exposure", ".35",
                     "--test-window-width", "1000",
                     "--test-export", str(folder / "localized.png"),
                     "--test-screenshot", str(folder / "masks.png")])
        check_screenshots(folder)
    assert hashlib.sha256(source.read_bytes()).hexdigest() == checksum
    print("Complete masks: native eight-locale Brush/Erase/ranges/boolean workflow, "
          "all selection panels, transformed EXIF, lossless persistence and reproducible export passed")
