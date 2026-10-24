#!/usr/bin/env python3
"""Check defined renderer and observer symbols; never launches either application."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect_binary(argument):
    path = Path(argument).resolve(strict=True)
    if not path.is_file():
        raise ValueError(f"Expected an executable file: {path}")
    before = digest(path)
    completed = subprocess.run(
        ["/usr/bin/nm", "-gU", str(path)],
        check=True,
        capture_output=True,
        text=True,
        timeout=10,
    )
    all_defined = subprocess.run(
        ["/usr/bin/nm", "-U", str(path)],
        check=True,
        capture_output=True,
        text=True,
        timeout=10,
    )
    after = digest(path)
    if before != after:
        raise RuntimeError(f"Executable changed during symbol inspection: {path}")
    # nm -gU exposes only defined external symbols. Compare mangled names;
    # symbol addresses and linker layout may differ between the two binaries.
    names = sorted({line.split()[-1] for line in completed.stdout.splitlines() if line.split()})
    # Inline C++ observers may have local definitions after linking. Include
    # these when checking both presence in UX and absence from shipping; keep
    # renderer signature equivalence restricted to the original global symbols.
    defined = sorted({line.split()[-1] for line in all_defined.stdout.splitlines() if line.split()})
    original = [name for name in names if name.lstrip("_").startswith("ZN4luma9renderCPUE")]
    alternate = [name for name in names if "renderCPUWithUxRows" in name]
    observers = [name for name in defined if "CpuRowObserver" in name or "thumbnail_trace" in name
                 or "LoadProbe" in name or "LoadResultGate" in name
                 or "holdLoadSuccess" in name or "observeLoadSuccessDelivery" in name
                 or "PreviewResult" in name or "PreviewOperation" in name
                 or "deferPreviewSuccess" in name or "observePreview" in name
                 or "MetalPreviewProbe" in name or "MergeJobProbe" in name
                 or "RetouchResult" in name or "deferRetouchSuccess" in name
                 or "observeRetouchDelivery" in name or "PersistenceGate" in name
                 or "PersistenceState" in name or "observePersistenceCheckpoint" in name]
    if not original:
        raise RuntimeError(f"Original renderCPU definition is absent: {path}")
    return {
        "path": str(path),
        "sha256": before,
        "original_renderer_symbols": original,
        "alternate_renderer_symbols": alternate,
        "test_observer_symbols": observers,
        "observer_symbol_scope": "all_defined_including_local",
        "nm_stderr": completed.stderr,
        "observer_nm_stderr": all_defined.stderr,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shipping", required=True)
    parser.add_argument("--ux", required=True)
    args = parser.parse_args()
    result = {
        "schema": 1,
        "scope": "Global renderer signatures and all defined observer symbols; no signing, distribution or runtime qualification",
    }
    try:
        shipping = inspect_binary(args.shipping)
        ux = inspect_binary(args.ux)
        result.update(shipping=shipping, ux=ux)
        if shipping["path"] == ux["path"]:
            raise RuntimeError("Shipping and UX executables must be distinct files")
        if shipping["alternate_renderer_symbols"] or shipping["test_observer_symbols"]:
            raise RuntimeError("UX renderer or thumbnail observation symbols leaked into shipping")
        if not any("deferPreviewSuccess" in name for name in ux["test_observer_symbols"]):
            raise RuntimeError("UX actual preview dispatch probe is absent")
        if not any("MetalPreviewProbe" in name for name in ux["test_observer_symbols"]):
            raise RuntimeError("UX actual Metal observation probe is absent")
        if not any("MergeJobProbe" in name for name in ux["test_observer_symbols"]):
            raise RuntimeError("UX actual merge handle probe is absent")
        if not any("deferRetouchSuccess" in name for name in ux["test_observer_symbols"]):
            raise RuntimeError("UX actual retouch dispatch probe is absent")
        if not any("observePersistenceCheckpoint" in name for name in ux["test_observer_symbols"]):
            raise RuntimeError("UX actual filesystem persistence probe is absent")
        if not ux["alternate_renderer_symbols"]:
            raise RuntimeError("UX alternate renderer definition is absent")
        if shipping["original_renderer_symbols"] != ux["original_renderer_symbols"]:
            raise RuntimeError("Shipping and UX original renderer signatures differ")
        result["status"] = "PASS"
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        result.update(status="FAIL", error=str(error))
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
