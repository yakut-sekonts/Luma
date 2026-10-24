"""Actual 0.5.1 catalog fixture and independent read-only migration audit."""
from contextlib import closing
import hashlib
import json
from pathlib import Path
import shutil
import sqlite3

from catalog_invariants import canonical, read_database, SHUTDOWN_MARKERS, STAGING_PREFIXES

FIXTURE = Path(__file__).resolve().parents[1] / 'assets/migration-051'
ORIGINALS = ('01-complete.DNG', '02-global-local.DNG', '03-common.webp')
FILES = frozenset((*ORIGINALS, *(name + '.png' for name in ORIGINALS)))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(path):
    path = canonical(path)
    with closing(sqlite3.connect(path.as_uri() + '?mode=ro', uri=True)) as db:
        if db.execute('PRAGMA integrity_check').fetchall() != [('ok',)]:
            raise ValueError('Migration catalog integrity failed')
        if db.execute('PRAGMA foreign_key_check').fetchall():
            raise ValueError('Migration catalog has orphan references')
        tables = {}
        for (name,) in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"):
            quoted = '"' + name.replace('"', '""') + '"'
            columns = [row[1] for row in db.execute('PRAGMA table_info(' + quoted + ')')]
            order = ','.join(str(i + 1) for i in range(len(columns)))
            rows = [list(row) for row in db.execute('SELECT * FROM ' + quoted + ' ORDER BY ' + order)]
            tables[name] = {'columns': columns, 'rows': rows}
        return {'schema': db.execute('PRAGMA user_version').fetchone()[0], 'tables': tables}


def load_fixture(folder=FIXTURE):
    folder = canonical(folder, directory=True)
    fixture = json.loads((folder / 'fixture.json').read_text())
    if (fixture['schema'] != 1 or fixture['stable_version'] != '0.5.1'
            or fixture['stable_commit'] != 'd113495cb1d92bf8232e0a6943d972c9fbf0e01a'
            or fixture['baseline']['schema'] != 11 or set(fixture['files']) != FILES):
        raise ValueError('Unexpected historical migration fixture')
    for name, expected in fixture['files'].items():
        path = canonical(folder / name)
        if path.stat().st_size != expected['size'] or digest(path) != expected['sha256']:
            raise ValueError('Historical fixture checksum differs: ' + name)
    assets = fixture['baseline']['tables']['assets']
    path_column = assets['columns'].index('original_path')
    if sorted(row[path_column] for row in assets['rows']) != ['@ORIGINALS@/' + n for n in ORIGINALS]:
        raise ValueError('Unexpected historical original paths')
    return fixture


def prepare_migration(state_root, folder=FIXTURE):
    # Exclusive fresh owned state: never open or overwrite a user catalog.
    state_root.mkdir(parents=True, exist_ok=False)
    state_root = canonical(state_root, directory=True)
    fixture = load_fixture(folder)
    originals = state_root / 'originals'
    originals.mkdir()
    for name in FILES:
        shutil.copyfile(folder / name, originals / name)
    baseline = fixture['baseline']
    assets = baseline['tables']['assets']
    index = assets['columns'].index('original_path')
    for row in assets['rows']:
        row[index] = str(originals / row[index].removeprefix('@ORIGINALS@/'))
    catalog = state_root / 'catalog.sqlite'
    with closing(sqlite3.connect(catalog)) as db, db:
        # Foreign keys are checked after the complete historical snapshot is inserted.
        # Install initialization triggers after insertion so they cannot invent rows.
        for sql in fixture['sql']:
            if sql.startswith('CREATE TABLE '):
                db.execute(sql)
        for name, values in baseline['tables'].items():
            quoted = '"' + name.replace('"', '""') + '"'
            placeholders = ','.join('?' for _ in values['columns'])
            db.executemany('INSERT INTO ' + quoted + ' VALUES(' + placeholders + ')', values['rows'])
        for sql in fixture['sql']:
            if not sql.startswith('CREATE TABLE '):
                db.execute(sql)
        db.execute('PRAGMA user_version=11')
    if snapshot(catalog) != baseline:
        raise ValueError('Reconstructed catalog differs from actual 0.5.1 baseline')
    (state_root / 'historical-baseline.json').write_text(json.dumps(baseline, indent=2) + '\n')
    return baseline


def compare_historical(before, after):
    if before['schema'] != 11 or after['schema'] != 21:
        raise ValueError('Unexpected migration schema transition')
    for name, expected in before['tables'].items():
        actual = after['tables'].get(name)
        if not actual or expected['columns'] != actual['columns']:
            raise ValueError('Historical columns changed: ' + name)
        if name == 'schema_migrations':
            if not all(row in actual['rows'] for row in expected['rows']):
                raise ValueError('Historical migration records lost')
        elif name == 'edit_local_masks':
            if ([row[0] for row in actual['rows']] != [row[0] for row in expected['rows']]
                    or not all(row[1].startswith('LUMA_LOCAL_MASKS 2 ') for row in expected['rows'])
                    or not all(row[1].startswith('LUMA_LOCAL_MASKS 3 ') for row in actual['rows'])):
                raise ValueError('Historical mask identities or format changed unexpectedly')
            # Full decoded equality is checked by the child; old export pixels are independent.
        elif actual != expected:
            raise ValueError('Historical table values changed: ' + name)


def audit_migration(state_root, evidence_root, child_pid, stage):
    state_root = canonical(state_root, directory=True)
    evidence_root = canonical(evidence_root, directory=True)
    if stage not in ('prepare', 'verify'):
        raise ValueError('Unknown migration stage')
    log = (evidence_root / 'app.log').read_text()
    if not all('UX_SHUTDOWN phase=' + m in log for m in SHUTDOWN_MARKERS):
        raise ValueError('Migration clean shutdown was not observed')
    decoded = json.loads((evidence_root / 'serialized-audit.json').read_text())
    if (decoded.get('pid') != child_pid or decoded.get('status') != 'PASS'
            or decoded.get('legacy_masks_equal') is not True
            or decoded.get('stable_export_pixels_exact') is not True):
        raise ValueError('Migration serialized audit does not belong to the child')
    catalog = state_root / 'catalog.sqlite'
    data = read_database(catalog)
    before = json.loads((state_root / 'historical-baseline.json').read_text())
    after = snapshot(catalog)
    compare_historical(before, after)
    assets = data.pop('assets')
    ids = [row[0] for row in assets]
    if len(ids) != 3 or sorted(decoded.get('asset_ids', [])) != ids:
        raise ValueError('Migration serialized audit does not cover every asset')
    originals = {}
    fixture = load_fixture()
    for identity, source, checksum in assets:
        path = canonical(source)
        if path.parent != state_root / 'originals' or path.name not in ORIGINALS:
            raise ValueError('Migrated original is outside the owned fixture')
        if digest(path) != checksum or checksum != fixture['files'][path.name]['sha256']:
            raise ValueError('Migrated original checksum differs: ' + identity)
        originals[path.name] = checksum
    backups = list((state_root / 'catalog.sqlite.luma-backups').glob('schema-11-*.sqlite3'))
    if len(backups) != 1 or snapshot(backups[0]) != before:
        raise ValueError('Missing or changed pre-migration backup')
    for path in state_root.rglob('*'):
        if path.is_symlink() or path.name.endswith('.tmp') or path.name.startswith(STAGING_PREFIXES):
            raise ValueError('Migration contains aliased or orphan staging file: ' + str(path))
    previous = state_root / 'first-migration.json'
    if stage == 'verify' and json.loads(previous.read_text()) != after:
        raise ValueError('Catalog changed after migration restart')
    result = {'status': 'PASS', 'old_schema': 11, 'new_schema': 21,
              'historical_tables': len(before['tables']), **data,
              'asset_ids': ids, 'original_sha256': originals,
              'pre_migration_backup_sha256': digest(backups[0]),
              'serialized_documents': decoded, 'clean_shutdown': True,
              'restart_idempotence': stage == 'verify'}
    return result, after
