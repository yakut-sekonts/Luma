"""Sign a prepared bundle from the inside out, then notarize and staple it.

Uses an explicitly configured Developer ID identity and notarytool Keychain
profile. Does not retrieve, print or persist credential values.
"""
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile


def run(*arguments):
    subprocess.run(arguments, check=True)


def notarize(path, profile):
    arguments = [
        "xcrun", "notarytool", "submit", str(path), "--keychain-profile", profile,
        "--wait", "--timeout", "20m", "--output-format", "json",
    ]
    if os.environ.get("LUMA_NOTARY_KEYCHAIN"):
        arguments += ["--keychain", os.environ["LUMA_NOTARY_KEYCHAIN"]]
    result = subprocess.run(arguments, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit("Notarization did not complete successfully; no release may be published.")
    record = json.loads(result.stdout)
    if record.get("status") != "Accepted" or not record.get("id"):
        raise SystemExit("Apple did not accept the notarization submission.")
    return {"submission_id": record["id"], "status": record["status"]}


def sign_app(app, identity, profile, report):
    entitlements = Path("platform/macos/Release.entitlements").resolve()
    app = app.resolve(strict=True)
    if app.suffix != ".app" or not identity.startswith("Developer ID Application:"):
        raise SystemExit("A prepared app bundle and Developer ID Application identity are required.")
    with (app / "Contents/Info.plist").open("rb") as source:
        version = plistlib.load(source)["CFBundleShortVersionString"]
    info = json.loads((app / "Contents/Resources/BUILD_INFO.json").read_text())
    if info.get("signing") != "developer-id" or info.get("version") != version:
        raise SystemExit("Build provenance does not match Developer ID signing.")
    # Never retain an old success record after a failed new attempt.
    report.unlink(missing_ok=True)
    code = set()
    magic = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
             b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
             b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
    for directory, folders, files in os.walk(app, followlinks=False):
        folders[:] = [name for name in folders if not (Path(directory) / name).is_symlink()]
        root = Path(directory)
        if root.suffix in {".framework", ".xpc", ".app"}:
            code.add(root)
        elif root.suffix == ".bundle" and (root / "Contents/Info.plist").exists():
            with (root / "Contents/Info.plist").open("rb") as source:
                if plistlib.load(source).get("CFBundleExecutable"):
                    code.add(root)
        for name in files:
            path = root / name
            if path.is_symlink():
                continue
            with path.open("rb") as source:
                if source.read(4) in magic:
                    code.add(path)
    for path in sorted(code, key=lambda item: (-len(item.parts), str(item))):
        arguments = ["codesign", "--force", "--sign", identity, "--timestamp", "--options", "runtime"]
        if path == app or path == app / "Contents/MacOS/Luma":
            arguments += ["--entitlements", str(entitlements)]
        elif "Downloader.xpc" in path.parts:
            # Sparkle's Downloader may carry its own sandbox configuration.
            arguments += ["--preserve-metadata=entitlements"]
        run(*arguments, str(path))
    run("codesign", "--verify", "--deep", "--strict", str(app))
    with tempfile.TemporaryDirectory(prefix="luma-notary-", dir=app.parent) as directory:
        archive = Path(directory) / "submission.zip"
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(archive))
        accepted = notarize(archive, profile)
    run("xcrun", "stapler", "staple", str(app))
    run("xcrun", "stapler", "validate", str(app))
    run("spctl", "--assess", "--type", "execute", "--verbose=2", str(app))
    report.write_text(json.dumps({"version": version, "source_commit": info["source_commit"], "app": accepted}, indent=2) + "\n")


def sign_dmg(path, identity, profile, report):
    record = json.loads(report.read_text())
    if record.get("app", {}).get("status") != "Accepted" or path.name != f"Luma-{record['version']}-macos-arm64.dmg":
        raise SystemExit("DMG notarization requires this version's accepted app.")
    record.pop("dmg", None)
    report.write_text(json.dumps(record, indent=2) + "\n")
    run("codesign", "--force", "--sign", identity, "--timestamp", str(path))
    accepted = notarize(path, profile)
    run("xcrun", "stapler", "staple", str(path))
    run("xcrun", "stapler", "validate", str(path))
    record["dmg"] = accepted
    report.write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    identity = os.environ.get("LUMA_SIGNING_IDENTITY", "")
    profile = os.environ.get("LUMA_NOTARY_PROFILE", "")
    if not identity or not profile or len(sys.argv) != 3 or sys.argv[1] not in {"app", "dmg"}:
        raise SystemExit("Set LUMA_SIGNING_IDENTITY and LUMA_NOTARY_PROFILE, then pass app|dmg and its path.")
    output = Path("dist/notarization.json")
    if sys.argv[1] == "app":
        sign_app(Path(sys.argv[2]), identity, profile, output)
    else:
        sign_dmg(Path(sys.argv[2]), identity, profile, output)
