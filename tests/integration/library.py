"""Public Library acceptance: originals, copy, organization, GUI, restart and migration."""
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


def main():
    cli, unit = Path(sys.argv[1]), Path(sys.argv[2])
    gui = cli.parent.parent / "Luma.app/Contents/MacOS/Luma"
    with tempfile.TemporaryDirectory(prefix="luma-library-acceptance-") as temporary:
        root = Path(temporary)
        sources = root / "sources"
        sources.mkdir()
        a, b = sources / "Кадр А.DNG", sources / "Кадр Б.dng"
        create_dng(a)
        data = bytearray(a.read_bytes())
        data[-2:] = b"\0\0"
        b.write_bytes(data)
        hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in [a, b]}
        result = subprocess.run([str(unit), str(a)], capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout.strip())
        catalog = root / "catalog.sqlite"

        def run(*args, expected=0):
            result = subprocess.run([str(cli), *map(str, args), "--catalog", str(catalog)], capture_output=True, text=True, timeout=90)
            assert result.returncode == expected, result.stdout + result.stderr
            return json.loads(result.stdout) if not expected else result

        assert run("import", "add", sources)["added"] == 2
        assert run("import", "add", sources)["duplicates"] == 2
        items = run("library")["items"]
        ids = [item["id"] for item in items]
        run("rate", 5, *ids)
        run("flag", 1, *ids)
        run("label", "blue", *ids)
        run("rate", 1, ids[0], "missing", expected=1)
        assert run("library", "--rating", 5)["total"] == 2
        collection = run("collection", "create", "Избранное")["id"]
        run("collection", "add", collection, *ids)
        assert run("collections")[0]["count"] == 2
        assert run("library", "--collection", collection, "--flag", 1, "--label", "blue")["total"] == 2
        assert run("library", "--search", "%")["total"] == 0
        run("collection", "rename", collection, "Отбор")
        run("open", a)
        run("develop", "exposure=1.25", "contrast=17")
        run("collection", "delete", collection)
        assert run("restore")["exposure"] == 1.25
        with sqlite3.connect(catalog) as db:
            assert db.execute("PRAGMA user_version").fetchone()[0] == 21
            assert db.execute("SELECT schema_version FROM edit_documents LIMIT 1").fetchone()[0] == 8
        destination = root / "copy"
        destination.mkdir()
        copy_catalog = root / "copy.sqlite"
        result = subprocess.run([str(cli), "import", "copy", str(destination), str(sources), "--catalog", str(copy_catalog)], capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, result.stderr
        assert json.loads(result.stdout)["added"] == 2
        with sqlite3.connect(copy_catalog) as db:
            for path, checksum in db.execute("SELECT original_path,checksum FROM assets"):
                assert Path(path).is_relative_to(destination.resolve())
                assert hashlib.sha256(Path(path).read_bytes()).hexdigest() == checksum
        gui_catalog = root / "gui.sqlite"
        env = {**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software"}
        result = subprocess.run([str(gui), "--catalog", str(gui_catalog), "--test-library", str(sources), "--test-screenshot", str(root / "library.png")], capture_output=True, text=True, env=env, timeout=100)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "Library GUI acceptance passed" in result.stdout
        # Treat missing QML bindings as failures, not cosmetic log noise.
        assert "ReferenceError" not in result.stderr and "Unable to assign" not in result.stderr, result.stderr
        with sqlite3.connect(gui_catalog) as db:
            assert db.execute("SELECT COUNT(*) FROM assets WHERE rating=4 AND flag=1 AND color_label='blue'").fetchone()[0] == 2
            assert db.execute("SELECT COUNT(*) FROM collection_assets").fetchone()[0] == 2
            assert db.execute("SELECT exposure FROM edit_documents JOIN session ON session.asset_id=edit_documents.asset_id").fetchone()[0] == 1.75
        thumbnails = list((root / "gui.sqlite.luma-cache/thumbnails").glob("*.jpg"))
        assert len(thumbnails) >= 3 and all(p.name.endswith("-edited-v4.jpg") for p in thumbnails), "Saved-edit thumbnails did not include both photos and the edited revision"
        close_catalog = root / "close.sqlite"
        result = subprocess.run([str(gui), "--catalog", str(close_catalog), "--test-library", str(sources), "--test-close-import"], capture_output=True, text=True, env=env, timeout=100)
        assert result.returncode == 0, result.stdout + result.stderr
        assert all(hashlib.sha256(p.read_bytes()).hexdigest() == checksum for p, checksum in hashes.items())
        print("Library: Add/Copy, journal recovery/cancel, atomic metadata, collections, filters, GUI and restart passed")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
