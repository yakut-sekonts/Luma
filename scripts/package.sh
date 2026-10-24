#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
build_directory="${1:-build}"
sdk="$PWD/.tools/qt/6.10.3/macos"
output="$PWD/dist"
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$build_directory/Luma.app/Contents/Info.plist")
if [[ -n "${LUMA_SIGNING_IDENTITY:-}" && -z "${LUMA_NOTARY_PROFILE:-}" ]]; then
    echo 'Developer ID packaging requires LUMA_NOTARY_PROFILE.' >&2
    exit 1
fi
mkdir -p "$output"
# Only generated output in dist is replaced; sources and catalogs are untouched.
if [[ -d "$output/Luma.app" ]]; then
    mv "$output/Luma.app" "$output/Luma.previous.$(date +%s).app"
fi
ditto "$build_directory/Luma.app" "$output/Luma.app"
.tools/venv/bin/python scripts/install-bundled-models.py \
    "${LUMA_BUNDLED_MODELS_DIR:-$build_directory/models}" "$output/Luma.app"
mkdir -p "$output/Luma.app/Contents/Frameworks"
cp "$build_directory/libluma_raw.dylib" "$output/Luma.app/Contents/Frameworks/"
# Exclude Qt's optional PostgreSQL/ODBC drivers and their external dependencies.
# Luma uses the pinned SQLite C API, not those drivers.
"$sdk/bin/macdeployqt" "$output/Luma.app" -qmldir="$PWD/apps/desktop/qml" -always-overwrite -no-strip -appstore-compliant
notices="$output/Luma.app/Contents/Resources/ThirdParty"
mkdir -p "$notices/sources"
cp LICENSE THIRD_PARTY_LICENSES.md "$notices/"
cp "$build_directory/_deps/libraw-src/LICENSE.LGPL" "$notices/LibRaw-LGPL.txt"
cp "$build_directory/_deps/libraw-src/COPYRIGHT" "$notices/LibRaw-COPYRIGHT.txt"
cp "$build_directory/_deps/lcms-src/LICENSE" "$notices/LittleCMS-LICENSE.txt"
test -s "$notices/Sparkle-LICENSE.txt"
test -s "$notices/PLCrashReporter-LICENSE.txt"
mkdir -p "$notices/Lensfun"
cp "$build_directory/_deps/lens_database-src/docs/cc-by-sa-3.0.txt" "$notices/Lensfun/LICENSE.txt"
cp "$build_directory/_deps/lens_database-src/README.md" "$notices/Lensfun/UPSTREAM_README.md"
cp -R "$build_directory/_deps/lens_database-src/data/db" "$notices/Lensfun/"
cp "$build_directory/generated/LensDatabase.h" "$notices/Lensfun/"
cp docs/LENS_DATABASE_NOTICE.md "$notices/Lensfun/NOTICE.md"
# Sources are reused by exact archive/version name after a complete tar validation.
# Interrupted downloads never replace the cached archive.
source_cache="$PWD/.tools/packaging-sources"
mkdir -p "$source_cache"
archive_source() {
    local url="$1" archive="$2"
    if [[ ! -f "$source_cache/$archive" ]]; then
        curl -fL --connect-timeout 15 --max-time 300 --speed-limit 1024 --speed-time 30 --retry 3 \
            "$url" -o "$source_cache/$archive.partial"
        tar -tf "$source_cache/$archive.partial" > /dev/null
        mv "$source_cache/$archive.partial" "$source_cache/$archive"
    fi
    tar -tf "$source_cache/$archive" > /dev/null
    cp "$source_cache/$archive" "$notices/sources/"
}
archive_source https://www.libraw.org/data/LibRaw-0.21.5.tar.gz LibRaw-0.21.5.tar.gz
for module in qtbase qtdeclarative qtsvg; do
    archive="$module-everywhere-src-6.10.3.tar.xz"
    archive_source "https://download.qt.io/archive/qt/6.10/6.10.3/submodules/$archive" "$archive"
    .tools/venv/bin/python scripts/extract-qt-notices.py "$notices/sources/$archive" "$notices/$module"
done
# Optional QML modules cause macdeployqt to copy QtSql drivers, including drivers
# for external DB vendors. Luma uses SQLite's C API and has no QtSql driver usage.
rm -rf "$output/Luma.app/Contents/PlugIns/sqldrivers"
# macdeployqt leaves the build-only Sparkle search path in the main executable.
# All runtime frameworks are already in the bundle; remove absolute build paths.
.tools/venv/bin/python - "$output/Luma.app/Contents/MacOS/Luma" <<'PYTHON'
from pathlib import Path
import re, subprocess, sys
executable = sys.argv[1]
commands = subprocess.check_output(["otool", "-l", executable], text=True)
for path in re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", commands):
    if Path(path).is_absolute():
        subprocess.run(["install_name_tool", "-delete_rpath", path, executable], check=True)
PYTHON
.tools/venv/bin/python - "$output/Luma.app" <<'PYTHON'
import hashlib, json, os, plistlib, subprocess, sys
from pathlib import Path
app = Path(sys.argv[1])
with (app / "Contents/Info.plist").open("rb") as file:
    version = plistlib.load(file)["CFBundleShortVersionString"]
commit = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
signed = bool(os.environ.get("LUMA_SIGNING_IDENTITY"))
models_manifest = (app / "Contents/Resources/Models/manifest.json").read_bytes()
info = {"version": version, "source_commit": commit, "platform": "macOS 14+", "architecture": "arm64",
        "distribution": "github-open-source",
        "signing": "developer-id" if signed else "ad-hoc",
        "bundled_models": json.loads(models_manifest)["models"],
        "models_manifest_sha256": hashlib.sha256(models_manifest).hexdigest(),
        "dependencies": {"Qt": "6.10.3", "LibRaw": "0.21.5", "SQLite": "3.50.4", "LittleCMS": "2.17", "Sparkle": "2.10.0", "LensfunDatabase": "0.3.4", "PLCrashReporter": "1.12.2"}}
if signed:
    info["notarization_policy"] = "required; acceptance recorded in release BUILD_INFO.json"
else:
    info["notarized"] = False
(app / "Contents/Resources/BUILD_INFO.json").write_text(json.dumps(info, indent=2) + "\n")
PYTHON
if [[ -n "${LUMA_SIGNING_IDENTITY:-}" ]]; then
    .tools/venv/bin/python scripts/sign-app.py app "$output/Luma.app"
else
    codesign --force --deep --sign - "$output/Luma.app"
fi
codesign --verify --deep --strict "$output/Luma.app"
ditto -c -k --sequesterRsrc --keepParent "$output/Luma.app" "$output/Luma-macos-arm64.zip"
(cd "$output" && shasum -a 256 Luma-macos-arm64.zip > SHA256SUMS)
echo "Packaged: $output/Luma.app"
