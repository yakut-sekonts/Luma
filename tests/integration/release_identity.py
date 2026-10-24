"""Package identity regressions using owned structural fixtures, without signing/network."""
import copy
import json
import os
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
import warnings
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from release_identity import verify_bundle_matches_zip, verify_zip_identity


class ReleaseIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='luma-identity-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.app = self.root / 'Luma.app'
        self.info = {'version': '1.0.0', 'source_commit': 'a' * 40,
                     'distribution': 'github-open-source', 'signing': 'ad-hoc',
                     'notarized': False, 'dependencies': {'Sparkle': '2.10.0'}}
        self.plist = {'CFBundleVersion': '1.0.0', 'CFBundleShortVersionString': '1.0.0',
                      'CFBundleIdentifier': 'app.luma.desktop', 'CFBundleExecutable': 'Luma'}
        self.write('Contents/Resources/BUILD_INFO.json', json.dumps(self.info).encode())
        self.write('Contents/Info.plist', plistlib.dumps(self.plist))
        self.write('Contents/MacOS/Luma', b'structural fixture, not an executable')
        (self.app / 'Contents/MacOS/Luma').chmod(0o755)
        self.write('Contents/Frameworks/Example.framework/Versions/A/Example', b'framework bytes')
        self.link = self.app / 'Contents/Frameworks/Example.framework/Versions/Current'
        self.link.symlink_to('A')
        self.archive = self.root / 'app.zip'
        self.pack()

    def write(self, name, data):
        path = self.app / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def pack(self):
        with zipfile.ZipFile(self.archive, 'w') as archive:
            for directory, dirs, files in os.walk(self.app, followlinks=False):
                for name in sorted(dirs + files):
                    path = Path(directory) / name
                    if path.is_dir() and not path.is_symlink():
                        continue
                    entry = zipfile.ZipInfo('Luma.app/' + path.relative_to(self.app).as_posix())
                    entry.create_system = 3
                    entry.external_attr = path.lstat().st_mode << 16
                    archive.writestr(entry, os.fsencode(os.readlink(path)) if path.is_symlink()
                                     else path.read_bytes())

    def verify(self):
        with zipfile.ZipFile(self.archive) as archive:
            return verify_bundle_matches_zip(self.app, archive, self.info)

    def test_identical_real_files_and_directory_symlink(self):
        result = self.verify()
        self.assertEqual(result['files_and_symlinks'], 5)
        self.assertEqual(result['source_commit'], self.info['source_commit'])
        self.assertGreater(result['regular_file_bytes'], 100)

    def test_changed_installer_file_rejected_with_unchanged_length(self):
        self.write('Contents/Frameworks/Example.framework/Versions/A/Example', b'Framework bytes')
        with self.assertRaisesRegex(ValueError, 'contents differ'):
            self.verify()

    def test_extra_or_missing_installer_file_rejected(self):
        extra = self.app / 'Contents/unexpected'
        extra.write_bytes(b'not in archive')
        with self.assertRaisesRegex(ValueError, 'members differ'):
            self.verify()
        extra.unlink()
        (self.app / 'Contents/MacOS/Luma').unlink()
        with self.assertRaisesRegex(ValueError, 'members differ'):
            self.verify()

    def test_changed_executable_permissions_rejected(self):
        (self.app / 'Contents/MacOS/Luma').chmod(0o644)
        with self.assertRaisesRegex(ValueError, 'metadata differ'):
            self.verify()

    def test_symlink_target_and_type_must_match_without_following(self):
        self.link.unlink()
        self.link.symlink_to('/nonexistent/foreign-framework')
        with self.assertRaisesRegex(ValueError, 'symlink targets differ'):
            self.verify()
        self.link.unlink()
        self.link.write_text('A')
        with self.assertRaisesRegex(ValueError, 'entry types differ'):
            self.verify()

    def test_duplicate_and_unsafe_zip_paths_rejected(self):
        for name in ('Luma.app/Contents/Info.plist', 'Luma.app/../outside', '/absolute',
                     'Luma.app//Contents/extra', 'Unexpected.app/file'):
            with self.subTest(name=name):
                self.pack()
                with warnings.catch_warnings(), zipfile.ZipFile(self.archive, 'a') as archive:
                    warnings.simplefilter('ignore', UserWarning)
                    archive.writestr(name, b'foreign member')
                with zipfile.ZipFile(self.archive) as archive, self.assertRaises(ValueError):
                    verify_zip_identity(archive, self.info)

    def test_symlinked_metadata_cannot_substitute_release_identity(self):
        path = self.app / 'Contents/Resources/BUILD_INFO.json'
        path.unlink()
        path.symlink_to('another.json')
        self.pack()
        with zipfile.ZipFile(self.archive) as archive, self.assertRaisesRegex(ValueError, 'regular'):
            verify_zip_identity(archive, self.info)

    def test_oversized_metadata_rejected_before_read(self):
        self.write('Contents/Resources/BUILD_INFO.json', b' ' * (1024 * 1024 + 1))
        self.pack()
        with zipfile.ZipFile(self.archive) as archive, self.assertRaisesRegex(ValueError, 'too large'):
            verify_zip_identity(archive, self.info)

    def test_only_post_signing_developer_id_receipts_are_allowed_to_differ(self):
        embedded = copy.deepcopy(self.info)
        embedded['signing'] = 'developer-id'
        embedded.pop('notarized')
        self.write('Contents/Resources/BUILD_INFO.json', json.dumps(embedded).encode())
        self.pack()
        self.info = {**embedded, 'notarized': True,
                     'notarization': {'app': {'status': 'Accepted'}, 'dmg': {'status': 'Accepted'}}}
        self.verify()  # Receipt validity is independently enforced by the publisher.
        self.info['source_commit'] = 'b' * 40
        with self.assertRaisesRegex(ValueError, 'provenance'):
            self.verify()

    def test_ad_hoc_provenance_has_no_ignored_receipt_fields(self):
        self.info['notarization'] = {'app': {'status': 'Accepted'}}
        with self.assertRaisesRegex(ValueError, 'provenance'):
            self.verify()


if __name__ == '__main__':
    unittest.main()
