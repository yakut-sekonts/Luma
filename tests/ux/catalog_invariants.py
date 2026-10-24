"""Independent, read-only post-exit audit of the owned backup/restore workflow."""
from contextlib import closing
import hashlib
import json
import math
from pathlib import Path
import sqlite3

REQUIRED_TABLES = frozenset(('assets', 'edit_documents', 'edit_local_masks',
                            'derived_images', 'import_sessions', 'import_items'))
SHUTDOWN_MARKERS = ('context.owner.destroy.end', 'qguiapplication.owner.destroy.end')
STAGING_PREFIXES = ('.luma-copy-', '.luma-move-', '.luma-backup-',
                    '.luma-derived-', '.luma-xmp-', '.luma-export-')


def canonical(path, *, directory=False):
    path = Path(path)
    if (not path.is_absolute() or path.resolve() != path or path.is_symlink()
            or not (path.is_dir() if directory else path.is_file())):
        raise ValueError('Audit requires a canonical existing owned path: ' + str(path))
    return path


def read_database(path):
    """Reject malformed, orphaned and non-finite persisted SQL values, without repair."""
    path = canonical(path)
    with closing(sqlite3.connect(path.as_uri() + '?mode=ro', uri=True)) as database:
        if database.execute('PRAGMA user_version').fetchone() != (21,):
            raise ValueError('Unexpected restored database schema')
        if database.execute('PRAGMA integrity_check').fetchall() != [('ok',)]:
            raise ValueError('Database integrity check failed')
        if database.execute('PRAGMA foreign_key_check').fetchall():
            raise ValueError('Orphaned database references')
        tables = {row[0] for row in database.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")}
        if not REQUIRED_TABLES <= tables:
            raise ValueError('Required data-safety tables are missing')
        numeric_values = 0
        for table in sorted(tables):
            quoted = '"' + table.replace('"', '""') + '"'
            for row in database.execute('SELECT * FROM ' + quoted):
                for value in row:
                    if isinstance(value, (float, int)):
                        numeric_values += 1
                        if not math.isfinite(value):
                            raise ValueError('Non-finite persisted numeric value in ' + table)
        if database.execute('SELECT 1 FROM assets a LEFT JOIN edit_documents e '
                            'ON e.asset_id=a.id WHERE e.asset_id IS NULL LIMIT 1').fetchone():
            raise ValueError('Asset has no saved edit document')
        if database.execute('SELECT 1 FROM derived_images LIMIT 1').fetchone():
            raise ValueError('Unfinished derived-image journal after shutdown')
        if database.execute("SELECT 1 FROM import_sessions WHERE state='running' LIMIT 1").fetchone():
            raise ValueError('Abandoned running import session')
        if database.execute("SELECT 1 FROM import_items WHERE state IN ('queued','staged') LIMIT 1").fetchone():
            raise ValueError('Abandoned pending import item')
        assets = list(database.execute('SELECT id,original_path,checksum FROM assets ORDER BY id'))
        return {'tables': len(tables), 'numeric_values': numeric_values, 'assets': assets}


def audit_restored_workflow(catalog, state_root, fixture_root, evidence_root, child_pid):
    state_root = canonical(state_root, directory=True)
    fixture_root = canonical(fixture_root, directory=True)
    evidence_root = canonical(evidence_root, directory=True)
    catalog = canonical(catalog)
    if catalog != state_root / 'restored/catalog.sqlite':
        raise ValueError('Audit must inspect the actual restored catalog')
    log = (evidence_root / 'app.log').read_text()
    if not all('UX_SHUTDOWN phase=' + marker in log for marker in SHUTDOWN_MARKERS):
        raise ValueError('Clean context/application shutdown was not observed')
    decoded = json.loads((evidence_root / 'serialized-audit.json').read_text())
    if decoded.get('pid') != child_pid or decoded.get('status') != 'PASS':
        raise ValueError('Serialized document validation is missing for this child')
    result = read_database(catalog)
    assets = result.pop('assets')
    identities = [row[0] for row in assets]
    if not identities or sorted(decoded.get('asset_ids', [])) != identities:
        raise ValueError('Decoded document audit does not cover every restored asset')
    original_hashes = {}
    for identity, source, checksum in assets:
        path = canonical(source)
        if not (path.is_relative_to(fixture_root) or path.is_relative_to(state_root)):
            raise ValueError('Restored asset points outside owned fixture/state roots')
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != checksum:
            raise ValueError('Restored original checksum differs: ' + identity)
        original_hashes[str(path)] = actual
    backups = {}
    for path in state_root.rglob('*'):
        if path.is_symlink():
            raise ValueError('Owned audit state contains a symbolic link')
        if not path.is_file():
            continue
        if path.suffix == '.tmp' or path.name.startswith(STAGING_PREFIXES):
            raise ValueError('Orphan temporary/staging file: ' + str(path))
        if path.parent.name.endswith('.luma-derived') and str(path) not in original_hashes:
            raise ValueError('Unregistered derived image: ' + str(path))
        if path.name.startswith('auto-') and path.suffix == '.sqlite3':
            backup = read_database(path)
            backups[str(path)] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                                  'asset_count': len(backup['assets'])}
    if not backups:
        raise ValueError('No readable backup was audited')
    return {'status': 'PASS', 'catalog': str(catalog), **result,
            'asset_ids': identities, 'original_sha256': original_hashes,
            'readable_backups': backups, 'serialized_documents': decoded,
            'clean_shutdown': True, 'orphan_staging_files': 0,
            'unfinished_imports': 0, 'unfinished_derived_images': 0}
