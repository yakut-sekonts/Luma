"""Calibrated project-owned RAW, neutral migration and durable optical exports."""
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "assets"))
from generate_dng import create_dng
from catalog_fixtures import remove_post_v11_features
from develop import png_info, tiff_info

cli = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix="luma-lens-") as temporary:
    root = Path(temporary)
    source = root / "Lens RAW.DNG"
    create_dng(source, maker="NIKON CORPORATION", model="NIKON D7500",
               lens="AF-S DX Nikkor 18-140mm f/3.5-5.6G ED VR", focal=18, aperture=5.6)
    original = hashlib.sha256(source.read_bytes()).hexdigest()
    catalog = root / "catalog.sqlite"

    def run(*args, expected=0):
        result = subprocess.run([str(cli), *map(str, args), "--catalog", str(catalog)],
                                capture_output=True, text=True, timeout=120)
        assert result.returncode == expected, result.stdout + result.stderr
        return json.loads(result.stdout) if expected == 0 else result

    initial = run("open", source)
    assert initial["optics"]["resolvedProfile"] and not (initial["enabledCorrections"] & 32768)
    profile = initial["optics"]["resolvedProfile"]
    run("develop", "exposure=0.4", "hsl.red.hue=8", "sharpen.amount=35", "noise.color=20",
        "crop.x=0.1", "crop.y=0.1", "crop.width=0.8", "crop.height=0.8", "crop.angle=4")
    run("curve", "red", "0:0.01", "0.5:0.6", "1:1")
    run("rate", 4, initial["asset"])
    before = run("restore")
    for extension in ("png", "jpg", "tif"):
        run("export", root / f"before.{extension}")
    with sqlite3.connect(catalog) as db:
        db.execute("PRAGMA foreign_keys=OFF")
        remove_post_v11_features(db)
        columns = [item[1] for item in db.execute("PRAGMA table_info(edit_documents)")
                   if not item[1].startswith(("lens_", "transform_"))]
        selection = ",".join("6 AS schema_version" if name == "schema_version" else "enabled & 32767 AS enabled" if name == "enabled" else name
                             for name in columns)
        db.execute("DROP TABLE edit_local_masks")
        db.execute("DROP TRIGGER initialize_local_masks")
        db.execute("CREATE TABLE old_edits AS SELECT " + selection + " FROM edit_documents")
        db.execute("DROP TABLE edit_documents")
        db.execute("ALTER TABLE old_edits RENAME TO edit_documents")
        db.execute("DELETE FROM schema_migrations WHERE version>=8")
        db.execute("PRAGMA user_version=7")
        durable = db.execute("SELECT revision,updated_at FROM edit_documents").fetchone()
        children = [db.execute(f"SELECT * FROM {table}").fetchall()
                    for table in ("edit_curve_points", "edit_hsl_bands")]
    assert run("restore") == before, "Neutral migration changed old settings or selection"
    with sqlite3.connect(catalog) as db:
        assert db.execute("PRAGMA user_version").fetchone()[0] == 21
        assert db.execute("SELECT schema_version,lens_profile,lens_distortion,lens_vignetting,lens_ca FROM edit_documents").fetchone() == (8,"",100,100,1)
        assert db.execute("SELECT revision,updated_at FROM edit_documents").fetchone() == durable
        assert db.execute("SELECT rating FROM assets").fetchone()[0] == 4
        for index, table in enumerate(("edit_curve_points", "edit_hsl_bands")):
            assert db.execute(f"SELECT * FROM {table}").fetchall() == children[index]
        assert db.execute("PRAGMA foreign_key_check").fetchall() == []
    for extension in ("png", "jpg", "tif"):
        run("export", root / f"neutral.{extension}")
        assert (root / f"before.{extension}").read_bytes() == (root / f"neutral.{extension}").read_bytes()
    run("enable", "lens")
    for extension in ("png", "jpg", "tif"):
        first, second = root / f"first.{extension}", root / f"second.{extension}"
        run("export", first)
        run("export", second)
        assert first.read_bytes() == second.read_bytes(), "Restart changed optical export"
        assert first.read_bytes() != (root / f"before.{extension}").read_bytes(), "Optics did not affect export"
        if extension == "png":
            dimensions = png_info(first)[:2]
        elif extension == "tif":
            assert tiff_info(first) == dimensions
        else:
            assert b"ICC_PROFILE\0" in first.read_bytes()
    run("lens", "profile", profile)
    assert run("restore")["optics"]["profile"] == profile
    run("develop", "lens.distortion=75", "lens.vignetting=125", "lens.chromaticAberration=0")
    saved = run("restore")
    run("develop", "exposure=1", "lens.chromaticAberration=0.5", expected=1)
    run("lens", "profile", "bad-id", expected=1)
    assert run("restore") == saved, "Invalid optics partially committed"
    with sqlite3.connect(catalog) as db:
        db.execute("PRAGMA ignore_check_constraints=ON")
        db.execute("UPDATE edit_documents SET lens_ca=0.5")
    run("restore", expected=1)
    with sqlite3.connect(catalog) as db:
        assert db.execute("SELECT lens_ca FROM edit_documents").fetchone()[0] == 0.5
        db.execute("UPDATE edit_documents SET lens_ca=0")
    run("disable", "lens")
    run("export", root / "disabled.png")
    assert (root / "disabled.png").read_bytes() == (root / "before.png").read_bytes()
    run("enable", "lens")
    run("develop", "lens.distortion=0", "lens.vignetting=0", "lens.chromaticAberration=0")
    run("export", root / "zero.png")
    assert (root / "zero.png").read_bytes() == (root / "before.png").read_bytes()
    # The migration's INSERT must enforce old and new invariants before COMMIT.
    with sqlite3.connect(catalog) as db:
        db.execute("PRAGMA foreign_keys=OFF")
        remove_post_v11_features(db)
        selection = ",".join("6 AS schema_version" if name == "schema_version" else
                             "9.0 AS exposure" if name == "exposure" else
                             "enabled & 32767 AS enabled" if name == "enabled" else name for name in columns)
        db.execute("CREATE TABLE bad_edits AS SELECT " + selection + " FROM edit_documents")
        db.execute("DROP TABLE edit_documents")
        db.execute("ALTER TABLE bad_edits RENAME TO edit_documents")
        db.execute("DELETE FROM schema_migrations WHERE version>=8")
        db.execute("PRAGMA user_version=7")
    run("restore", expected=1)
    with sqlite3.connect(catalog) as db:
        assert db.execute("PRAGMA user_version").fetchone()[0] == 7
        assert db.execute("SELECT exposure FROM edit_documents").fetchone()[0] == 9
        assert db.execute("SELECT name FROM sqlite_master WHERE name='edit_documents_v7'").fetchall() == []
        assert db.execute("SELECT * FROM edit_curve_points").fetchall() == children[0]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == original
    assert not list(root.glob(".luma-export-*"))
    print("Lens calibration, migration/rollback, neutral exports, PNG/JPEG/TIFF16, restart and original integrity passed")
