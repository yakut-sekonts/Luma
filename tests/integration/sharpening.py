"""Sharpening migration, full-resolution exports, restart and original integrity."""
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
from catalog_fixtures import remove_post_v11_features

cli = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix="luma-sharpening-") as temporary:
    root = Path(temporary)
    source = root / "Sharpening RAW.DNG"
    create_dng(source)
    catalog = root / "catalog.sqlite"
    def run(*args, expected=0):
        result = subprocess.run([str(cli), *map(str, args), "--catalog", str(catalog)],
                                capture_output=True, text=True, timeout=180)
        assert result.returncode == expected, result.stdout + result.stderr
        return json.loads(result.stdout) if expected == 0 else result
    original = hashlib.sha256(source.read_bytes()).hexdigest()
    initial = run("open", source)
    run("develop", "exposure=0.35", "hsl.red.hue=7")
    run("curve", "rgb", "0:0.01", "0.4:0.55", "1:1")
    run("rate", 4, initial["asset"])
    collection = run("collection", "create", "Preserved collection")
    # Record the durable settings and downgrade only the new parent fields/flags
    # to the actual previous layout; all Curve/HSL children stay intact.
    before = run("restore")
    base = root / "before.png"
    run("export", base)
    with sqlite3.connect(catalog) as db:
        db.execute("PRAGMA foreign_keys=OFF")
        remove_post_v11_features(db)
        db.execute("DROP TABLE edit_local_masks")
        db.execute("DROP TRIGGER initialize_local_masks")
        db.execute("CREATE TABLE old_edits AS SELECT asset_id, 3 AS schema_version, exposure, temperature, tint, custom_wb, contrast, highlights, shadows, whites, blacks, vibrance, saturation, enabled & 2047 AS enabled, curve_shadows, curve_darks, curve_lights, curve_highlights, advanced_saved, revision, updated_at FROM edit_documents")
        db.execute("DROP TABLE edit_documents")
        db.execute("ALTER TABLE old_edits RENAME TO edit_documents")
        db.execute("DELETE FROM schema_migrations WHERE version>=5")
        db.execute("PRAGMA user_version=4")
        revision = db.execute("SELECT revision,updated_at FROM edit_documents").fetchone()
        children = [db.execute(f"SELECT * FROM {table}").fetchall()
                    for table in ("edit_curve_points", "edit_hsl_bands")]
    assert run("restore") == before, "Migration changed existing settings/selection"
    with sqlite3.connect(catalog) as db:
        assert db.execute("PRAGMA user_version").fetchone()[0] == 21
        assert db.execute("SELECT schema_version,sharpen_amount,sharpen_radius,sharpen_detail,sharpen_masking FROM edit_documents").fetchone() == (8,0,1,25,0)
        assert db.execute("SELECT revision,updated_at FROM edit_documents").fetchone() == revision
        assert db.execute("SELECT rating FROM assets").fetchone()[0] == 4
        assert db.execute("SELECT COUNT(*) FROM collections").fetchone()[0] == 1
        for index, table in enumerate(("edit_curve_points", "edit_hsl_bands")):
            assert db.execute(f"SELECT * FROM {table}").fetchall() == children[index]
        assert db.execute("PRAGMA foreign_key_check").fetchall() == []
    migrated = root / "migrated.png"
    run("export", migrated)
    assert migrated.read_bytes() == base.read_bytes(), "Neutral migration changed prior pixels"
    run("export", root / "baseline.tif")
    run("export", root / "baseline.jpg")
    run("develop", "sharpen.amount=105", "sharpen.radius=1.8", "sharpen.detail=75", "sharpen.masking=0")
    saved = run("restore")
    assert saved["sharpening"] == {"amount":105,"radius":1.8,"detail":75,"masking":0}
    run("develop", "sharpen.amount=20", "sharpen.radius=nan", expected=1)
    assert run("restore") == saved, "Invalid sharpening partially committed"
    for extension in ("png", "tif", "jpg"):
        first, second = root / f"first.{extension}", root / f"second.{extension}"
        run("export", first);run("export", second)
        assert first.read_bytes() == second.read_bytes(), f"Restart changed {extension} output"
        if extension == "png":
            assert first.read_bytes() != base.read_bytes(), "Sharpening did not affect PNG pixels"
        if extension in ("tif", "jpg"):
            assert first.read_bytes() != (root / f"baseline.{extension}").read_bytes(), f"Sharpening did not affect {extension}"
        if extension == "jpg":
            assert b"ICC_PROFILE\0" in first.read_bytes(), "Sharpened JPEG lost ICC"
    run("disable", "sharpening")
    disabled = root / "disabled.png"
    run("export", disabled)
    assert disabled.read_bytes() == base.read_bytes(), "Disabling did not restore baseline pixels"
    run("enable", "sharpening")
    again = root / "again.png"
    run("export", again)
    assert again.read_bytes() == (root / "first.png").read_bytes(), "Re-enable changed sharpening"
    # A failed migration must retain the entire old catalog, including its version.
    with sqlite3.connect(catalog) as db:
        db.execute("PRAGMA foreign_keys=OFF")
        remove_post_v11_features(db)
        db.execute("CREATE TABLE bad_edits AS SELECT asset_id, 3 AS schema_version, 9.0 AS exposure, temperature, tint, custom_wb, contrast, highlights, shadows, whites, blacks, vibrance, saturation, enabled & 2047 AS enabled, curve_shadows, curve_darks, curve_lights, curve_highlights, advanced_saved, revision, updated_at FROM edit_documents")
        db.execute("DROP TABLE edit_documents")
        db.execute("ALTER TABLE bad_edits RENAME TO edit_documents")
        db.execute("DELETE FROM schema_migrations WHERE version>=5")
        db.execute("PRAGMA user_version=4")
    run("restore", expected=1)
    with sqlite3.connect(catalog) as db:
        assert db.execute("PRAGMA user_version").fetchone()[0] == 4
        assert db.execute("SELECT exposure FROM edit_documents").fetchone()[0] == 9
        assert db.execute("SELECT name FROM sqlite_master WHERE name='edit_documents_v4'").fetchall() == []
        assert db.execute("SELECT * FROM edit_curve_points").fetchall() == children[0]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == original
    assert not list(root.glob(".luma-export-*"))
    print("Sharpening migration/rollback, neutral pixels, full-resolution exports, restart, ICC and original integrity passed")
