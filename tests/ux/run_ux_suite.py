#!/usr/bin/env python3
"""Capability-aware UX qualification; unexecuted checks never count as passes."""
from __future__ import annotations
import argparse
import fcntl
import tempfile
from collections import Counter
from contextlib import closing
import hashlib
from history_chaos_cli import configure_history_chaos
from catalog_invariants import audit_restored_workflow
from migration_fixtures import prepare_migration, audit_migration
from native_bundle_provenance import native_bundle_info, launch_executable
from quality_policy import quality_skip_reason, quality_fixture_count
from thumbnail_response_fixtures import CASE_PROFILE_IDS as THUMBNAIL_PROFILE_IDS, official_fixture as thumbnail_fixture
from crop_done_fixtures import OFFICIAL_CASE as CROP_DONE_CASE, official_fixture as crop_done_fixture
from race007_fixtures import OFFICIAL_CASE as PARTIAL_RENDER_CASE, official_fixture as partial_render_fixture
from export_watermark_fixtures import (OFFICIAL_CASE as WATERMARK_CASE,
                                       official_fixture as watermark_fixture,
                                       child_environment as watermark_child_environment)
from export_current_filename_fixtures import (OFFICIAL_CASE as CURRENT_FILENAME_CASE,
                                              official_fixture as current_filename_fixture)
from metadata_display_fixtures import (OFFICIAL_CASE as METADATA_DISPLAY_CASE,
                                       official_fixture as metadata_display_fixture)
from ai_workflow_fixtures import CASE_PROFILE_IDS as AI_PROFILE_IDS, official_fixture as ai_fixture
from catalog_scale_fixtures import CASE_PROFILE_IDS as CATALOG_SCALE_IDS, official_fixture as catalog_scale_fixture
from merge_fixtures import CASE_PROFILE_IDS as MERGE_PROFILE_IDS, official_fixture as merge_fixture
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

CASE_PROFILE_IDS = AI_PROFILE_IDS | CATALOG_SCALE_IDS | MERGE_PROFILE_IDS | THUMBNAIL_PROFILE_IDS | {WATERMARK_CASE, CROP_DONE_CASE, CURRENT_FILENAME_CASE, PARTIAL_RENDER_CASE, METADATA_DISPLAY_CASE}

# These adapters have explicit actual-native render/measurement contracts. NATIVE below means
# implemented adapter, not compatibility with every requested Qt backend.
NATIVE_ONLY_CONTRACTS = {
    'UX-MAC-001': {'platform': 'cocoa', 'graphics_api': 'metal', 'device_pixel_ratio': 2},
    CROP_DONE_CASE: {'platform': 'cocoa', 'graphics_api': 'metal'},
    'UX-P10-A11Y-002': {'platform': 'cocoa'},
    'UX-NAV-002': {'platform': 'cocoa'},
    'UX-PRV-002': {'platform': 'cocoa'},
    'UX-ADV-012': {'platform': 'cocoa'},
    'UX-PERF-006': {'platform': 'cocoa'},
}


def unsupported_backend_requirement(identifier):
    # Match execute()'s existing controlled environment route exactly. No probe,
    # hardware model inference or environment override: no child is started here.
    if identifier in NATIVE_ONLY_CONTRACTS and os.environ.get('GITHUB_ACTIONS'):
        return {
            'required': dict(NATIVE_ONLY_CONTRACTS[identifier]),
            'classification_basis': 'runner_controlled_child_environment',
            'requested_platform': 'offscreen', 'requested_graphics_api': 'software',
            'actual_runtime_observed': False, 'child_launch_suppressed': True,
        }
    return None

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
NATIVE.update(('UX-PRV-012', 'UX-P06-ZOM-003', 'UX-P06-ZOM-004', 'UX-P06-ZOM-012'))
NATIVE.update(f'UX-P06-ZOM-{i:03}' for i in (6,7,8,9))
NATIVE.add('UX-PRV-013')
NATIVE.add('UX-PRV-006')
NATIVE.update(('UX-P06-GPU-001', 'UX-P06-GPU-002'))
NATIVE.update(('UX-P06-MEM-001', 'UX-P06-MEM-002'))
NATIVE.update(('UX-P06-LRU-001', 'UX-P06-LRU-002'))
NATIVE.add('UX-P09-BKP-001')
NATIVE.add('UX-P06-PRG-001')
NATIVE.update(MERGE_PROFILE_IDS)
NATIVE.update(AI_PROFILE_IDS)
NATIVE.update(f'UX-P07-REM-{i:03}' for i in (1, 2, 3))
NATIVE.update(f'UX-P09-RLK-{i:03}' for i in (1, 2, 3, 4, 6, 7))
NATIVE.update(('UX-P09-BKP-002', 'UX-P09-BKP-006', 'UX-P09-BKP-007'))
NATIVE.update(('UX-P09-META-001', 'UX-P09-META-006', 'UX-P09-META-002', 'UX-P09-META-007', 'UX-P09-XMP-002',
               'UX-P09-XMP-003', 'UX-P09-XMP-004', 'UX-P09-XMP-005'))
NATIVE.update(('UX-P06-ZOM-001', 'UX-P06-ZOM-002', 'UX-P06-ZOM-005',
               'UX-P06-PRG-002', 'UX-P06-PRG-004', 'UX-P06-PRG-007', 'UX-P06-PRG-008', 'UX-P06-PRG-009'))
NATIVE.update(f'UX-IMP-{i:03}' for i in (1, 2, 3, 4, 6, 7, 8, 9, 13, 14, 15, 16))
NATIVE.add('UX-APP-008')
NATIVE.update(f'UX-REC-{i:03}' for i in (1, 2, 3, 4, 5, 6, 12))
NATIVE.update(f'UX-EXP-{i:03}' for i in (1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 13, 15, 18, 29, 30))
NATIVE.add('UX-APP-009')
RESTART = {'UX-APP-001', 'UX-APP-003', 'UX-APP-005', 'UX-APP-007',
           'UX-MSK-008', 'UX-MSK-040', 'UX-UND-013', 'UX-UND-014', 'UX-UND-015', 'UX-REC-007',
           'UX-DTL-012', 'UX-SLD-020', 'UX-CRP-018', 'UX-LNS-010', 'UX-TRN-015', 'UX-IMP-020', 'UX-LOC-006', 'UX-LOC-007'}
KILL = {'UX-P09-XMP-012', 'UX-P09-FLT-002', 'UX-UND-014', 'UX-UND-015', 'UX-REC-007', 'UX-IMP-020'}
RESTART.update(('UX-PRS-011', 'UX-PRS-018', 'UX-REC-009', 'UX-APP-004', 'UX-SLD-016'))
RESTART.add('UX-EXP-025')
RESTART.update(('UX-P08-PAN-012', 'UX-P08-FLT-001'))
RESTART.add('UX-P07-DNS-008')
RESTART.update(CATALOG_SCALE_IDS)
RESTART.update(('UX-P09-XMP-012', 'UX-P09-FLT-002'))
CRASH_IDS = {f'UX-P09-FLT-{i:03}' for i in (1, 3, 4, 5, 6)}
RESTART.update(CRASH_IDS)
KILL.update(CRASH_IDS)
RESTART.add('UX-P10-DAT-004')
RESTART.update(('UX-P10-DAY-007', 'UX-P10-DAY-010', 'UX-P10-DAY-013', 'UX-P10-DAT-005'))
KILL.add('UX-P10-DAY-007')
NATIVE.add('UX-P09-XMP-001')
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
NATIVE.update(('UX-BAT-004', 'UX-BAT-007', 'UX-BAT-014', 'UX-UPD-001',
               'UX-UPD-005', 'UX-UPD-006', 'UX-APP-011', 'UX-RACE-010'))
NATIVE.update(('UX-EXP-017', 'UX-EXP-027'))
NATIVE.update(('UX-EXP-023', 'UX-EXP-024'))
NATIVE.update(('UX-PERF-017', 'UX-PERF-018', 'UX-DTL-011'))
NATIVE.update(('UX-LIB-006', 'UX-LIB-018', 'UX-MAC-001'))
NATIVE.add('UX-UND-017')
NATIVE.add('UX-EXP-026')
NATIVE.add('UX-MAC-007')
NATIVE.add('UX-MAC-008')
NATIVE.add('UX-RACE-005')
NATIVE.add('UX-LIB-004')
NATIVE.add('UX-LIB-005')
NATIVE.add('UX-RACE-002')
NATIVE.add(WATERMARK_CASE)
NATIVE.add(CROP_DONE_CASE)
NATIVE.add(CURRENT_FILENAME_CASE)
NATIVE.add(PARTIAL_RENDER_CASE)
NATIVE.add('UX-P09-PERF-001')  # Executable Python schema adapter; no native UI claim.
NATIVE.add('UX-P10-A11Y-002')
NATIVE.add('UX-P10-DAY-009')
NATIVE.add('UX-P10-DAY-003')
GLOBAL = {'UX-UND-018'}

CASE_TIMEOUTS = {'UX-MAC-008': 300, 'UX-MAC-007': 300, 'UX-EXP-026': 300, 'UX-UND-017': 3600, 'UX-LIB-006': 300, 'UX-LIB-018': 300, 'UX-MAC-001': 300,
                 'UX-PERF-017': 600, 'UX-PERF-018': 600, 'UX-DTL-011': 900,
                 'UX-EXP-023': 900, 'UX-EXP-024': 600,
                 'UX-EXP-017': 300, 'UX-EXP-025': 300, 'UX-EXP-027': 300,
                 'UX-BAT-014': 900, 'UX-UND-016': 600, 'UX-ADV-013': 600, 'UX-PRS-019': 900,
                 'UX-IMP-019': 600, 'UX-NAV-002': 300, 'UX-PRV-002': 300, 'UX-ADV-012': 600,
                 'UX-APP-012': 600, 'UX-APP-015': 600, 'UX-EXP-021': 600,
                 'UX-LIB-017': 600, 'UX-LIB-008': 300, 'UX-LIB-014': 300,
                 'UX-LIB-015': 300, 'UX-LIB-016': 300, 'UX-RACE-009': 300,
                 'UX-VIS-004': 600, 'UX-EXP-011': 600, 'UX-VIS-005': 600, 'UX-VIS-009': 300,
                 'UX-VIS-011': 300, 'UX-VIS-012': 300, 'UX-REC-011': 300}


ISOLATED_VARIANTS = {
    'UX-P10-UPG-012': ('save', 'import', 'export', 'batch', 'merge', 'merge-cancel'),
    'UX-P10-DAY-011': ('hdr', 'panorama'),
    'UX-UPD-003': ('import', 'export', 'batch'),
    'UX-MAC-008': ('copy', 'export'),
}


def scenario_timeout(identifier, arguments):
    explicit = getattr(arguments, 'timeout', None)
    return explicit if explicit is not None else CASE_TIMEOUTS.get(identifier, 180)




def atomic_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + '.partial')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
    temporary.replace(path)


def changed_originals(checksums):
    changed = []
    for name, digest in checksums.items():
        path = Path(name)
        try:
            if (not path.is_file() or path.is_symlink() or path.resolve() != path
                    or hashlib.sha256(path.read_bytes()).hexdigest() != digest):
                changed.append(name)
        except OSError:
            changed.append(name)
    return changed


def merge_fixture_hashes(profiles):
    """Validate owned original files before combining disjoint profile hashes."""
    merged = {}
    for folder, checksums in profiles:
        folder = Path(folder)
        if not folder.is_absolute() or folder.resolve() != folder or folder.is_symlink():
            raise ValueError('Fixture directory must be canonical and without symlinks')
        for name, digest in checksums.items():
            path = Path(name)
            if (not path.is_absolute() or path == folder or not path.is_relative_to(folder)
                    or path.resolve() != path or path.is_symlink() or not path.is_file()):
                raise ValueError('Original must be a regular file inside its owned fixture directory: ' + str(path))
            canonical = str(path)
            if canonical in merged:
                raise ValueError('Duplicate original path across fixture profiles: ' + canonical)
            if (not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest)
                    or hashlib.sha256(path.read_bytes()).hexdigest() != digest):
                raise ValueError('Fixture checksum does not match its original: ' + canonical)
            merged[canonical] = digest
    return merged


def implemented_scope(scope, identifier=None):
    # A named, source-implemented TARGET can enter qualification without
    # changing the original specification or admitting other future targets.
    if scope == 'TARGET':
        return MANIFEST.get('implemented_targets', {}).get(identifier, scope)
    return scope


def capability_implemented(scope, identifier):
    return (implemented_scope(scope, identifier) in ('ALL', 'CURRENT', *MANIFEST['completed_phases'])
            or identifier in MANIFEST.get('implemented_scenarios', ()))


def in_scope(scope, through, identifier=None):
    scope = implemented_scope(scope, identifier)
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


def fixtures(folder, count=1000, include_large=True):
    folder.mkdir(parents=True, exist_ok=True)
    for index in range(count):
        path = folder / f'Photo-{index:04}.png'
        if not path.exists():
            png(path, index)
    stress = folder / 'stress'
    stress.mkdir(exist_ok=True)
    if include_large:
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
            'native_bundle_info': native_bundle_info(build),
            'source_snapshot_point': 'suite_end',
            'build_directory': str(build), 'hosted_ci': bool(os.environ.get('GITHUB_ACTIONS')),
            'performance_mode': 'MEASURE_ONLY'}


def build_provenance(build, snapshot):
    path = build / 'ux-build-provenance.json'
    try:
        recorded = json.loads(path.read_text())
        bundle = native_bundle_info(build)
        bundle_valid = (recorded.get('native_bundle_info') == snapshot.get('native_bundle_info') == bundle
                        and (bundle is None or (bundle.get('status') == 'PASS'
                             and bundle.get('executable_sha256') == snapshot.get('test_executable_sha256'))))
        valid = (bundle_valid and recorded.get('schema') == 1 and bool(snapshot.get('test_executable_sha256'))
                 and all(recorded.get(field) == snapshot.get(field)
                         for field in ('source_tree_sha256', 'test_executable_sha256')))
        return {'status': 'PASS' if valid else 'UNVERIFIED', 'path': str(path),
                'source_commit': recorded.get('source_commit'),
                'reason': '' if valid else 'Build record does not match current sources and executable'}
    except (OSError, ValueError, TypeError, AttributeError) as error:
        return {'status': 'UNVERIFIED', 'path': str(path), 'reason': str(error)}


def offline_child_command(identifier, command):
    if identifier != 'UX-P10-DAY-012':
        return list(command)
    return ['/usr/bin/sandbox-exec', '-p', '(version 1) (allow default) (deny network*)', *command]


def execute(row, directory, arguments, fixture_folder):
    directory.mkdir(parents=True)
    environment = os.environ.copy()
    if not os.environ.get('GITHUB_ACTIONS'):
        environment.pop('QT_QUICK_BACKEND', None)
    else:
        environment['QT_QUICK_BACKEND'] = 'software'
    if os.environ.get('GITHUB_ACTIONS'):
        environment['QT_QPA_PLATFORM'] = 'offscreen'
    if row['id'] == WATERMARK_CASE:
        environment = watermark_child_environment(environment, directory, fixture_folder, case_id=row['id'])
    command = [str(launch_executable(arguments.build)), '--case', row['id'], '--root', str(directory),
               '--fixtures', str(fixture_folder), '--qml', str(ROOT / 'apps/desktop/qml')]
    state_root = getattr(arguments, 'state_root', directory)
    if row['id'] == 'UX-P10-DAT-004' and getattr(arguments, 'stage', None) == 'prepare':
        prepare_migration(state_root)
    if getattr(arguments, 'stage', None):
        command.extend(['--stage', arguments.stage, '--state-root', str(state_root)])
    if getattr(arguments, 'variant', None):
        command.extend(['--variant', arguments.variant])
    if row['id'] == 'UX-UND-017':
        command.extend(['--seed', str(arguments.seed), '--actions', str(arguments.actions)])
        if arguments.replay:
            command.extend(['--replay', str(arguments.replay)])
    command = offline_child_command(row['id'], command)
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
                if row['id'] in KILL and getattr(arguments, 'stage', None) in ('prepare', 'prepare-committed') and ready.exists():
                    marker = json.loads(ready.read_text())
                    if marker.get('id') != row['id'] or marker.get('stage') != arguments.stage or marker.get('pid') != process.pid:
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
                    with (directory / 'thread-sample.log').open('w') as sample_log:
                        diagnostic = subprocess.Popen(['/usr/bin/sample', str(process.pid), '1', '10',
                                                       '-file', str(directory / 'thread-sample.txt')],
                                                      stdout=sample_log, stderr=subprocess.STDOUT)
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
                atomic_json(directory / 'thread-sample-status.json',
                            {'returncode': diagnostic.returncode,
                             'sample_available': (directory / 'thread-sample.txt').exists()})
    result_path = directory / 'result.json'
    child_result = json.loads(result_path.read_text()) if result_path.exists() else None
    if child_result is not None:
        # Keep the child's evidence before the supervisor overrides an apparent
        # PASS when teardown crashes or the watchdog terminates the process.
        atomic_json(directory / 'child-result.json', child_result)
    result = dict(child_result) if child_result is not None else {'id': row['id'], 'status': 'FAIL'}
    verified_kill = intentional_kill and process.returncode == -signal.SIGKILL
    if intentional_kill and not verified_kill:
        reason = reason or ('Intentional-kill checkpoint was not terminated by SIGKILL '
                            f'(child exit {process.returncode})')
    if verified_kill:
        result = {**json.loads((directory / 'metrics.json').read_text()), 'id': row['id'],
                  'status': 'PASS', 'message': 'Intentional SIGKILL at the verified persistence checkpoint',
                  'intentional_kill': True}
    result.update(child_returncode=process.returncode,
                  child_reported_status=child_result.get('status') if child_result else None,
                  intentional_kill_requested=intentional_kill)
    if reason or process.returncode != 0 and not verified_kill:
        exit_message = f'Child exited {process.returncode}'
        if process.returncode < 0:
            try:
                exit_message += f' ({signal.Signals(-process.returncode).name})'
            except ValueError:
                pass  # Retain the numeric code if the platform has no signal name.
        result.update(status='FAIL', message=reason or result.get('message') or exit_message)
    catalog = state_root / 'catalog.sqlite'
    restored_daily = row['id'] in ('UX-P10-DAY-010', 'UX-P10-DAT-005') and getattr(arguments, 'stage', None) == 'verify'
    if restored_daily:
        catalog = state_root / 'restored' / 'catalog.sqlite'
        if not catalog.is_file() or catalog.is_symlink():
            result.update(status='FAIL', message='P0 restored catalog missing or aliased after process exit')
    result['catalog_audit_path'] = str(catalog)
    if catalog.exists():
        try:
            with closing(sqlite3.connect(f'file:{catalog}?mode=ro', uri=True)) as database:
                assert database.execute('PRAGMA integrity_check').fetchone() == ('ok',)
                assert not database.execute('PRAGMA foreign_key_check').fetchall()
            result['catalog_integrity'] = 'PASS'
        except (sqlite3.Error, AssertionError) as error:
            result.update(status='FAIL', message=f'P0 catalog integrity: {error}')
    if row['id'] == 'UX-P10-DAT-005' and restored_daily and result['status'] == 'PASS':
        try:
            audit = audit_restored_workflow(catalog, state_root, fixture_folder, directory, process.pid)
            atomic_json(directory / 'data-invariants.json', audit)
            result['data_invariants'] = audit
        except (OSError, ValueError, sqlite3.Error, KeyError, TypeError) as error:
            result.update(status='FAIL', message=f'P0 restored data invariant: {error}')
    if row['id'] == 'UX-P10-DAT-004' and result['status'] == 'PASS':
        try:
            audit, after = audit_migration(state_root, directory, process.pid, arguments.stage)
            atomic_json(directory / 'data-invariants.json', audit)
            if arguments.stage == 'prepare':
                atomic_json(state_root / 'first-migration.json', after)
            result['data_invariants'] = audit
        except (OSError, ValueError, sqlite3.Error, KeyError, TypeError) as error:
            result.update(status='FAIL', message=f'P0 migration data invariant: {error}')
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
    if getattr(arguments, 'variant', None):
        result['variant'] = arguments.variant
    atomic_json(result_path, result)
    return result


def execute_scenario(row, directory, arguments, fixture_folder):
    if row['id'] == 'UX-P09-PERF-001':
        from performance_baseline import execute_schema_case
        return execute_schema_case(row, directory)
    variants = ISOLATED_VARIANTS.get(row['id'])
    if row['id'] not in RESTART and not variants:
        return execute(row, directory, arguments, fixture_folder)
    directory.mkdir(parents=True)
    cycles = 50 if row['id'] == 'UX-APP-001' else 20 if row['id'] == 'UX-APP-005' else 0
    if variants:
        stages = variants
    elif cycles:
        stages = ['cycle'] * cycles
    elif row['id'] == 'UX-P09-FLT-001':
        stages = ['prepare', 'verify', 'prepare-committed', 'verify-committed']
    else:
        stages = ['prepare', 'verify']
    outcomes = []
    for index, stage in enumerate(stages):
        options = dict(vars(arguments))
        if variants:
            # Operation variants are independent application processes, never restart stages.
            options.pop('stage', None)
            options.pop('state_root', None)
            options['variant'] = stage
        else:
            state = 'state-committed' if stage.endswith('-committed') else 'state'
            options.update(stage=stage, state_root=directory / state)
        options = argparse.Namespace(**options)
        outcome = execute(row, directory / f'{index + 1:02}-{stage}', options, fixture_folder)
        outcomes.append(outcome)
        if outcome['status'] != 'PASS':
            break
    result = dict(outcomes[-1])
    if variants:
        result.pop('variant', None)
        result['required_variants'] = list(variants)
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
    if phase is None or implemented_scope(row['scope'], row.get('id')) in ('ALL', phase):
        return True
    return row['mode'] == 'BLOCKING' and in_scope(row['scope'], phase, row.get('id'))


def row_selected(row, arguments):
    if arguments.case and row['id'] not in arguments.case:
        return False
    if not phase_selected(row, arguments.phase):
        return False
    return (arguments.suite == 'all' or row['mode'] ==
            {'blocking': 'BLOCKING', 'regression': 'REGRESSION', 'stress': 'STRESS',
             'perf': 'PERF_LOCAL', 'chaos': 'CHAOS'}.get(arguments.suite))


def source_phase():
    match = re.search(r'project\(Luma VERSION (\d+)\.(\d+)\.\d+ LANGUAGES',
                      (ROOT / 'CMakeLists.txt').read_text())
    if not match:
        raise ValueError('Cannot determine current Luma source phase')
    return '.'.join(match.groups())


def qualification_requested(arguments):
    return (not getattr(arguments, 'quality_only', False)
            and arguments.release_qualification and not arguments.case
            and arguments.suite == 'all' and arguments.phase is None)


def qualify(results, inventory, through, requested):
    required = {r['id'] for r in inventory if r['mode'] == 'BLOCKING'
                and in_scope(r['scope'], through, r['id'])}
    missing = []
    for identifier in required:
        attempts = [r for r in results if r['id'] == identifier]
        if len(attempts) < 3 or any(r['status'] != 'PASS' for r in attempts):
            missing.append(identifier)
    if any(r['status'] in ('FAIL', 'FLAKY') and
           r.get('severity_if_failed', r.get('severity', 'P0')) in ('P0', 'P1') for r in results):
        return 'FAIL', sorted(missing)
    if any(r['status'] == 'SKIPPED' and r.get('skip_reason') == 'unsupported_backend'
           and in_scope(r.get('scope', 'CURRENT'), through, r['id']) for r in results):
        return 'NOT_QUALIFIED', sorted(missing)
    if not requested:
        return 'NOT_QUALIFIED', sorted(missing)
    observed = {row['id'] for row in results}
    if any(row['id'] not in observed for row in inventory if in_scope(row['scope'], through, row['id'])):
        return 'FAIL', sorted(missing)
    if any(r['status'] == 'SKIPPED' and r.get('skip_reason', 'unadapted') == 'unadapted'
           and in_scope(r.get('scope', 'CURRENT'), through, r['id']) for r in results):
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
    supplemental = summary.get('inventory_domain') == 'supplemental'
    title = '# Supplemental UX regressions' if supplemental else '# UX qualification'
    lines = [title, '',
             ('Supplemental results do not qualify official specification scenarios.' if supplemental
              else 'Specification inventory results.'),
             f"Inventory domain: {summary.get('inventory_domain', 'specification')}; "
             f"official inventory: {summary.get('official_inventory_count', len(INVENTORY))}; "
             f"selected inventory: {summary.get('selected_inventory_count', len(INVENTORY))}.", '',
             f"Execution: {summary['execution_status']}. Release gate: {summary['release_gate']}.", '',
             f"Results: {json.dumps(summary['summary'])}. Original hashes: {summary['original_hashes']}.", '',
             f"Source commit: {hardware['commit']}; dirty worktree: {hardware['worktree_dirty']}.",
             f"Source tree SHA-256: {hardware['source_tree_sha256']}.",
             f"Test executable SHA-256: {hardware['test_executable_sha256']}.",
             f"Build provenance: {summary.get('build_provenance', {}).get('status', 'UNVERIFIED')}. "
             f"Source consistency: {summary.get('source_consistency', 'UNVERIFIED')}.", '',
             f"Quality-only policy: {summary.get('quality_only', False)}. "
             f"Deferred implemented load adapters: {len(summary.get('deferred_load_implemented', []))}; "
             f"deferred unadapted inventory: {len(summary.get('deferred_load_unadapted', []))}.",
             'Policy deferral is SKIPPED and never PASS or full release qualification.',
             f"Unsupported-backend adapters: {len(summary.get('unsupported_backend', []))}. "
             'These checks were not executed and keep execution incomplete.',
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


def main(*, inventory=None, executable_ids=None, fixture_factory=None,
         domain='specification', default_report_name='ux-report'):
    selected_inventory = INVENTORY if inventory is None else inventory
    selected_executable = NATIVE if executable_ids is None else executable_ids
    make_fixtures = fixtures if fixture_factory is None else fixture_factory
    if domain not in ('specification', 'supplemental'):
        raise ValueError('Unknown UX inventory domain')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build-verify')
    parser.add_argument('--app', type=Path)
    parser.add_argument('--suite', choices=['all', 'blocking', 'regression', 'stress', 'perf', 'chaos'], default=None)
    parser.add_argument('--through', default=source_phase())
    parser.add_argument('--phase')
    parser.add_argument('--case', action='append', default=[])
    parser.add_argument('--runs', type=int, default=1)
    parser.add_argument('--actions', type=int, default=None)
    parser.add_argument('--seed', type=int, default=None)
    parser.add_argument('--replay', type=Path)
    parser.add_argument('--release-qualification', action='store_true')
    parser.add_argument('--quality-only', action='store_true',
                        help='Run bounded functional checks; defer reviewed load adapters without qualifying a full release')
    parser.add_argument('--require-build-provenance', action='store_true',
                        help='Require a matching attested build before execution; implied by --release-qualification')
    parser.add_argument('--artifacts', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--timeout', type=int, help='Override the per-scenario deadline in seconds')
    args = parser.parse_args()
    if domain != 'specification':
        if (args.release_qualification or args.phase is not None or args.seed is not None
                or args.actions is not None or args.replay is not None or args.quality_only):
            parser.error('Supplemental regressions use their own bounded fixture; official qualification, phase, quality-policy and chaos flags are unsupported')
        if args.suite not in (None, 'all', 'regression'):
            parser.error('Supplemental regressions support only all/regression selection')
    args.require_build_provenance = args.require_build_provenance or args.release_qualification
    if args.phase and not re.fullmatch(r'\d+\.\d+', args.phase):
        parser.error('--phase must be a numeric major.minor phase')
    if not re.fullmatch(r'\d+\.\d+', args.through):
        parser.error('--through must be a numeric major.minor phase')
    if args.release_qualification and tuple(map(int, args.through.split('.'))) < tuple(map(int, source_phase().split('.'))):
        parser.error('Release qualification cannot exclude the current source phase')
    if args.app:
        parser.error('--app is not an executable adapter yet; refusing an ignored request')
    if args.quality_only:
        if args.release_qualification:
            parser.error('--quality-only cannot request full release qualification')
        if args.suite in ('stress', 'perf', 'chaos') or args.seed is not None or args.actions is not None or args.replay:
            parser.error('--quality-only cannot request stress/performance/chaos/replay workloads')
    configure_history_chaos(args, parser)
    unknown = set(args.case) - {row['id'] for row in selected_inventory}
    if unknown:
        parser.error('Unknown scenario IDs: ' + ', '.join(sorted(unknown)))
    if args.runs < 1 or (args.timeout is not None and args.timeout < 1):
        parser.error('--runs and --timeout must be positive')
    selected_rows = [row for row in selected_inventory if row_selected(row, args)]
    fixture_rows = [row for row in selected_rows
                    if row['id'] in selected_executable and in_scope(row['scope'], args.through, row['id'])
                    and capability_implemented(row['scope'], row['id'])
                    and not (args.quality_only and quality_skip_reason(row))
                    and not (domain == 'specification' and unsupported_backend_requirement(row['id']))]
    case_profile_rows = [row for row in fixture_rows
                         if domain == 'specification' and row['id'] in CASE_PROFILE_IDS]
    base_fixture_rows = [row for row in fixture_rows if row not in case_profile_rows]
    protected_fixture_ids = CASE_PROFILE_IDS | NATIVE_ONLY_CONTRACTS.keys()
    profile_only_request = (domain == 'specification' and bool(args.case)
                            and set(args.case).issubset(protected_fixture_ids))
    # Native suites share one macOS focus/keyboard surface, including across worktrees.
    with (Path(tempfile.gettempdir()) / f'luma-ux-screen-{os.getuid()}.lock').open('a') as screen_lock:
        print('Waiting for exclusive UI test access...', flush=True)
        fcntl.flock(screen_lock.fileno(), fcntl.LOCK_EX)
        args.build = args.build.resolve()
        args.artifacts = (args.artifacts or args.build / 'ux-artifacts').resolve()
        args.report = (args.report or args.build / default_report_name).resolve()
        args.report.mkdir(parents=True, exist_ok=True)
        if args.require_build_provenance:
            preflight = build_provenance(args.build, fingerprint(args.build))
            if preflight['status'] != 'PASS':
                parser.error(f"Build provenance preflight failed ({preflight['path']}): {preflight['reason']}. "
                             f'Run tests/ux/build_ux.py --build {args.build}')
        run_root = args.artifacts.resolve() / (time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()) + f'-{os.getpid()}')
        run_root.mkdir(parents=True)
        fixture_folder = (run_root / 'public-fixtures-supplemental' if domain == 'supplemental'
                          else run_root / 'public-fixtures-quality' if args.quality_only
                          else args.artifacts.resolve() / 'public-fixtures')
        if args.quality_only:
            count = quality_fixture_count(base_fixture_rows)
            base_checksums = make_fixtures(fixture_folder, count=count, include_large=False) if count else {}
        elif not base_fixture_rows and (case_profile_rows or profile_only_request):
            base_checksums = {}
        else:
            base_checksums = make_fixtures(fixture_folder)
        case_fixture_folders = {}
        case_profiles = []
        hash_profiles = [(fixture_folder, base_checksums)]
        for row in case_profile_rows:
            folder = run_root / 'case-fixtures' / row['id']
            if row['id'] in CATALOG_SCALE_IDS:
                extra_checksums, profile = catalog_scale_fixture(folder, owned_root=run_root, case_id=row['id'])
            elif row['id'] in AI_PROFILE_IDS:
                extra_checksums, profile = ai_fixture(folder, owned_root=run_root, case_id=row['id'])
            elif row['id'] in MERGE_PROFILE_IDS:
                extra_checksums, profile = merge_fixture(folder, owned_root=run_root, case_id=row['id'])
            elif row['id'] == WATERMARK_CASE:
                extra_checksums, profile = watermark_fixture(folder, owned_root=run_root)
            elif row['id'] == CROP_DONE_CASE:
                extra_checksums, profile = crop_done_fixture(folder, owned_root=run_root)
            elif row['id'] == CURRENT_FILENAME_CASE:
                extra_checksums, profile = current_filename_fixture(folder, owned_root=run_root)
            elif row['id'] == METADATA_DISPLAY_CASE:
                extra_checksums, profile = metadata_display_fixture(folder, owned_root=run_root)
            elif row['id'] == PARTIAL_RENDER_CASE:
                extra_checksums, profile = partial_render_fixture(folder, owned_root=run_root)
            else:
                extra_checksums, profile = thumbnail_fixture(folder, owned_root=run_root, case_id=row['id'])
            case_fixture_folders[row['id']] = folder
            case_profiles.append(profile)
            hash_profiles.append((folder, extra_checksums))
        checksums = merge_fixture_hashes(hash_profiles)
        base_photo_count = sum(Path(name).parent == fixture_folder and
                               Path(name).suffix.lower() == '.png' for name in base_checksums)
        fixture_profile = {
            'directory': str(fixture_folder), 'public_photo_count': base_photo_count,
            'base_original_checksum_count': len(base_checksums),
            'large_24mp_generated': str(fixture_folder / 'stress' / 'Large.png') in base_checksums,
            'case_profiles': case_profiles,
            'extra_public_photo_count': sum(profile['public_photo_count'] for profile in case_profiles),
            'extra_original_checksum_count': sum(profile['original_checksum_count'] for profile in case_profiles),
            'original_checksum_count': len(checksums),
        }
        atomic_json(run_root / 'fixture-manifest.json',
                    {'schema': 1, 'fixture_profile': fixture_profile, 'original_sha256': checksums})
        suite_start = fingerprint(args.build)
        provenance = build_provenance(args.build, suite_start)
        if args.require_build_provenance and provenance['status'] != 'PASS':
            parser.error(f"Build provenance changed during fixture setup ({provenance['path']}): {provenance['reason']}. "
                         f'Run tests/ux/build_ux.py --build {args.build}')
        results = []
        global_rows = []
        observed_mutations = set()
        for row in selected_rows:
            identifier, scope, mode = row['id'], row['scope'], row['mode']
            repetitions = repetitions_for(row, args)
            applicable = in_scope(scope, args.through, identifier)
            capable = capability_implemented(scope, identifier)
            if not applicable or not capable or identifier == 'UX-MSK-005':
                results.append({**row, 'status': 'NOT_IMPLEMENTED', 'notes': ['Capability absent; future phase/conditional tool']})
                continue
            policy_skip = quality_skip_reason(row) if args.quality_only else None
            if (policy_skip and policy_skip[0] == 'policy_unreviewed'
                    and identifier not in selected_executable and identifier not in GLOBAL):
                policy_skip = None  # Preserve missing-adapter accounting for unimplemented functional checks.
            if policy_skip:
                skip_reason, reason = policy_skip
                results.append({**row, 'status': 'SKIPPED', 'skip_reason': skip_reason,
                                'adapter_implemented': identifier in selected_executable,
                                'notes': ['Deferred by the functional-only execution policy: ' + reason]})
                continue
            if identifier in GLOBAL:
                global_rows.append(row)
                continue
            if identifier not in selected_executable:
                results.append({**row, 'status': 'SKIPPED', 'skip_reason': 'unadapted',
                                'notes': ['No verified executable adapter yet; current scope qualification remains incomplete']})
                continue
            unsupported = (unsupported_backend_requirement(identifier)
                           if domain == 'specification' else None)
            if unsupported:
                results.append({**row, 'status': 'SKIPPED', 'skip_reason': 'unsupported_backend',
                                'adapter_implemented': True, 'backend_requirement': unsupported,
                                'notes': ['Not executed: the existing GITHUB_ACTIONS child route forces '
                                          'offscreen/software, which cannot qualify this actual-native contract. '
                                          'No actual runtime backend was observed.']})
                continue
            for repeat in range(repetitions):
                result = execute_scenario(row, run_root / identifier / f'repeat-{repeat + 1}', args,
                                          case_fixture_folders.get(identifier, fixture_folder))
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
        source_stable = (all(suite_start[field] == suite_end[field]
                             for field in ('source_tree_sha256', 'test_executable_sha256'))
                         and suite_start.get('native_bundle_info') == suite_end.get('native_bundle_info'))
        summary = {'hardware': suite_end, 'build_provenance': provenance,
                   'inventory_domain': domain, 'official_inventory_count': len(INVENTORY),
                   'selected_inventory_count': len(selected_inventory),
                   'source_consistency': 'PASS' if source_stable else 'CHANGED_DURING_SUITE', 'scope_through': args.through, 'run_directory': str(run_root),
                   'summary': dict(Counter(r['status'] for r in results)), 'original_hashes': 'FAIL' if mutations else 'PASS',
                   'results': results, 'quality_only': args.quality_only,
                   'fixture_profile': fixture_profile,
                   'deferred_load_implemented': sorted({r['id'] for r in results
                                                       if r.get('skip_reason') == 'deferred_load' and r['adapter_implemented']}),
                   'deferred_load_unadapted': sorted({r['id'] for r in results
                                                     if r.get('skip_reason') == 'deferred_load' and not r['adapter_implemented']}),
                   'policy_unreviewed': sorted({r['id'] for r in results
                                                if r.get('skip_reason') == 'policy_unreviewed'}),
                   'unsupported_backend': sorted({r['id'] for r in results
                                                  if r.get('skip_reason') == 'unsupported_backend'})}
        selected_failed = bool(mutations) or any(r['status'] == 'FAIL' for r in results)
        if domain == 'specification':
            summary['release_gate'], blocking_missing = qualify(results, selected_inventory, args.through,
                                                               qualification_requested(args))
        else:
            summary['release_gate'], blocking_missing = 'NOT_QUALIFIED', []
        incomplete = any(r['status'] == 'SKIPPED' and r.get('skip_reason', 'unadapted') in ('unadapted', 'policy_unreviewed', 'unsupported_backend')
                         for r in results) or not any(r.get('child_pid') for r in results)
        summary['unadapted_applicable'] = sorted({r['id'] for r in results if r['status'] == 'SKIPPED'
                                                and r.get('skip_reason', 'unadapted') == 'unadapted'
                                                and in_scope(r.get('scope', 'CURRENT'), args.through, r['id'])})
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
