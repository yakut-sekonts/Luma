"""Crop migration, full-resolution exports, restart and original integrity."""
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

from develop import png_info, tiff_info
cli = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix="luma-crop-") as temporary:
    root = Path(temporary)
    source = root / "Crop RAW.DNG"
    create_dng(source)
    # Add deterministic sensor noise to the project-owned uncompressed Bayer fixture.
    import struct
    data = bytearray(source.read_bytes())
    offset = len(data) - 128 * 96 * 2
    for i in range(128 * 96):
        value = struct.unpack_from("<H", data, offset + i * 2)[0]
        value += ((i * 37 + (i // 128) * 61) % 31 - 15) * 22
        struct.pack_into("<H", data, offset + i * 2, max(513, min(16383, value)))
    source.write_bytes(data)
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
    run("develop", "sharpen.amount=55", "sharpen.radius=1.3", "sharpen.detail=70", "sharpen.masking=20")
    run("disable", "saturation")
    run("develop", "noise.luminance=45", "noise.color=75", "noise.colorDetail=30")
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
        db.execute("CREATE TABLE old_edits AS SELECT asset_id, 5 AS schema_version, exposure, temperature, tint, custom_wb, contrast, highlights, shadows, whites, blacks, vibrance, saturation, enabled & 16383 AS enabled, curve_shadows, curve_darks, curve_lights, curve_highlights, advanced_saved, sharpen_amount, sharpen_radius, sharpen_detail, sharpen_masking, noise_luminance, noise_luminance_detail, noise_luminance_contrast, noise_color, noise_color_detail, noise_color_smoothness, revision, updated_at FROM edit_documents")
        db.execute("DROP TABLE edit_documents")
        db.execute("ALTER TABLE old_edits RENAME TO edit_documents")
        db.execute("DELETE FROM schema_migrations WHERE version>=7")
        db.execute("PRAGMA user_version=6")
        revision = db.execute("SELECT revision,updated_at FROM edit_documents").fetchone()
        children = [db.execute(f"SELECT * FROM {table}").fetchall()
                    for table in ("edit_curve_points", "edit_hsl_bands")]
    assert run("restore") == before, "Migration changed existing settings/selection"
    with sqlite3.connect(catalog) as db:
        assert db.execute("PRAGMA user_version").fetchone()[0] == 21
        assert db.execute("SELECT schema_version,crop_x,crop_y,crop_width,crop_height,crop_angle,crop_aspect,crop_quarter_turns,crop_flip_h,crop_flip_v FROM edit_documents").fetchone() == (8,0,0,1,1,0,0,0,0,0)
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
    run("develop", "crop.x=0.25", "crop.y=0.125", "crop.width=0.5", "crop.height=0.75", "crop.aspect=1.5", "crop.quarterTurns=1", "crop.flipHorizontal=1", "crop.flipVertical=1")
    saved = run("restore")
    assert saved["crop"] == {"x":0.25,"y":0.125,"width":0.5,"height":0.75,"angle":0,"aspect":1.5,"quarterTurns":1,"flipHorizontal":True,"flipVertical":True}
    assert saved["noiseReduction"] == before["noiseReduction"]
    assert saved["sharpening"] == before["sharpening"]
    run("develop", "exposure=2", "crop.quarterTurns=0.5", expected=1)
    assert run("restore") == saved, "Invalid crop partially committed"
    for extension in ("png", "tif", "jpg"):
        first, second = root / f"first.{extension}", root / f"second.{extension}"
        run("export", first);run("export", second)
        assert first.read_bytes() == second.read_bytes(), f"Restart changed {extension} output"
        if extension == "tif":
            assert tiff_info(first) == (48,32), "Crop TIFF16 dimensions wrong"
        if extension == "png":
            assert png_info(first) == (48,32,8), "Crop PNG dimensions wrong"
            assert first.read_bytes() != base.read_bytes(), "Crop did not affect PNG pixels"
        if extension in ("tif", "jpg"):
            assert first.read_bytes() != (root / f"baseline.{extension}").read_bytes(), f"Crop did not affect {extension}"
        if extension == "jpg":
            assert b"ICC_PROFILE\0" in first.read_bytes(), "Cropped JPEG lost ICC"
    run("disable", "crop")
    disabled = root / "disabled.png"
    run("export", disabled)
    assert disabled.read_bytes() == base.read_bytes(), "Disabling did not restore baseline pixels"
    run("enable", "crop")
    again = root / "again.png"
    run("export", again)
    assert again.read_bytes() == (root / "first.png").read_bytes(), "Re-enable changed crop"
    run("develop", "crop.angle=12")
    run("export", root / "straightened.png")
    run("export", root / "straightened.tif")
    run("export", root / "straightened-restart.png")
    assert (root / "straightened.png").read_bytes() == (root / "straightened-restart.png").read_bytes()
    width,height,depth = png_info(root / "straightened.png")
    assert tiff_info(root / "straightened.tif") == (width,height)
    assert width<48 and height<32 and abs(width-1.5*height)<=2
    # Reject corrupt stored integers/booleans without silently truncating them.
    for column in ("crop_quarter_turns", "crop_flip_h", "crop_flip_v"):
        with sqlite3.connect(catalog) as db:
            previous = db.execute(f"SELECT {column} FROM edit_documents").fetchone()[0]
            db.execute("PRAGMA ignore_check_constraints=ON")
            db.execute(f"UPDATE edit_documents SET {column}=0.5")
        run("restore", expected=1)
        with sqlite3.connect(catalog) as db:
            assert db.execute(f"SELECT {column} FROM edit_documents").fetchone()[0] == 0.5
            db.execute(f"UPDATE edit_documents SET {column}=?", (previous,))
    # A failed migration must retain the entire old catalog, including its version.
    with sqlite3.connect(catalog) as db:
        db.execute("PRAGMA foreign_keys=OFF")
        remove_post_v11_features(db)
        db.execute("CREATE TABLE bad_edits AS SELECT asset_id, 5 AS schema_version, 9.0 AS exposure, temperature, tint, custom_wb, contrast, highlights, shadows, whites, blacks, vibrance, saturation, enabled & 16383 AS enabled, curve_shadows, curve_darks, curve_lights, curve_highlights, advanced_saved, sharpen_amount, sharpen_radius, sharpen_detail, sharpen_masking, noise_luminance, noise_luminance_detail, noise_luminance_contrast, noise_color, noise_color_detail, noise_color_smoothness, revision, updated_at FROM edit_documents")
        db.execute("DROP TABLE edit_documents")
        db.execute("ALTER TABLE bad_edits RENAME TO edit_documents")
        db.execute("DELETE FROM schema_migrations WHERE version>=7")
        db.execute("PRAGMA user_version=6")
    run("restore", expected=1)
    with sqlite3.connect(catalog) as db:
        assert db.execute("PRAGMA user_version").fetchone()[0] == 6
        assert db.execute("SELECT exposure FROM edit_documents").fetchone()[0] == 9
        assert db.execute("SELECT name FROM sqlite_master WHERE name='edit_documents_v6'").fetchall() == []
        assert db.execute("SELECT * FROM edit_curve_points").fetchall() == children[0]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == original
    assert not list(root.glob(".luma-export-*"))
    print("Crop migration/rollback, neutral pixels, full-resolution exports, restart, ICC and original integrity passed")
