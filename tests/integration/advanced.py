"""Advanced CLI/desktop processing, atomic edits and restart-identical exports."""
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

cli = Path(sys.argv[1])
gui = cli.parent.parent / "Luma.app/Contents/MacOS/Luma"
with tempfile.TemporaryDirectory(prefix="luma-advanced-") as temporary:
    root = Path(temporary)
    source = root / "Advanced RAW.DNG"
    create_dng(source)
    checksum = hashlib.sha256(source.read_bytes()).hexdigest()
    catalog = root / "advanced.sqlite"
    def run(*args, expected=0, database=catalog):
        result = subprocess.run([str(cli), *map(str, args), "--catalog", str(database)],
                                capture_output=True, text=True, timeout=120)
        assert result.returncode == expected, f"{args}: {result.stdout} {result.stderr}"
        return json.loads(result.stdout) if expected == 0 else result
    run("open", source)
    run("develop", "exposure=0.5", "temperature=5350", "tint=12")
    baseline = root / "baseline.png"
    run("export", baseline)
    run("curve", "rgb", "0:0.02", "0.3:0.2", "0.7:0.8", "1:0.98")
    run("curve", "red", "0:0.05", "0.4:0.5", "1:0.95")
    run("curve", "blue", "0:0.02", "1:1")
    run("develop", "curve.shadows=-20", "curve.lights=35", "hsl.orange.hue=10",
        "hsl.red.saturation=-20", "hsl.blue.luminance=25")
    saved = run("restore")
    assert saved["editSchemaVersion"] == 8
    assert saved["curves"]["rgb"] == [[0,0.02],[0.3,0.2],[0.7,0.8],[1,0.98]]
    assert saved["hsl"]["orange"]["hue"] == 10 and saved["curveParametric"]["lights"] == 35
    run("curve", "rgb", "0:0", "0.4:0.6", "0.4:0.8", "1:1", expected=1)
    run("develop", "curve.lights=80", "hsl.red.hue=nan", expected=1)
    assert run("restore") == saved, "Invalid advanced batch partially committed"
    with sqlite3.connect(catalog) as db:
        db.execute("CREATE TRIGGER fail_hsl BEFORE INSERT ON edit_hsl_bands WHEN NEW.band=3 BEGIN SELECT RAISE(ABORT,'injected'); END")
    run("develop", "exposure=1.7", "hsl.red.hue=30", expected=1)
    assert run("restore") == saved, "A failed advanced write changed the durable edit document"
    with sqlite3.connect(catalog) as db:
        db.execute("DROP TRIGGER fail_hsl")
        assert db.execute("PRAGMA user_version").fetchone()[0] == 21
        assert db.execute("SELECT COUNT(*) FROM edit_hsl_bands").fetchone()[0] == 8
    edited = root / "edited.png"
    run("export", edited)
    assert edited.read_bytes() != baseline.read_bytes(), "Advanced pipeline did not affect exported pixels"
    restarted = root / "restarted.png"
    run("export", restarted)
    assert edited.read_bytes() == restarted.read_bytes(), "Advanced restart changed PNG pixels"
    tiff1, tiff2 = root / "first.tif", root / "second.tif"
    run("export", tiff1); run("export", tiff2)
    assert tiff1.read_bytes() == tiff2.read_bytes(), "Advanced restart changed TIFF16 pixels"
    jpeg = root / "edited.jpg"
    run("export", jpeg)
    assert b"ICC_PROFILE\0" in jpeg.read_bytes(), "Advanced JPEG lost its ICC profile"
    run("disable", "curves"); run("disable", "hsl")
    disabled = root / "disabled.png"
    run("export", disabled)
    assert disabled.read_bytes() == baseline.read_bytes(), "Disabling advanced edits did not restore basic output"
    assert run("restore")["curves"] == saved["curves"], "Disabling erased curve points"
    run("enable", "curves"); run("enable", "hsl")
    resumed = root / "resumed.png"
    run("export", resumed)
    assert resumed.read_bytes() == edited.read_bytes(), "Re-enabling advanced edits changed their rendering"
    gui_catalog = root / "gui.sqlite"
    env = {**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software"}
    first, second = root / "gui-first.png", root / "gui-second.png"
    for args in (
        ["--test-edit", str(source), "--test-exposure", "0.75", "--test-export", str(first)],
        ["--test-restore", "0.75", "--test-export", str(second)],
    ):
        result = subprocess.run([str(gui), "--catalog", str(gui_catalog), "--test-develop", "--test-advanced", *args],
                                env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, result.stdout + result.stderr
        for message in ("ReferenceError", "TypeError", "Unable to assign", "Binding loop detected"):
            assert message not in result.stderr, result.stderr
    assert first.read_bytes() == second.read_bytes(), "Advanced desktop restart changed exported pixels"
    restored = run("restore", database=gui_catalog)
    assert restored["curves"]["rgb"] == [[0,0],[0.4,0.62],[1,1]]
    assert restored["curveParametric"]["lights"] == 15
    assert restored["hsl"]["red"]["hue"] == 12 and restored["hsl"]["blue"]["luminance"] == 25
    assert hashlib.sha256(source.read_bytes()).hexdigest() == checksum
    assert not list(root.glob(".luma-export-*"))
    print("Curves/HSL CLI and desktop gestures, Undo/Redo, atomic save, restart exports and original integrity passed")
