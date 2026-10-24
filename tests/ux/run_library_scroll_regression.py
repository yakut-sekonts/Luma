#!/usr/bin/env python3
"""Bounded BUG-035 regression; does not qualify physical trackpad input or load behavior."""
import hashlib
from pathlib import Path

import run_ux_suite as runner

IDENTIFIER = 'CANDIDATE-LIBRARY-SELECTION-SCROLL'
FILMSTRIP_IDENTIFIER = 'CANDIDATE-FILMSTRIP-SELECTION-SCROLL'
IDENTIFIERS = {IDENTIFIER, FILMSTRIP_IDENTIFIER}
INVENTORY = [{'id': IDENTIFIER, 'severity': 'P1', 'scope': 'CURRENT', 'mode': 'REGRESSION',
              'scenario': 'Library deep-scroll anchor survives selection, opening/return and filename filters across metadata pages.'},
             {'id': FILMSTRIP_IDENTIFIER, 'severity': 'P1', 'scope': 'CURRENT', 'mode': 'REGRESSION',
              'scenario': 'Develop filmstrip retains horizontal anchor and offset after clicks and photo loads; Right-key reveal still works.'}]


def fixtures(folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    checksums = {}
    for index in range(176):
        path = folder / f'Photo-{index:04}.png'
        if path.exists() or path.is_symlink():
            raise ValueError('Library scroll fixtures require a fresh directory')
        runner.png(path, index)
        checksums[str(path.resolve())] = hashlib.sha256(path.read_bytes()).hexdigest()
    return checksums


if __name__ == '__main__':
    raise SystemExit(runner.main(inventory=INVENTORY, executable_ids=IDENTIFIERS,
                                fixture_factory=fixtures, domain='supplemental',
                                default_report_name='ux-library-scroll-report'))
