"""A child's PASS cannot hide corrupted restored data or incomplete shutdown."""
import hashlib
from contextlib import closing
import json
from pathlib import Path
import shutil
import sqlite3
import tempfile
import unittest
from catalog_invariants import audit_restored_workflow, read_database


class RestoredDataTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='luma-data-audit-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.state = self.root / 'state'
        self.fixture = self.root / 'fixtures'
        self.evidence = self.root / 'verify'
        for path in (self.state / 'restored', self.fixture, self.evidence):
            path.mkdir(parents=True)
        self.catalog = self.state / 'restored/catalog.sqlite'
        self.source = self.fixture / 'original.png'
        self.source.write_bytes(b'Owned immutable original')
        self.digest = hashlib.sha256(self.source.read_bytes()).hexdigest()
        with closing(sqlite3.connect(self.catalog)) as database, database:
            database.executescript('''
                PRAGMA user_version=21;
                CREATE TABLE assets(id TEXT PRIMARY KEY, original_path TEXT, checksum TEXT);
                CREATE TABLE edit_documents(asset_id TEXT PRIMARY KEY REFERENCES assets(id), exposure REAL);
                CREATE TABLE edit_local_masks(asset_id TEXT REFERENCES assets(id), document TEXT);
                CREATE TABLE derived_images(path TEXT PRIMARY KEY);
                CREATE TABLE import_sessions(id TEXT PRIMARY KEY, state TEXT);
                CREATE TABLE import_items(session_id TEXT REFERENCES import_sessions(id), state TEXT);
            ''')
            database.execute('INSERT INTO assets VALUES(?,?,?)', ('photo', str(self.source), self.digest))
            database.execute('INSERT INTO edit_documents VALUES(?,?)', ('photo', 0.5))
        backups = self.state / 'catalog.sqlite.luma-backups'
        backups.mkdir()
        self.backup = backups / 'auto-fixture.sqlite3'
        shutil.copyfile(self.catalog, self.backup)
        self.marker = self.evidence / 'serialized-audit.json'
        self.marker.write_text(json.dumps({'status': 'PASS', 'pid': 123, 'asset_ids': ['photo']}))
        (self.evidence / 'app.log').write_text(
            'UX_SHUTDOWN phase=context.owner.destroy.end\n'
            'UX_SHUTDOWN phase=qguiapplication.owner.destroy.end\n')

    def audit(self):
        return audit_restored_workflow(self.catalog, self.state, self.fixture, self.evidence, 123)

    def sql(self, command, values=()):
        with closing(sqlite3.connect(self.catalog)) as database, database:
            database.execute(command, values)

    def test_complete_audit_is_read_only_and_covers_exact_assets(self):
        before = {p: p.read_bytes() for p in (self.catalog, self.source, self.backup)}
        result = self.audit()
        self.assertEqual(result['status'], 'PASS')
        self.assertEqual(result['asset_ids'], ['photo'])
        self.assertEqual(result['original_sha256'], {str(self.source): self.digest})
        self.assertEqual(result['numeric_values'], 1)
        self.assertEqual(len(result['readable_backups']), 1)
        self.assertEqual(before, {p: p.read_bytes() for p in before})

    def test_missing_edit_document_fails(self):
        self.sql('DELETE FROM edit_documents')
        with self.assertRaisesRegex(ValueError, 'no saved edit'):
            self.audit()

    def test_orphan_mask_fails(self):
        self.sql('INSERT INTO edit_local_masks VALUES(?,?)', ('missing', 'invalid'))
        with self.assertRaisesRegex(ValueError, 'Orphaned'):
            self.audit()

    def test_nonfinite_values_fail_in_any_table(self):
        for value in (float('inf'), -float('inf')):
            with self.subTest(value=value):
                self.sql('UPDATE edit_documents SET exposure=?', (value,))
                with self.assertRaisesRegex(ValueError, 'Non-finite'):
                    self.audit()

    def test_unfinished_import_and_derived_journals_fail(self):
        self.sql("INSERT INTO import_sessions VALUES('s','running')")
        with self.assertRaisesRegex(ValueError, 'running import'):
            self.audit()
        self.sql("UPDATE import_sessions SET state='completed'")
        self.sql("INSERT INTO import_items VALUES('s','staged')")
        with self.assertRaisesRegex(ValueError, 'pending import'):
            self.audit()
        self.sql("UPDATE import_items SET state='committed'")
        self.sql("INSERT INTO derived_images VALUES('/owned/unfinished.dng')")
        with self.assertRaisesRegex(ValueError, 'derived-image journal'):
            self.audit()

    def test_missing_required_schema_and_corruption_fail(self):
        self.sql('DROP TABLE import_items')
        with self.assertRaisesRegex(ValueError, 'tables are missing'):
            self.audit()
        self.catalog.write_bytes(b'Not SQLite')
        with self.assertRaises(sqlite3.DatabaseError):
            self.audit()

    def test_changed_original_fails(self):
        self.source.write_bytes(b'Changed')
        with self.assertRaisesRegex(ValueError, 'checksum differs'):
            self.audit()

    def test_source_outside_owned_roots_fails(self):
        external = self.root / 'external.png'
        shutil.copyfile(self.source, external)
        self.sql('UPDATE assets SET original_path=?', (str(external),))
        with self.assertRaisesRegex(ValueError, 'outside owned'):
            self.audit()

    def test_alias_paths_are_rejected(self):
        alias = self.fixture / 'alias.png'
        alias.symlink_to(self.source)
        self.sql('UPDATE assets SET original_path=?', (str(alias),))
        with self.assertRaisesRegex(ValueError, 'canonical'):
            self.audit()

    def test_staging_files_and_unregistered_derived_images_fail(self):
        staging = self.state / '.luma-copy-owned.tmp'
        staging.write_bytes(b'Unfinished')
        with self.assertRaisesRegex(ValueError, 'temporary/staging'):
            self.audit()
        staging.unlink()
        derived = self.state / 'catalog.sqlite.luma-derived'
        derived.mkdir()
        (derived / 'orphan.dng').write_bytes(b'Unregistered')
        with self.assertRaisesRegex(ValueError, 'Unregistered derived'):
            self.audit()

    def test_backup_must_exist_and_open(self):
        self.backup.write_bytes(b'Corrupt')
        with self.assertRaises(sqlite3.DatabaseError):
            self.audit()
        self.backup.unlink()
        with self.assertRaisesRegex(ValueError, 'No readable backup'):
            self.audit()

    def test_incomplete_teardown_fails(self):
        (self.evidence / 'app.log').write_text('UX_SHUTDOWN phase=context.owner.destroy.end\n')
        with self.assertRaisesRegex(ValueError, 'shutdown'):
            self.audit()

    def test_missing_or_wrong_document_validation_fails(self):
        for marker in ({'status': 'PASS', 'pid': 999, 'asset_ids': ['photo']},
                       {'status': 'PASS', 'pid': 123, 'asset_ids': []},
                       {'status': 'FAIL', 'pid': 123, 'asset_ids': ['photo']}):
            with self.subTest(marker=marker):
                self.marker.write_text(json.dumps(marker))
                with self.assertRaises(ValueError):
                    self.audit()
        self.marker.unlink()
        with self.assertRaises(FileNotFoundError):
            self.audit()

    def test_wrong_catalog_cannot_substitute_for_restored_copy(self):
        original = self.state / 'catalog.sqlite'
        shutil.copyfile(self.catalog, original)
        with self.assertRaisesRegex(ValueError, 'actual restored'):
            audit_restored_workflow(original, self.state, self.fixture, self.evidence, 123)

    def test_older_schema_requires_explicit_new_qualification(self):
        self.sql('PRAGMA user_version=20')
        with self.assertRaisesRegex(ValueError, 'schema'):
            read_database(self.catalog)


if __name__ == '__main__':
    unittest.main()
