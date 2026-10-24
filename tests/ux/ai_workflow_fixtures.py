"""Pinned redistributable photographs for actual local AI workflow checks."""
import hashlib
from pathlib import Path
import shutil
import struct
import zlib

CASE_PROFILE_IDS = {'UX-P07-MSK-001', 'UX-P07-MSK-003', 'UX-P07-SEM-001', 'UX-P07-REM-001', 'UX-P07-REM-002', 'UX-P07-REM-003'}
ROOT = Path(__file__).resolve().parents[2]
PORTRAIT_SHA256 = 'b7224c8c68fc7aaf76657b0d039e9fac69c00117d58523e5d2a11fe7cd2fbe61'


def official_fixture(folder, *, owned_root, case_id):
    folder, owned_root = Path(folder), Path(owned_root)
    if (not owned_root.is_absolute() or owned_root.resolve() != owned_root
            or not owned_root.is_dir() or owned_root.is_symlink()):
        raise ValueError('AI workflow requires a canonical owned run directory')
    if case_id not in CASE_PROFILE_IDS or folder != owned_root / 'case-fixtures' / case_id:
        raise ValueError('AI workflow requires its exact owned case directory')
    if folder.resolve() != folder or folder.exists() or folder.is_symlink():
        raise ValueError('AI workflow requires a fresh directory without aliases')
    if case_id.startswith('UX-P07-REM-'):
        folder.mkdir(parents=True)
        width, height = 256, 192
        def chunk(kind, data):
            return (struct.pack('>I', len(data)) + kind + data
                    + struct.pack('>I', zlib.crc32(kind + data)))
        rows = bytearray()
        for y in range(height):
            rows.append(0)
            for x in range(width):
                level = 240 if 115 <= x <= 119 and 81 <= y <= 85 else 64
                rows.extend((level, level, level))
        target = folder / 'Defect.png'
        target.write_bytes(b'\x89PNG\r\n\x1a\n'
                           + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
        return {str(target): hashlib.sha256(target.read_bytes()).hexdigest()}, {
            'case_id': case_id, 'name': 'generated-removal-defect-v1', 'directory': str(folder),
            'public_photo_count': 1, 'original_checksum_count': 1, 'large_24mp_generated': False,
            'width': width, 'height': height, 'defect_rectangle': [115, 81, 119, 85],
            'background_rgb8': 64, 'defect_rgb8': 240,
        }
    if case_id == 'UX-P07-SEM-001':
        names = {
            'Portrait': PORTRAIT_SHA256,
            'Landscape': '65f8cab61ebac24d87257397d9964af17e2972a5eb47d507ee44f3c0209c5b3d',
            'Night': 'a10274f9ca55d6dbd6f09de8473831bbd3349d604149b7d497390b5e37845985',
        }
        hashes = {}
        for name, digest in names.items():
            source = ROOT / 'tests/assets/filters' / (name.lower() + '.png')
            if source.is_symlink() or hashlib.sha256(source.read_bytes()).hexdigest() != digest:
                raise ValueError('Pinned semantic photograph is missing or changed')
        folder.mkdir(parents=True)
        for name, digest in names.items():
            target = folder / (name + '.png')
            shutil.copyfile(ROOT / 'tests/assets/filters' / (name.lower() + '.png'), target)
            if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
                raise ValueError('Owned semantic photo changed')
            hashes[str(target)] = digest
        return hashes, {
            'case_id': case_id, 'name': 'actual-local-semantic-photos-v1', 'directory': str(folder),
            'public_photo_count': 3, 'original_checksum_count': 3, 'large_24mp_generated': False,
            'source_license_manifest': 'tests/assets/filters/manifest.json',
        }
    source = ROOT / 'tests/assets/filters/portrait.png'
    if source.is_symlink() or hashlib.sha256(source.read_bytes()).hexdigest() != PORTRAIT_SHA256:
        raise ValueError('Pinned public-domain portrait is missing or changed')
    folder.mkdir(parents=True)
    copied = folder / 'Portrait.png'
    shutil.copyfile(source, copied)
    if hashlib.sha256(copied.read_bytes()).hexdigest() != PORTRAIT_SHA256:
        raise ValueError('Owned portrait copy changed')
    return {str(copied): PORTRAIT_SHA256}, {
        'case_id': case_id, 'name': 'actual-local-ai-portrait-v1', 'directory': str(folder),
        'public_photo_count': 1, 'original_checksum_count': 1, 'large_24mp_generated': False,
        'source_license_manifest': 'tests/assets/filters/manifest.json', 'width': 512, 'height': 512,
    }
