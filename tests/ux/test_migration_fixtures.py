"""Negative controls for the independent migration auditor, not a fake migration test."""
from contextlib import closing
import json
from pathlib import Path
import shutil
import sqlite3
import tempfile
import unittest

from migration_fixtures import (FIXTURE, audit_migration, load_fixture, prepare_migration, snapshot)


class MigrationAuditTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='luma-migration-audit-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.state = self.root / 'state'
        self.before = prepare_migration(self.state)
        self.catalog = self.state / 'catalog.sqlite'
        self.evidence = self.root / 'evidence'
        self.evidence.mkdir()
        backups = self.state / 'catalog.sqlite.luma-backups'
        backups.mkdir()
        self.backup = backups / 'schema-11-fixture.sqlite3'
        shutil.copyfile(self.catalog, self.backup)
        # Deliberately minimal auditor control; only the native app exercises real migrations.
        with closing(sqlite3.connect(self.catalog)) as db, db:
            db.execute('PRAGMA user_version=21')
            db.execute('CREATE TABLE derived_images(path TEXT PRIMARY KEY)')
            db.execute("UPDATE edit_local_masks SET document=replace(document,'LUMA_LOCAL_MASKS 2 ','LUMA_LOCAL_MASKS 3 ')")
        self.ids = [r[0] for r in self.before['tables']['assets']['rows']]
        self.marker = self.evidence / 'serialized-audit.json'
        self.marker.write_text(json.dumps({'status': 'PASS', 'pid': 123, 'asset_ids': self.ids,
                                          'legacy_masks_equal': True, 'stable_export_pixels_exact': True}))
        (self.evidence / 'app.log').write_text('UX_SHUTDOWN phase=context.owner.destroy.end\n'
                                              'UX_SHUTDOWN phase=qguiapplication.owner.destroy.end\n')

    def audit(self, stage='prepare'):
        return audit_migration(self.state, self.evidence, 123, stage)

    def sql(self, statement, values=()):
        with closing(sqlite3.connect(self.catalog)) as db, db:
            db.execute(statement, values)

    def test_valid_auditor_control_is_read_only(self):
        before = {p: p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        result, after = self.audit()
        self.assertEqual(result['status'], 'PASS')
        self.assertEqual(len(result['asset_ids']), 3)
        self.assertEqual(before, {p: p.read_bytes() for p in before})
        (self.state / 'first-migration.json').write_text(json.dumps(after))
        self.assertTrue(self.audit('verify')[0]['restart_idempotence'])

    def test_reconstruction_is_exact_and_refuses_existing_state(self):
        state = self.root / 'another'
        baseline = prepare_migration(state)
        self.assertEqual(snapshot(state / 'catalog.sqlite'), baseline)
        with self.assertRaises(FileExistsError):
            prepare_migration(state)

    def test_changed_historical_value_is_rejected(self):
        self.sql('UPDATE edit_documents SET exposure=0.7')
        with self.assertRaisesRegex(ValueError, 'table values changed'):
            self.audit()

    def test_loss_of_mandatory_document_is_rejected(self):
        self.sql('PRAGMA foreign_keys=OFF')
        self.sql('DELETE FROM edit_documents')
        with self.assertRaisesRegex(ValueError, 'Orphaned|no saved edit'):
            self.audit()

    def test_unchanged_mask_format_is_rejected(self):
        self.sql("UPDATE edit_local_masks SET document=replace(document,'LUMA_LOCAL_MASKS 3 ','LUMA_LOCAL_MASKS 2 ')")
        with self.assertRaisesRegex(ValueError, 'mask identities or format'):
            self.audit()

    def test_corrupt_missing_or_modified_backup_is_rejected(self):
        original = self.backup.read_bytes()
        self.backup.write_bytes(b'Not SQLite')
        with self.assertRaises(sqlite3.DatabaseError):
            self.audit()
        self.backup.write_bytes(original)
        with closing(sqlite3.connect(self.backup)) as db, db:
            db.execute('UPDATE assets SET rating=1')
        with self.assertRaisesRegex(ValueError, 'pre-migration backup'):
            self.audit()
        self.backup.unlink()
        with self.assertRaisesRegex(ValueError, 'pre-migration backup'):
            self.audit()

    def test_changed_original_is_rejected(self):
        (self.state / 'originals/01-complete.DNG').write_bytes(b'Changed')
        with self.assertRaisesRegex(ValueError, 'original checksum'):
            self.audit()

    def test_nonfinite_new_value_and_orphan_staging_are_rejected(self):
        self.sql('CREATE TABLE new_values(value REAL)')
        self.sql('INSERT INTO new_values VALUES(?)', (float('inf'),))
        with self.assertRaisesRegex(ValueError, 'Non-finite'):
            self.audit()
        self.sql('DROP TABLE new_values')
        (self.state / '.luma-copy-orphan').write_text('abandoned')
        with self.assertRaisesRegex(ValueError, 'orphan staging'):
            self.audit()

    def test_restart_mutation_of_new_table_is_rejected(self):
        _, after = self.audit()
        (self.state / 'first-migration.json').write_text(json.dumps(after))
        self.sql('CREATE TABLE unexpected(value INTEGER)')
        with self.assertRaisesRegex(ValueError, 'changed after migration restart'):
            self.audit('verify')

    def test_missing_shutdown_or_wrong_child_audit_is_rejected(self):
        d = json.loads(self.marker.read_text())
        d['pid'] = 124
        self.marker.write_text(json.dumps(d))
        with self.assertRaisesRegex(ValueError, 'does not belong'):
            self.audit()
        self.marker.unlink()
        with self.assertRaises(FileNotFoundError):
            self.audit()
        (self.evidence / 'app.log').write_text('')
        with self.assertRaisesRegex(ValueError, 'shutdown'):
            self.audit()

    def test_fixture_checksums_and_unsafe_original_paths_are_rejected(self):
        copied = self.root / 'fixture'
        shutil.copytree(FIXTURE, copied)
        data = json.loads((copied / 'fixture.json').read_text())
        data['baseline']['tables']['assets']['rows'][0][1] = '@ORIGINALS@/../external.DNG'
        (copied / 'fixture.json').write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, 'original paths'):
            load_fixture(copied)
        (copied / '01-complete.DNG').write_bytes(b'Changed')
        with self.assertRaisesRegex(ValueError, 'fixture checksum'):
            load_fixture(copied)


if __name__ == '__main__':
    unittest.main()
