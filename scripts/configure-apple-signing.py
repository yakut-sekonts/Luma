"""Prepare/clean an isolated signing Keychain on an ephemeral Actions runner.

Credentials come only from explicitly configured Actions secrets. No workstation
credential lookup is performed. Sensitive subprocess output is never echoed.
"""
import base64
import json
import os
from pathlib import Path
import secrets
import shlex
import shutil
import subprocess
import sys
import tempfile


def command(*arguments):
    result = subprocess.run(arguments, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("Apple signing setup command failed; credentials were not printed.")
    return result.stdout


def state_path():
    if os.environ.get("GITHUB_ACTIONS") != "true" or not os.environ.get("RUNNER_TEMP"):
        raise RuntimeError("This setup is restricted to an Actions runner.")
    return Path(os.environ["RUNNER_TEMP"]).resolve() / "luma-apple-signing.json"


def clean(state):
    if not state.exists():
        return
    configuration = json.loads(state.read_text())
    folder = Path(configuration["folder"])
    if folder.parent != state.parent or not folder.name.startswith("luma-apple-"):
        raise RuntimeError("Signing cleanup path is not owned by this job.")
    # Restore the original search list before deleting our isolated Keychain.
    command("security", "list-keychains", "-d", "user", "-s", *configuration["previous_keychains"])
    keychain = folder / "signing.keychain-db"
    if keychain.exists():
        command("security", "delete-keychain", str(keychain))
    shutil.rmtree(folder)
    state.unlink()


def configure(state):
    if state.exists():
        raise RuntimeError("A signing setup already exists for this runner; clean it before retrying.")
    names = ("APPLE_CERTIFICATE_P12", "APPLE_CERTIFICATE_PASSWORD", "APPLE_NOTARY_KEY_P8",
             "APPLE_NOTARY_KEY_ID", "APPLE_NOTARY_ISSUER_ID", "APPLE_SIGNING_IDENTITY")
    values = {name: os.environ.get(name, "") for name in names}
    if not all(values.values()):
        raise RuntimeError("Developer ID certificate and notarization credentials are incomplete.")
    identity = values["APPLE_SIGNING_IDENTITY"]
    if not identity.startswith("Developer ID Application:") or "\n" in identity or "\r" in identity:
        raise RuntimeError("Invalid Developer ID Application identity.")
    folder = Path(tempfile.mkdtemp(prefix="luma-apple-", dir=state.parent))
    folder.chmod(0o700)
    keychain = folder / "signing.keychain-db"
    previous = shlex.split(command("security", "list-keychains", "-d", "user"))
    state.write_text(json.dumps({"folder": str(folder), "previous_keychains": previous}))
    state.chmod(0o600)
    certificate, api_key = folder / "certificate.p12", folder / "AuthKey.p8"
    password = secrets.token_urlsafe(32)
    try:
        certificate.write_bytes(base64.b64decode(values["APPLE_CERTIFICATE_P12"], validate=True))
        api_key.write_text(values["APPLE_NOTARY_KEY_P8"])
        certificate.chmod(0o600); api_key.chmod(0o600)
        command("security", "create-keychain", "-p", password, str(keychain))
        command("security", "set-keychain-settings", "-lut", "21600", str(keychain))
        command("security", "unlock-keychain", "-p", password, str(keychain))
        command("security", "import", str(certificate), "-k", str(keychain), "-P",
                values["APPLE_CERTIFICATE_PASSWORD"], "-T", "/usr/bin/codesign")
        command("security", "set-key-partition-list", "-S", "apple-tool:,apple:,codesign:", "-s", "-k", password, str(keychain))
        command("security", "list-keychains", "-d", "user", "-s", str(keychain), *previous)
        profile = "luma-release"
        command("xcrun", "notarytool", "store-credentials", profile, "--key", str(api_key),
                "--key-id", values["APPLE_NOTARY_KEY_ID"], "--issuer", values["APPLE_NOTARY_ISSUER_ID"],
                "--keychain", str(keychain))
        with Path(os.environ["GITHUB_ENV"]).open("a") as environment:
            environment.write(f"LUMA_SIGNING_IDENTITY={identity}\nLUMA_NOTARY_PROFILE={profile}\nLUMA_NOTARY_KEYCHAIN={keychain}\n")
    except Exception:
        clean(state)
        raise
    finally:
        certificate.unlink(missing_ok=True)
        api_key.unlink(missing_ok=True)


if __name__ == "__main__":
    try:
        state = state_path()
        if sys.argv[1:] == ["--clean"]:
            clean(state)
        elif not sys.argv[1:]:
            configure(state)
        else:
            raise RuntimeError("Expected no arguments or --clean.")
    except Exception as error:
        # Base64/subprocess exceptions can include sensitive input; emit only
        # our controlled operational messages.
        print(str(error) if isinstance(error, RuntimeError) else "Apple signing setup failed.", file=sys.stderr)
        raise SystemExit(1)
