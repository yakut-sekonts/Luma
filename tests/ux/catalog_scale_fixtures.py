"""Declare metadata-only catalog sizes without generating unrelated photographs."""
from pathlib import Path

CASE_ROWS = {'UX-P09-CAT-001': 10000, 'UX-P09-CAT-006': 100000, 'UX-P09-CAT-011': 500000}
CASE_PROFILE_IDS = set(CASE_ROWS)


def official_fixture(folder, *, owned_root, case_id):
    folder, owned_root = Path(folder), Path(owned_root)
    if (not owned_root.is_absolute() or owned_root.resolve() != owned_root
            or not owned_root.is_dir() or owned_root.is_symlink()):
        raise ValueError('Catalog profile requires a canonical owned run directory')
    if case_id not in CASE_ROWS or folder != owned_root / 'case-fixtures' / case_id:
        raise ValueError('Catalog profile requires its exact owned case directory')
    if folder.resolve() != folder or folder.exists() or folder.is_symlink():
        raise ValueError('Catalog profile requires a fresh directory without aliases')
    folder.mkdir(parents=True)
    return {}, {'case_id': case_id, 'name': 'metadata-only-catalog-v1',
                'directory': str(folder), 'metadata_rows': CASE_ROWS[case_id],
                'public_photo_count': 0, 'original_checksum_count': 0,
                'large_24mp_generated': False}
