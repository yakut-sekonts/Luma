#!/usr/bin/env python3
"""Build UX tests and bind their binary to an unchanged source snapshot."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import subprocess
import tempfile

from run_ux_suite import ROOT, atomic_json, fingerprint


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build-verify')
    args = parser.parse_args()
    build = args.build.resolve()
    # Serialize against native suites: never replace their executable mid-run.
    with (Path(tempfile.gettempdir()) / f'luma-ux-screen-{os.getuid()}.lock').open('a') as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        attestation = build / 'ux-build-provenance.json'
        attestation.unlink(missing_ok=True)
        before = fingerprint(build)
        command = [str(ROOT / '.tools/venv/bin/cmake'), '--build', str(build),
                   '--target', 'luma_ux_tests', '--parallel', '3']
        result = subprocess.run(command, cwd=ROOT, check=False)
        if result.returncode:
            return result.returncode
        after = fingerprint(build)
        if before['source_tree_sha256'] != after['source_tree_sha256']:
            parser.error('Sources changed during the build; no provenance was recorded')
        if not after['test_executable_sha256']:
            parser.error('UX executable is missing after the build')
        atomic_json(attestation, {'schema': 1, 'source_tree_sha256': after['source_tree_sha256'],
                                  'test_executable_sha256': after['test_executable_sha256'],
                                  'source_commit': after['commit'], 'command': command})
        print(json.dumps({'build_provenance': str(attestation), 'source_tree_sha256': after['source_tree_sha256']}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
