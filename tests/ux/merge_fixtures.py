"""Owned, radiometrically bracketed Bayer sources and overlapping panorama crops."""
import hashlib
import importlib.util
import math
from pathlib import Path
import struct
import zlib

CASE_PROFILE_IDS = {
    'UX-P10-DAY-012',
    'UX-P10-UPG-012',
    'UX-P10-DAY-011',
    'UX-P09-FLT-006',
    'UX-P07-DNS-001', 'UX-P07-DNS-002', 'UX-P07-DNS-008',
    *(f'UX-P08-MRG-{i:03}' for i in (1, 3, 4, 5, 6, 9, 10)),
    'UX-P08-HDR-001', 'UX-P08-HDR-009', 'UX-P08-PAN-001', 'UX-P08-PAN-002',
    'UX-P08-FLT-002', 'UX-P08-PAN-004', 'UX-P08-ALG-002',
    'UX-P08-PAN-012', 'UX-P08-FLT-001',
    'UX-P08-HDR-006', 'UX-P08-HDR-007', 'UX-P08-DGH-001', 'UX-P08-ALG-001',
}
ROOT = Path(__file__).resolve().parents[2]


def texture(x, y):
    # Non-periodic, spatially coherent detail survives Bayer reconstruction and
    # exposure clipping. Both panorama crops use the same world coordinates.
    tile = ((x // 17 * 73856093) ^ (y // 13 * 19349663)) % 101 / 100
    return .12 + .46 * tile + .08 * math.sin(x / 7 + y / 11)


def png(path, dx, dy):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    raw = bytearray()
    for y in range(192):
        raw.append(0)
        for x in range(256):
            value = round(255 * texture(x + dx, y + dy))
            raw.extend((value, value, value))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 256, 192, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def official_fixture(folder, *, owned_root, case_id):
    folder, owned_root = Path(folder), Path(owned_root)
    if (not owned_root.is_absolute() or owned_root.resolve() != owned_root
            or not owned_root.is_dir() or owned_root.is_symlink()):
        raise ValueError('Merge profile requires a canonical owned run directory')
    if case_id not in CASE_PROFILE_IDS or folder != owned_root / 'case-fixtures' / case_id:
        raise ValueError('Merge profile requires its exact owned case directory')
    if folder.resolve() != folder or folder.exists() or folder.is_symlink():
        raise ValueError('Merge profile requires a fresh directory without aliases')
    folder.mkdir(parents=True)
    daily = case_id == 'UX-P10-DAY-011'
    panorama = case_id.startswith(('UX-P08-PAN-', 'UX-P08-ALG-'))
    rejected = case_id in ('UX-P08-PAN-004', 'UX-P08-ALG-002')
    offset = (4096, 8192) if rejected else (0, 48) if case_id == 'UX-P08-PAN-002' else (64, 0)
    if panorama:
        png(folder / 'Frame01.png', 0, 0)
        png(folder / 'Frame02.png', *offset)
    else:
        spec = importlib.util.spec_from_file_location('merge_dng', ROOT / 'tests/assets/generate_dng.py')
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        for i, (gain, shutter) in enumerate(((.25, (1, 500)), (1, (1, 125)), (4, (4, 125)))):
            def sample(x, y, gain=gain, frame=i):
                # The rightmost highlight exceeds SDR white at the middle exposure.
                if case_id == 'UX-P08-HDR-006':
                    x += (-6, 0, 6)[frame]
                scene = texture(x, y) + (1.2 if x > 210 and 48 < y < 144 else 0)
                if case_id in ('UX-P08-HDR-007', 'UX-P08-DGH-001'):
                    left = (48, 112, 176)[frame]
                    if left <= x < left + 32 and 56 <= y < 96:
                        scene = .78
                return min(.99, scene * gain)
            generator.create_dng(folder / f'Frame{i + 1:02}.dng', width=256, height=192,
                                 sample=sample, exif_ifd=True, shutter=shutter)
    if daily:
        png(folder / 'Frame01.png', 0, 0)
        png(folder / 'Frame02.png', 64, 0)
    update = case_id == 'UX-P10-UPG-012'
    if update:
        from thumbnail_response_fixtures import palette_png
        for index in range(32):
            palette_png(folder / f'Photo-{index:04}.png', index)
    files = sorted(folder.iterdir())
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}, {
        'case_id': case_id, 'name': 'merge-owned-bracket-overlap-v1', 'directory': str(folder),
        'public_photo_count': 32 if update else 2 if daily else len(files) if panorama else 0,
        'original_checksum_count': len(files), 'width': 256, 'height': 192,
        'large_24mp_generated': False, 'panorama': None if daily else panorama,
        'workflow_variants': (['save', 'import', 'export', 'batch', 'merge', 'merge-cancel'] if update
                              else ['hdr', 'panorama'] if daily else ['panorama' if panorama else 'hdr']),
        'offset': list(offset) if panorama else None, 'expected_alignment_rejection': rejected,
        'shutter_seconds': None if panorama else [1 / 500, 1 / 125, 4 / 125],
    }
