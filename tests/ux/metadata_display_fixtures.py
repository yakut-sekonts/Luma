"""One owned DNG with a real EXIF IFD for all six capture-data fields."""
import hashlib
import importlib.util
from pathlib import Path

OFFICIAL_CASE = 'UX-P09-META-001'
PROFILE_NAME = 'metadata-exif-ifd-dng1-v1'
ROOT = Path(__file__).resolve().parents[2]


def official_fixture(folder, *, owned_root):
    folder, owned_root = Path(folder), Path(owned_root)
    if (not owned_root.is_absolute() or owned_root.resolve() != owned_root
            or not owned_root.is_dir() or owned_root.is_symlink()):
        raise ValueError('Metadata profile requires a canonical owned run directory')
    if folder != owned_root / 'case-fixtures' / OFFICIAL_CASE:
        raise ValueError('Metadata profile requires its exact owned case directory')
    if folder.resolve() != folder or folder.exists() or folder.is_symlink():
        raise ValueError('Metadata profile requires a fresh directory without aliases')
    folder.parent.mkdir(exist_ok=True)
    (folder / 'stress').mkdir(parents=True)
    source = folder / 'stress' / 'Lens.DNG'
    spec = importlib.util.spec_from_file_location('metadata_dng', ROOT / 'tests/assets/generate_dng.py')
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    generator.create_dng(source, maker='NIKON CORPORATION', model='NIKON D7500',
                         lens='AF-S DX Nikkor 18-140mm f/3.5-5.6G ED VR',
                         focal=18, aperture=5.6, exif_ifd=True)
    return {str(source): hashlib.sha256(source.read_bytes()).hexdigest()}, {
        'case_id': OFFICIAL_CASE, 'name': PROFILE_NAME, 'directory': str(folder),
        'public_photo_count': 0, 'original_checksum_count': 1,
        'dng_width': 128, 'dng_height': 96, 'exif_ifd': True,
        'expected_iso': 100, 'expected_shutter_seconds': 1 / 125,
        'expected_aperture': 5.6, 'expected_focal_length_mm': 18,
        'large_24mp_generated': False,
    }
