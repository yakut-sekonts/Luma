#!/usr/bin/env python3
"""Capability-aware UX qualification; unexecuted checks never count as passes."""
from __future__ import annotations
import argparse
import fcntl
import tempfile
from collections import Counter
from contextlib import closing
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import signal
import sqlite3
import struct
import subprocess
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
INVENTORY = json.loads((Path(__file__).with_name('scenarios.json')).read_text())
MANIFEST = json.loads((Path(__file__).with_name('capabilities.json')).read_text())
NATIVE = {
    'UX-APP-002', 'UX-APP-013', 'UX-NAV-001', 'UX-NAV-003',
    *[f'UX-SEL-{i:03}' for i in range(1, 6)],
    'UX-SLD-001', 'UX-SLD-003', 'UX-SLD-004', 'UX-SLD-005', 'UX-SLD-006',
    'UX-DSC-001', 'UX-DSC-003', 'UX-PRV-001', 'UX-PRV-003',
    'UX-SEL-007', 'UX-SEL-008', 'UX-SEL-009', 'UX-SEL-010', 'UX-SEL-011', 'UX-SEL-012', 'UX-KEY-005',
    'UX-FLT-001', 'UX-FLT-003', 'UX-FLT-004', 'UX-FLT-005', 'UX-FLT-006', 'UX-FLT-008', 'UX-FLT-009',
    'UX-FLT-010', 'UX-FLT-012', 'UX-FLT-013',
}
NATIVE.update(f'UX-KEY-{i:03}' for i in (7, 8, 9, 10, 11, 16, 17))
NATIVE.update(f'UX-FLT-{i:03}' for i in (7, 11, 14, 15))
NATIVE.update(f'UX-NAV-{i:03}' for i in (5, 6, 7, 10, 11, 12, 14, 15))
NATIVE.update(f'UX-TRN-{i:03}' for i in (2, 5, 9, 10, 11, 12, 13, 14))

NATIVE.update(f'UX-MSK-{i:03}' for i in (1,2,3,4,6,7,9,10,11,13,14,15,16,17,19,20,21,22,24,26,27,28,29,31,32,33,34,35,36,37,38))

NATIVE.update(('UX-APP-006', 'UX-APP-007', 'UX-NAV-008', 'UX-NAV-009', 'UX-NAV-013', 'UX-KEY-002',
               'UX-SLD-007', 'UX-SLD-008', 'UX-SLD-019', 'UX-UND-001', 'UX-UND-002', 'UX-UND-003', 'UX-UND-004', 'UX-UND-005', 'UX-UND-010'))
NATIVE.add('UX-PRV-013')
NATIVE.add('UX-PRV-006')
NATIVE.update(f'UX-IMP-{i:03}' for i in (1, 2, 3, 4, 6, 7, 8, 9, 13, 14, 15, 16))
NATIVE.add('UX-APP-008')
NATIVE.update(f'UX-REC-{i:03}' for i in (1, 2, 3, 4, 5, 6, 12))
NATIVE.update(f'UX-EXP-{i:03}' for i in (1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 13, 15, 18, 29, 30))
NATIVE.add('UX-APP-009')
RESTART = {'UX-APP-001', 'UX-APP-003', 'UX-APP-005', 'UX-APP-007',
           'UX-MSK-008', 'UX-MSK-040', 'UX-UND-013', 'UX-UND-014', 'UX-UND-015', 'UX-REC-007',
           'UX-DTL-012', 'UX-SLD-020', 'UX-CRP-018', 'UX-LNS-010', 'UX-TRN-015', 'UX-IMP-020', 'UX-LOC-006', 'UX-LOC-007'}
KILL = {'UX-UND-014', 'UX-UND-015', 'UX-REC-007', 'UX-IMP-020'}
RESTART.update(('UX-PRS-011', 'UX-PRS-018', 'UX-REC-009', 'UX-APP-004', 'UX-SLD-016'))
NATIVE.update(RESTART)
NATIVE.update(f'UX-BAT-{i:03}' for i in (1, 2, 3, 5, 6, 8, 9, 10, 12, 15))
NATIVE.add('UX-APP-010')
NATIVE.update(('UX-UND-011', 'UX-UND-012'))
NATIVE.update(('UX-CRP-001', 'UX-CRP-002', 'UX-UND-006'))
NATIVE.add('UX-KEY-003')
NATIVE.add('UX-DSC-007')
NATIVE.update(f'UX-DTL-{i:03}' for i in (1, 2, 3, 4, 5, 6, 8, 9))
NATIVE.update(f'UX-LNS-{i:03}' for i in (1, 2, 3, 5, 6, 7, 8))
NATIVE.update(f'UX-ADV-{i:03}' for i in (1, 2, 4, 5, 6, 7, 8, 10, 11, 14))
NATIVE.update(('UX-PRS-001', 'UX-PRS-004', 'UX-PRS-005', 'UX-RACE-001', 'UX-RACE-004'))
NATIVE.update(f'UX-TRN-{i:03}' for i in (1, 3, 4, 6, 7, 8))
NATIVE.update(('UX-LIB-001', 'UX-LIB-010', 'UX-LIB-011', 'UX-LOC-001', 'UX-LOC-002'))
NATIVE.update(('UX-UPD-002', 'UX-UPD-003', 'UX-UPD-004'))
NATIVE.add('UX-KEY-001')
NATIVE.update(f'UX-LOC-{i:03}' for i in (3,4,5,8))
NATIVE.update(f'UX-CRP-{i:03}' for i in (3,4,5,6,7,8,9,11,12,13,14,17))
NATIVE.update(('UX-KEY-012', 'UX-KEY-013', 'UX-KEY-014'))
NATIVE.update(('UX-LIB-012', 'UX-FLT-002'))
NATIVE.update(f'UX-SEL-{i:03}' for i in (6,13,14,15,16,17,18,19))
NATIVE.update(('UX-KEY-004', 'UX-KEY-006'))
NATIVE.update(('UX-UND-007', 'UX-UND-008', 'UX-UND-009', 'UX-PRV-004'))
NATIVE.update(f'UX-PRS-{i:03}' for i in (7, 8, 14, 15, 16))
NATIVE.update(('UX-UND-016', 'UX-ADV-013', 'UX-REC-008'))
NATIVE.update(f'UX-SLD-{i:03}' for i in (12, 13, 14, 18))
NATIVE.update(f'UX-PRS-{i:03}' for i in (2, 3, 6, 9, 10, 12, 17, 19))
NATIVE.update(('UX-PRV-007', 'UX-PRV-008', 'UX-ADV-003', 'UX-ADV-009',
               'UX-ADV-015', 'UX-RACE-006', 'UX-RACE-008'))
NATIVE.update(('UX-IMP-005', 'UX-IMP-010', 'UX-IMP-011', 'UX-IMP-012', 'UX-IMP-019',
               'UX-NAV-002', 'UX-PRV-002', 'UX-ADV-012', 'UX-BAT-013', 'UX-PERF-006'))
NATIVE.update(('UX-SLD-015', 'UX-SLD-017', 'UX-DSC-004', 'UX-APP-012', 'UX-APP-015'))
NATIVE.update(f'UX-EXP-{i:03}' for i in (9, 14, 16, 19, 21, 22))
NATIVE.update(('UX-DSC-005', 'UX-DSC-006', 'UX-DSC-008', 'UX-DSC-009',
               'UX-SLD-010', 'UX-SLD-011', 'UX-LNS-009'))
NATIVE.update(f'UX-VIS-{i:03}' for i in (1, 2, 4, 5, 6, 9, 10, 11, 12))
NATIVE.update(('UX-REC-011', 'UX-EXP-011'))
NATIVE.update(f'UX-LIB-{i:03}' for i in (8, 14, 15, 16, 17))
NATIVE.add('UX-RACE-009')
GLOBAL = {'UX-UND-018'}

CASE_TIMEOUTS = {'UX-UND-016': 600, 'UX-ADV-013': 600, 'UX-PRS-019': 900,
                 'UX-IMP-019': 600, 'UX-NAV-002': 300, 'UX-PRV-002': 300, 'UX-ADV-012': 600,
                 'UX-APP-012': 600, 'UX-APP-015': 600, 'UX-EXP-021': 600,
                 'UX-LIB-017': 600, 'UX-LIB-008': 300, 'UX-LIB-014': 300,
                 'UX-LIB-015': 300, 'UX-LIB-016': 300, 'UX-RACE-009': 300,
                 'UX-VIS-004': 600, 'UX-EXP-011': 600, 'UX-VIS-005': 600, 'UX-VIS-009': 300,
                 'UX-VIS-011': 300, 'UX-VIS-012': 300, 'UX-REC-011': 300}


def scenario_timeout(identifier, arguments):
    explicit = getattr(arguments, 'timeout', None)
    return explicit if explicit is not None else CASE_TIMEOUTS.get(identifier, 180)




def atomic_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + '.partial')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
    temporary.replace(path)


def changed_originals(checksums):
    return [name for name, digest in checksums.items()
            if not Path(name).is_file() or hashlib.sha256(Path(name).read_bytes()).hexdigest() != digest]


def in_scope(scope, through):
    if scope in ('ALL', 'CURRENT'):
        return True
    if scope == 'TARGET':
        return False
    return tuple(map(int, scope.split('.'))) <= tuple(map(int, through.split('.')))


def png(path, index):
    width, height = 256, 192
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    pixels = b''.join(b'\0' + bytes(v for x in range(width)
                                 for v in ((x + index * 17) % 256, (y * 2 + index) % 256,
                                           (x + y + index) % 256, 255)) for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


def fixtures(folder, count=1000):
    folder.mkdir(parents=True, exist_ok=True)
    for index in range(count):
        path = folder / f'Photo-{index:04}.png'
        if not path.exists():
            png(path, index)
    stress = folder / 'stress'
    stress.mkdir(exist_ok=True)
    large = stress / 'Large.png'
    if not large.exists():
        width, height = 6000, 4000
        compressor = zlib.compressobj(1)
        row = b'\0' + bytes((80, 120, 160, 255)) * width
        compressed = b''.join(compressor.compress(row) for _ in range(height)) + compressor.flush()
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
        large.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                          + chunk(b'IDAT', compressed) + chunk(b'IEND', b''))
    lens = stress / 'Lens.DNG'
    if not lens.exists():
        specification = importlib.util.spec_from_file_location('luma_ux_dng', ROOT / 'tests/assets/generate_dng.py')
        generator = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(generator)
        generator.create_dng(lens, maker='NIKON CORPORATION', model='NIKON D7500',
                             lens='AF-S DX Nikkor 18-140mm f/3.5-5.6G ED VR', focal=18, aperture=5.6)
    building = stress / 'Building.DNG'
    if not building.exists():
        specification = importlib.util.spec_from_file_location('luma_ux_geometry_dng', ROOT / 'tests/assets/generate_dng.py')
        generator = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(generator)
        # Public calibrated projective building, also used by transform acceptance.
        def facade(x, y):
            u, v = 2*x/319-1, 2*y/239-1
            a, b, c, d = 0.85-u*0.13, -0.06+u*0.12, 0.04-v*0.13, 0.85+v*0.12
            sx, sy = (u*d-b*v)/(a*d-b*c), (a*v-u*c)/(a*d-b*c)
            edge = ((abs(abs(sx)-0.55) < 0.014 and abs(sy) < 0.8)
                    or (abs(abs(sy)-0.5) < 0.014 and abs(sx) < 0.8))
            return 0.02 if edge else 0.7
        generator.create_dng(building, width=320, height=240, sample=facade)
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.rglob('*')
            if p.is_file() and p.suffix.lower() in ('.png', '.dng')}


def fingerprint(build):
    def command(*args):
        result = subprocess.run(args, capture_output=True, text=True, check=False)
        return result.stdout.strip() if result.returncode == 0 else 'unavailable'
    paths = subprocess.run(['git', '-C', str(ROOT), 'ls-files', '-co', '--exclude-standard', '-z'],
                           capture_output=True, check=True).stdout.split(b'\0')
    source = hashlib.sha256()
    for name in sorted(set(paths) - {b''}):
        source.update(name + b'\0')
        path = ROOT / os.fsdecode(name)
        source.update(hashlib.sha256(path.read_bytes()).digest() if path.is_file() else b'DELETED')
    executable = build / 'luma_ux_tests'
    return {'chip': command('sysctl', '-n', 'machdep.cpu.brand_string'),
            'model': command('sysctl', '-n', 'hw.model'), 'ram_bytes': command('sysctl', '-n', 'hw.memsize'),
            'macos': platform.mac_ver()[0], 'qt': '6.10.3',
            'commit': command('git', '-C', str(ROOT), 'rev-parse', 'HEAD'),
            'worktree_dirty': bool(command('git', '-C', str(ROOT), 'status', '--porcelain')),
            'source_tree_sha256': source.hexdigest(),
            'test_executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest() if executable.is_file() else None,
            'source_snapshot_point': 'suite_end',
            'build_directory': str(build), 'hosted_ci': bool(os.environ.get('GITHUB_ACTIONS')),
            'performance_mode': 'MEASURE_ONLY'}


def build_provenance(build, snapshot):
    path = build / 'ux-build-provenance.json'
    try:
        recorded = json.loads(path.read_text())
        valid = (recorded.get('schema') == 1 and bool(snapshot.get('test_executable_sha256'))
                 and all(recorded.get(field) == snapshot.get(field)
                         for field in ('source_tree_sha256', 'test_executable_sha256')))
        return {'status': 'PASS' if valid else 'UNVERIFIED', 'path': str(path),
                'source_commit': recorded.get('source_commit'),
                'reason': '' if valid else 'Build record does not match current sources and executable'}
    except (OSError, ValueError, TypeError, AttributeError) as error:
        return {'status': 'UNVERIFIED', 'path': str(path), 'reason': str(error)}


def execute(row, directory, arguments, fixture_folder):
    directory.mkdir(parents=True)
    environment = os.environ.copy()
    if not os.environ.get('GITHUB_ACTIONS'):
        environment.pop('QT_QUICK_BACKEND', None)
    else:
        environment['QT_QUICK_BACKEND'] = 'software'
    if os.environ.get('GITHUB_ACTIONS'):
        environment['QT_QPA_PLATFORM'] = 'offscreen'
    command = [str(arguments.build / 'luma_ux_tests'), '--case', row['id'], '--root', str(directory),
               '--fixtures', str(fixture_folder), '--qml', str(ROOT / 'apps/desktop/qml')]
    state_root = getattr(arguments, 'state_root', directory)
    if getattr(arguments, 'stage', None):
        command.extend(['--stage', arguments.stage, '--state-root', str(state_root)])
    if getattr(arguments, 'variant', None):
        command.extend(['--variant', arguments.variant])
    atomic_json(directory / 'command.json', command)
    deadline_seconds = scenario_timeout(row['id'], arguments)
    started = time.monotonic()
    last_beat, observed = started, None
    reason = None
    diagnostic = None
    intentional_kill = False
    with (directory / 'app.log').open('w') as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=environment, start_new_session=True)
        try:
            while process.poll() is None:
                ready = directory / 'kill-ready.json'
                if row['id'] in KILL and getattr(arguments, 'stage', None) == 'prepare' and ready.exists():
                    marker = json.loads(ready.read_text())
                    if marker.get('id') != row['id'] or marker.get('stage') != 'prepare' or marker.get('pid') != process.pid:
                        reason = 'Invalid intentional-kill stage marker'
                    elif row['id'] == 'UX-UND-015' and time.time() * 1000 - marker['utc_ms'] > 100:
                        reason = 'Pending-debounce kill missed its 100 ms fixture window'
                    else:
                        intentional_kill = True
                    break
                heartbeat = directory / 'heartbeat.json'
                if heartbeat.exists():
                    current = heartbeat.stat().st_mtime_ns
                    if current != observed:
                        observed, last_beat = current, time.monotonic()
                if time.monotonic() - last_beat > 2 and diagnostic is None and Path('/usr/bin/sample').exists():
                    diagnostic = subprocess.Popen(['/usr/bin/sample', str(process.pid), '1', '10',
                                                   '-file', str(directory / 'thread-sample.txt')],
                                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if time.monotonic() - last_beat > 5:
                    reason = 'P0 hard hang: no main-loop heartbeat for >5s'
                if time.monotonic() - started > deadline_seconds:
                    reason = 'Scenario deadline exceeded; last heartbeat remained recorded'
                if reason:
                    break
                time.sleep(.05)  # Parent watchdog sampling; UI waits are condition-based.
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            if diagnostic is not None:
                try:
                    diagnostic.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    diagnostic.terminate()
                    diagnostic.wait(timeout=2)
    result_path = directory / 'result.json'
    result = json.loads(result_path.read_text()) if result_path.exists() else {'id': row['id'], 'status': 'FAIL'}
    if intentional_kill and process.returncode == -signal.SIGKILL:
        result = {**json.loads((directory / 'metrics.json').read_text()), 'id': row['id'],
                  'status': 'PASS', 'message': 'Intentional SIGKILL at the verified persistence checkpoint',
                  'intentional_kill': True}
    if reason or process.returncode != 0 and not intentional_kill:
        result.update(status='FAIL', message=reason or result.get('message', f'Child exited {process.returncode}'))
    catalog = state_root / 'catalog.sqlite'
    if catalog.exists():
        try:
            with closing(sqlite3.connect(f'file:{catalog}?mode=ro', uri=True)) as database:
                assert database.execute('PRAGMA integrity_check').fetchone() == ('ok',)
                assert not database.execute('PRAGMA foreign_key_check').fetchall()
            result['catalog_integrity'] = 'PASS'
        except (sqlite3.Error, AssertionError) as error:
            result.update(status='FAIL', message=f'P0 catalog integrity: {error}')
    if result['status'] == 'FAIL' and catalog.exists():
        try:
            with closing(sqlite3.connect(f'file:{catalog}?mode=ro', uri=True)) as source:
                with closing(sqlite3.connect(directory / 'catalog-copy.sqlite')) as target:
                    source.backup(target)
            result['catalog_snapshot'] = 'catalog-copy.sqlite'
        except sqlite3.Error as error:
            result['catalog_snapshot_error'] = str(error)
    result.update(scope=row['scope'], mode=row['mode'], severity_if_failed=row['severity'],
                  scenario=row['scenario'], artifacts=str(directory), child_pid=process.pid,
                  wall_duration_ms=round((time.monotonic() - started) * 1000))
    atomic_json(result_path, result)
    return result


def execute_scenario(row, directory, arguments, fixture_folder):
    operations = row['id'] == 'UX-UPD-003'
    if row['id'] not in RESTART and not operations:
        return execute(row, directory, arguments, fixture_folder)
    directory.mkdir(parents=True)
    cycles = 50 if row['id'] == 'UX-APP-001' else 20 if row['id'] == 'UX-APP-005' else 0
    if operations:
        stages = ['import', 'export', 'batch']
    elif cycles:
        stages = ['cycle'] * cycles
    else:
        stages = ['prepare', 'verify']
    outcomes = []
    for index, stage in enumerate(stages):
        options = dict(vars(arguments))
        options.update({'variant': stage} if operations else {'stage': stage, 'state_root': directory / 'state'})
        options = argparse.Namespace(**options)
        outcome = execute(row, directory / f'{index + 1:02}-{stage}', options, fixture_folder)
        outcomes.append(outcome)
        if outcome['status'] != 'PASS':
            break
    result = dict(outcomes[-1])
    result.update(artifacts=str(directory), stages=outcomes, metric_scope='final_stage',
                  wall_duration_ms=sum(r['wall_duration_ms'] for r in outcomes),
                  main_loop_stall_max_ms=max(r.get('main_loop_stall_max_ms', 0) for r in outcomes),
                  rss_peak_mb=max(r.get('rss_peak_mb', 0) for r in outcomes))
    atomic_json(directory / 'result.json', result)
    return result


def repetitions_for(row, arguments):
    required = 3 if arguments.release_qualification and row['mode'] == 'BLOCKING' else 1
    return max(arguments.runs, required)


def phase_selected(row, phase):
    if phase is None or row['scope'] in ('ALL', phase):
        return True
    return row['mode'] == 'BLOCKING' and in_scope(row['scope'], phase)


def source_phase():
    match = re.search(r'project\(Luma VERSION (\d+)\.(\d+)\.\d+ LANGUAGES',
                      (ROOT / 'CMakeLists.txt').read_text())
    if not match:
        raise ValueError('Cannot determine current Luma source phase')
    return '.'.join(match.groups())


def qualification_requested(arguments):
    return (arguments.release_qualification and not arguments.case
            and arguments.suite == 'all' and arguments.phase is None)


def qualify(results, inventory, through, requested):
    required = {r['id'] for r in inventory if r['mode'] == 'BLOCKING'
                and in_scope(r['scope'], through)}
    missing = []
    for identifier in required:
        attempts = [r for r in results if r['id'] == identifier]
        if len(attempts) < 3 or any(r['status'] != 'PASS' for r in attempts):
            missing.append(identifier)
    if any(r['status'] in ('FAIL', 'FLAKY') and
           r.get('severity_if_failed', r.get('severity', 'P0')) in ('P0', 'P1') for r in results):
        return 'FAIL', sorted(missing)
    if not requested:
        return 'NOT_QUALIFIED', sorted(missing)
    observed = {row['id'] for row in results}
    if any(row['id'] not in observed for row in inventory if in_scope(row['scope'], through)):
        return 'FAIL', sorted(missing)
    if any(r['status'] == 'SKIPPED' and r.get('skip_reason', 'unadapted') == 'unadapted'
           and in_scope(r.get('scope', 'CURRENT'), through) for r in results):
        return 'FAIL', sorted(missing)
    return ('FAIL' if missing else 'PASS'), sorted(missing)


THEMES = {
    'APP': 'Lifecycle and layout', 'NAV': 'Navigation', 'LIB': 'Library browsing',
    'SEL': 'Selection and metadata', 'FLT': 'Search and collections', 'IMP': 'Import',
    'DSC': 'Develop scrolling', 'SLD': 'Editing controls', 'PRV': 'Async previews',
    'CRP': 'Crop', 'TRN': 'Transform', 'LNS': 'Lens corrections', 'DTL': 'Detail',
    'ADV': 'Curves and HSL', 'PRS': 'Presets', 'BAT': 'Batch edits', 'EXP': 'Export',
    'UND': 'History and persistence', 'REC': 'Recovery and storage faults',
    'KEY': 'Keyboard and accessibility', 'MSK': 'Local masks', 'LOC': 'Localization',
}


def theme(row):
    parts = row['id'].split('-')
    prefix = parts[1] if len(parts) > 1 else row['id']
    return THEMES.get(prefix, prefix)


def write_reports(summary, destination):
    atomic_json(destination / 'UX_REPORT.json', summary)
    hardware = summary['hardware']
    lines = ['# UX qualification', '',
             f"Execution: {summary['execution_status']}. Release gate: {summary['release_gate']}.", '',
             f"Results: {json.dumps(summary['summary'])}. Original hashes: {summary['original_hashes']}.", '',
             f"Source commit: {hardware['commit']}; dirty worktree: {hardware['worktree_dirty']}.",
             f"Source tree SHA-256: {hardware['source_tree_sha256']}.",
             f"Test executable SHA-256: {hardware['test_executable_sha256']}.",
             f"Build provenance: {summary.get('build_provenance', {}).get('status', 'UNVERIFIED')}. "
             f"Source consistency: {summary.get('source_consistency', 'UNVERIFIED')}.", '',
             'Performance is MEASURE_ONLY. Input dispatch is not visual acknowledgment.',
             'Frame intervals include idle periods; they are not an active-interaction FPS baseline.', '',
             f"Unqualified blocking IDs: {len(summary['blocking_missing_or_failed'])}.", '']
    grouped = {}
    for row in summary['results']:
        grouped.setdefault(theme(row), []).append(row)
    roadmap = ['# Roadmap evidence by product area', '',
               'Unexecuted adapters are qualification work, not evidence that a product feature is absent.',
               'Passing repeats do not remove evidence from earlier failures.', '']
    for name, rows in sorted(grouped.items()):
        lines += [f'## {name}', '', str(dict(Counter(r['status'] for r in rows))), '']
        unresolved = {}
        for row in rows:
            message = row.get('message') or '; '.join(row.get('notes', []))
            evidence = f" [evidence](<{row['artifacts']}/result.json>)" if row.get('artifacts') else ''
            lines.append(f"- {row['id']}: {row['status']} — {message}{evidence}")
            if row['status'] not in ('PASS', 'MEASURED'):
                unresolved[row['id']] = row
        lines.append('')
        if unresolved:
            roadmap += [f'## {name}', '']
            for status, label in [('FAIL', 'Reproduced failures'), ('FLAKY', 'Unresolved intermittent failures'),
                                  ('NOT_IMPLEMENTED', 'Missing or conditional capabilities'),
                                  ('SKIPPED', 'Qualification still required')]:
                selected = [r for r in unresolved.values() if r['status'] == status]
                if selected:
                    roadmap += [f'{label}: {len(selected)} checks.', '']
                    for row in selected:
                        reason = row.get('message') or '; '.join(row.get('notes', []))
                        roadmap.append(f"- {row['id']} ({row.get('scope', 'ALL')}): {row.get('scenario', '')} — {reason}")
                    roadmap.append('')
    (destination / 'UX_REPORT.md').write_text('\n'.join(lines))
    (destination / 'ROADMAP_CANDIDATES.md').write_text('\n'.join(roadmap))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build-verify')
    parser.add_argument('--app', type=Path)
    parser.add_argument('--suite', choices=['all', 'blocking', 'regression', 'stress', 'perf', 'chaos'], default='all')
    parser.add_argument('--through', default=source_phase())
    parser.add_argument('--phase')
    parser.add_argument('--case', action='append', default=[])
    parser.add_argument('--runs', type=int, default=1)
    parser.add_argument('--actions', type=int, default=10000)
    parser.add_argument('--seed', type=int, default=123456)
    parser.add_argument('--replay', type=Path)
    parser.add_argument('--release-qualification', action='store_true')
    parser.add_argument('--require-build-provenance', action='store_true',
                        help='Require a matching attested build before execution; implied by --release-qualification')
    parser.add_argument('--artifacts', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--timeout', type=int, help='Override the per-scenario deadline in seconds')
    args = parser.parse_args()
    args.require_build_provenance = args.require_build_provenance or args.release_qualification
    if args.phase and not re.fullmatch(r'\d+\.\d+', args.phase):
        parser.error('--phase must be a numeric major.minor phase')
    if not re.fullmatch(r'\d+\.\d+', args.through):
        parser.error('--through must be a numeric major.minor phase')
    if args.release_qualification and tuple(map(int, args.through.split('.'))) < tuple(map(int, source_phase().split('.'))):
        parser.error('Release qualification cannot exclude the current source phase')
    if args.app or args.replay:
        parser.error('--app and --replay are not executable adapters yet; refusing an ignored request')
    if args.suite == 'chaos':
        parser.error('Seeded chaos/replay adapter is not implemented yet')
    unknown = set(args.case) - {row['id'] for row in INVENTORY}
    if unknown:
        parser.error('Unknown scenario IDs: ' + ', '.join(sorted(unknown)))
    if args.runs < 1 or (args.timeout is not None and args.timeout < 1):
        parser.error('--runs and --timeout must be positive')
    # Native suites share one macOS focus/keyboard surface, including across worktrees.
    with (Path(tempfile.gettempdir()) / f'luma-ux-screen-{os.getuid()}.lock').open('a') as screen_lock:
        print('Waiting for exclusive UI test access...', flush=True)
        fcntl.flock(screen_lock.fileno(), fcntl.LOCK_EX)
        args.build = args.build.resolve()
        args.artifacts = (args.artifacts or args.build / 'ux-artifacts').resolve()
        args.report = (args.report or args.build / 'ux-report').resolve()
        args.report.mkdir(parents=True, exist_ok=True)
        if args.require_build_provenance:
            preflight = build_provenance(args.build, fingerprint(args.build))
            if preflight['status'] != 'PASS':
                parser.error(f"Build provenance preflight failed ({preflight['path']}): {preflight['reason']}. "
                             f'Run tests/ux/build_ux.py --build {args.build}')
        run_root = args.artifacts.resolve() / (time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()) + f'-{os.getpid()}')
        run_root.mkdir(parents=True)
        fixture_folder = args.artifacts.resolve() / 'public-fixtures'
        checksums = fixtures(fixture_folder)
        suite_start = fingerprint(args.build)
        provenance = build_provenance(args.build, suite_start)
        if args.require_build_provenance and provenance['status'] != 'PASS':
            parser.error(f"Build provenance changed during fixture setup ({provenance['path']}): {provenance['reason']}. "
                         f'Run tests/ux/build_ux.py --build {args.build}')
        results = []
        global_rows = []
        observed_mutations = set()
        for row in INVENTORY:
            identifier, scope, mode = row['id'], row['scope'], row['mode']
            repetitions = repetitions_for(row, args)
            if args.case and identifier not in args.case:
                continue
            applicable = in_scope(scope, args.through)
            capable = scope in ('ALL', 'CURRENT') or scope in MANIFEST['completed_phases']
            if not phase_selected(row, args.phase):
                continue
            mode_selected = args.suite == 'all' or mode == {'blocking': 'BLOCKING', 'regression': 'REGRESSION',
                                 'stress': 'STRESS', 'perf': 'PERF_LOCAL', 'chaos': 'CHAOS'}.get(args.suite)
            if not mode_selected:
                continue
            if not applicable or not capable or identifier == 'UX-MSK-005':
                results.append({**row, 'status': 'NOT_IMPLEMENTED', 'notes': ['Capability absent; future phase/conditional tool']})
                continue
            if identifier in GLOBAL:
                global_rows.append(row)
                continue
            if identifier not in NATIVE:
                results.append({**row, 'status': 'SKIPPED', 'skip_reason': 'unadapted',
                                'notes': ['No verified executable adapter yet; current scope qualification remains incomplete']})
                continue
            for repeat in range(repetitions):
                result = execute_scenario(row, run_root / identifier / f'repeat-{repeat + 1}', args, fixture_folder)
                mutations = changed_originals(checksums)
                observed_mutations.update(mutations)
                result['original_integrity'] = 'FAIL' if mutations else 'PASS'
                if mutations:
                    result.update(status='FAIL', message='P0 original SHA changed', mutated_originals=mutations)
                atomic_json(Path(result['artifacts']) / 'result.json', result)
                results.append(result)
                print(f"{identifier} repeat {repeat + 1}: {result['status']} {result.get('message', '')}", flush=True)
        observed_mutations.update(changed_originals(checksums))
        mutations = sorted(observed_mutations)
        for row in global_rows:
            repetitions = repetitions_for(row, args)
            native_executed = any(result.get('child_pid') for result in results)
            for repeat in range(repetitions):
                results.append({**row, 'status': 'FAIL' if mutations else 'PASS' if native_executed else 'SKIPPED',
                                'notes': ['Full original SHA-256 set checked before the suite, after every native scenario and at suite end'
                                          if native_executed else 'Integrity-only selection did not exercise an application workflow'],
                                'repeat': repeat + 1, 'original_count': len(checksums)})
        if mutations:
            results.append({'id': 'ORIGINAL-INTEGRITY', 'status': 'FAIL', 'severity': 'P0', 'paths': mutations})
        suite_end = fingerprint(args.build)
        source_stable = all(suite_start[field] == suite_end[field] for field in ('source_tree_sha256', 'test_executable_sha256'))
        summary = {'hardware': suite_end, 'build_provenance': provenance,
                   'source_consistency': 'PASS' if source_stable else 'CHANGED_DURING_SUITE', 'scope_through': args.through, 'run_directory': str(run_root),
                   'summary': dict(Counter(r['status'] for r in results)), 'original_hashes': 'FAIL' if mutations else 'PASS',
                   'results': results}
        selected_failed = bool(mutations) or any(r['status'] == 'FAIL' for r in results)
        summary['release_gate'], blocking_missing = qualify(results, INVENTORY, args.through,
                                                           qualification_requested(args))
        incomplete = any(r['status'] == 'SKIPPED' and r.get('skip_reason', 'unadapted') == 'unadapted' for r in results) or not any(r.get('child_pid') for r in results)
        summary['unadapted_applicable'] = sorted({r['id'] for r in results if r['status'] == 'SKIPPED'
                                                and r.get('skip_reason', 'unadapted') == 'unadapted'
                                                and in_scope(r.get('scope', 'CURRENT'), args.through)})
        if not source_stable or args.require_build_provenance and provenance['status'] != 'PASS':
            incomplete = True
            summary['release_gate'] = 'FAIL' if args.release_qualification else 'NOT_QUALIFIED'
        summary['execution_status'] = 'FAIL' if selected_failed else 'INCOMPLETE' if incomplete else 'PASS'
        summary['blocking_missing_or_failed'] = sorted(set(blocking_missing))
        write_reports(summary, args.report)
        print(json.dumps({'summary': summary['summary'], 'release_gate': summary['release_gate'], 'report': str(args.report)}, ensure_ascii=False))
        return 0 if not selected_failed and not incomplete and (not args.release_qualification or summary['release_gate'] == 'PASS') else 1


if __name__ == '__main__':
    raise SystemExit(main())
