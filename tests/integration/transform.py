"""Perspective persistence, transactional migration and full-resolution output."""
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
with tempfile.TemporaryDirectory(prefix="luma-transform-") as temporary:
    root = Path(temporary)
    source = root / "Perspective RAW.DNG"
    create_dng(source, maker="NIKON CORPORATION", model="NIKON D7500",
               lens="AF-S DX Nikkor 18-140mm f/3.5-5.6G ED VR", focal=18, aperture=5.6)
    checksum = hashlib.sha256(source.read_bytes()).hexdigest()
    catalog = root / "catalog.sqlite"

    def run(*args, expected=0):
        result = subprocess.run([str(cli), *map(str, args), "--catalog", str(catalog)], capture_output=True, text=True, timeout=120)
        assert result.returncode == expected, result.stdout + result.stderr
        return json.loads(result.stdout) if expected == 0 else result

    initial = run("open", source)
    run("develop", "exposure=0.4", "hsl.red.hue=8", "noise.color=20", "sharpen.amount=35",
        "crop.x=0.1", "crop.y=0.1", "crop.width=0.8", "crop.height=0.8", "crop.angle=4")
    run("curve", "red", "0:0.01", "0.5:0.6", "1:1")
    run("enable", "lens")
    run("rate", 4, initial["asset"])
    before = run("restore")
    for extension in ("png", "jpg", "tif"):
        run("export", root / f"before.{extension}")
    with sqlite3.connect(catalog) as db:
        db.execute("PRAGMA foreign_keys=OFF")
        remove_post_v11_features(db)
        columns = [row[1] for row in db.execute("PRAGMA table_info(edit_documents)") if not row[1].startswith("transform_")]
        selection = ",".join("7 AS schema_version" if name == "schema_version" else
                             "enabled & 65535 AS enabled" if name == "enabled" else name for name in columns)
        db.execute("DROP TABLE edit_local_masks")
        db.execute("DROP TRIGGER initialize_local_masks")
        db.execute("CREATE TABLE old_edits AS SELECT " + selection + " FROM edit_documents")
        db.execute("DROP TABLE edit_documents")
        db.execute("ALTER TABLE old_edits RENAME TO edit_documents")
        db.execute("DELETE FROM schema_migrations WHERE version>=9")
        db.execute("PRAGMA user_version=8")
        durable = db.execute("SELECT revision,updated_at FROM edit_documents").fetchone()
        children = [db.execute(f"SELECT * FROM {table}").fetchall() for table in ("edit_curve_points", "edit_hsl_bands")]
    assert run("restore") == before, "Neutral Transform migration changed prior settings"
    with sqlite3.connect(catalog) as db:
        assert db.execute("PRAGMA user_version").fetchone()[0] == 21
        assert db.execute("SELECT schema_version,transform_rotate,transform_scale,transform_constrain_crop,transform_guided FROM edit_documents").fetchone() == (8, 0, 100, 1, 0)
        assert db.execute("SELECT revision,updated_at FROM edit_documents").fetchone() == durable
        assert db.execute("SELECT rating FROM assets").fetchone()[0] == 4
        for index, table in enumerate(("edit_curve_points", "edit_hsl_bands")):
            assert db.execute(f"SELECT * FROM {table}").fetchall() == children[index]
        assert db.execute("PRAGMA foreign_key_check").fetchall() == []
    for extension in ("png", "jpg", "tif"):
        run("export", root / f"neutral.{extension}")
        assert (root / f"before.{extension}").read_bytes() == (root / f"neutral.{extension}").read_bytes()
    run("develop", "transform.rotate=8", "transform.horizontal=18", "transform.vertical=-22",
        "transform.aspect=11", "transform.scale=110", "transform.xOffset=7", "transform.yOffset=-4")
    manual = run("restore")
    for extension in ("png", "jpg", "tif"):
        first, second = root / f"first.{extension}", root / f"second.{extension}"
        run("export", first)
        run("export", second)
        assert first.read_bytes() == second.read_bytes(), "Restart changed projective export"
        assert first.read_bytes() != (root / f"before.{extension}").read_bytes(), "Transform had no export effect"
        if extension == "png":
            dimensions = png_info(first)[:2]
        elif extension == "tif":
            assert tiff_info(first) == dimensions
        else:
            assert b"ICC_PROFILE\0" in first.read_bytes()
    run("develop", "exposure=1", "transform.constrainCrop=0.5", expected=1)
    run("develop", "transform.m11=0", expected=1)
    run("develop", "transform.scale=50", "transform.horizontal=100", "transform.vertical=100", "transform.constrainCrop=0", expected=1)
    assert run("restore") == manual, "Invalid/pole-containing transform partially committed"
    # Apply all matrix entries atomically, independent of their argument order.
    run("develop", "transform.m11=0", "transform.m22=0", "transform.m12=1", "transform.m21=-1", "transform.guided=1")
    matrix = run("restore")
    run("export", root / "guided.png")
    run("export", root / "guided-restart.png")
    assert (root / "guided.png").read_bytes() == (root / "guided-restart.png").read_bytes()
    assert matrix["geometry"]["transform.guided"] == 1
    run("disable", "transform")
    run("export", root / "disabled.png")
    assert (root / "disabled.png").read_bytes() == (root / "before.png").read_bytes()
    run("enable", "transform")
    run("disable", "crop")
    run("develop", "transform.rotate=0", "transform.horizontal=0", "transform.vertical=0", "transform.aspect=0",
        "transform.scale=50", "transform.xOffset=0", "transform.yOffset=0", "transform.guided=0", "transform.constrainCrop=0")
    for extension in ("png", "tif"):
        output = root / f"transparent.{extension}"
        run("export", output)
        info = subprocess.check_output(["sips", "-g", "hasAlpha", str(output)], text=True)
        assert "hasAlpha: yes" in info, "Exterior transparency lost in " + extension
    for column in ("transform_guided", "transform_constrain_crop"):
        with sqlite3.connect(catalog) as db:
            old = db.execute(f"SELECT {column} FROM edit_documents").fetchone()[0]
            db.execute("PRAGMA ignore_check_constraints=ON")
            db.execute(f"UPDATE edit_documents SET {column}=0.5")
        run("restore", expected=1)
        with sqlite3.connect(catalog) as db:
            assert db.execute(f"SELECT {column} FROM edit_documents").fetchone()[0] == 0.5
            db.execute(f"UPDATE edit_documents SET {column}=?", (old,))
    with sqlite3.connect(catalog) as db:
        db.execute("PRAGMA foreign_keys=OFF")
        remove_post_v11_features(db)
        selection = ",".join("7 AS schema_version" if name == "schema_version" else "9.0 AS exposure" if name == "exposure" else
                             "enabled & 65535 AS enabled" if name == "enabled" else name for name in columns)
        db.execute("CREATE TABLE bad_edits AS SELECT " + selection + " FROM edit_documents")
        db.execute("DROP TABLE edit_documents")
        db.execute("ALTER TABLE bad_edits RENAME TO edit_documents")
        db.execute("DELETE FROM schema_migrations WHERE version>=9")
        db.execute("PRAGMA user_version=8")
    run("restore", expected=1)
    with sqlite3.connect(catalog) as db:
        assert db.execute("PRAGMA user_version").fetchone()[0] == 8
        assert db.execute("SELECT exposure FROM edit_documents").fetchone()[0] == 9
        assert db.execute("SELECT name FROM sqlite_master WHERE name='edit_documents_v8'").fetchall() == []
        assert db.execute("SELECT * FROM edit_curve_points").fetchall() == children[0]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == checksum
    assert not list(root.glob(".luma-export-*"))
    print("Transform migration/rollback, neutral pixels, manual/matrix exports, transparency, restart and RAW integrity passed")
