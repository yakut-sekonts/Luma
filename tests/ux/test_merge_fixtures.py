"""Independent EXIF/overlap checks and isolated runner routing for merge sources."""
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

import merge_fixtures as merge
import test_thumbnail_response_fixtures as response_tests


class MergeFixtureTests(unittest.TestCase):
    run_cli = response_tests.ThumbnailFixtureTests.run_cli

    def test_bracket_exif_and_original_guards(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            folder = root / 'case-fixtures/UX-P08-HDR-001'
            hashes, profile = merge.official_fixture(folder, owned_root=root, case_id=folder.name)
            self.assertEqual(profile['original_checksum_count'], 3)
            for source, expected in zip(sorted(folder.iterdir()), [(1, 500), (1, 125), (4, 125)]):
                data = source.read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), hashes[str(source)])

                def directory(offset):
                    return {struct.unpack_from('<H', data, offset + 2 + 12 * i)[0]: offset + 2 + 12 * i
                            for i in range(struct.unpack_from('<H', data, offset)[0])}

                main = directory(struct.unpack_from('<I', data, 4)[0])
                exif = directory(struct.unpack_from('<I', data, main[34665] + 8)[0])
                self.assertEqual(struct.unpack_from('<II', data,
                                 struct.unpack_from('<I', data, exif[33434] + 8)[0]), expected)
                self.assertEqual(struct.unpack_from('<H', data, exif[34855] + 8)[0], 100)
            with self.assertRaises(ValueError):
                merge.official_fixture(folder, owned_root=root, case_id=folder.name)
            with self.assertRaises(ValueError):
                merge.official_fixture(root / 'foreign', owned_root=root, case_id=folder.name)
            self.assertEqual(hashes, {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                      for p in folder.iterdir()})

    def test_panorama_crops_have_exact_known_overlap(self):
        def pixels(path):
            data = path.read_bytes()
            offset, compressed = 8, bytearray()
            while offset < len(data):
                size, = struct.unpack_from('>I', data, offset)
                if data[offset + 4:offset + 8] == b'IDAT':
                    compressed.extend(data[offset + 8:offset + 8 + size])
                offset += size + 12
            raw = zlib.decompress(compressed)
            return [raw[y * 769 + 1:(y + 1) * 769] for y in range(192)]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for case, dx, dy in [('UX-P08-PAN-001', 64, 0), ('UX-P08-PAN-002', 0, 48)]:
                folder = root / 'case-fixtures' / case
                merge.official_fixture(folder, owned_root=root, case_id=case)
                a, b = [pixels(folder / name) for name in ('Frame01.png', 'Frame02.png')]
                for y in range(192 - dy):
                    self.assertEqual(a[y + dy][dx * 3:], b[y][:(256 - dx) * 3])

    def test_runner_routes_solo_and_mixed_profiles(self):
        for quality in (True, False):
            outcome = self.run_cli(['UX-P08-HDR-001'], quality=quality, runs=3,
                                   extra_flags=('--through', '0.9'))
            self.assertEqual((outcome['code'], outcome['base_calls'], len(outcome['calls'])), (0, 0, 3))
            self.assertTrue(all(folder.name == 'UX-P08-HDR-001' for _, folder in outcome['calls']))
        mixed = self.run_cli(['UX-P08-PAN-002', 'UX-P09-META-006'], quality=True,
                             extra_flags=('--through', '0.9'))
        self.assertEqual((mixed['code'], mixed['base_calls'], len(mixed['calls'])), (0, 1, 2))

    def test_update_guard_supplies_bounded_images_and_real_hdr_brackets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            folder = root / 'case-fixtures/UX-P10-UPG-012'
            hashes, profile = merge.official_fixture(folder, owned_root=root, case_id=folder.name)
            self.assertEqual((len(hashes), profile['public_photo_count']), (35, 32))
            self.assertEqual(profile['workflow_variants'],
                             ['save', 'import', 'export', 'batch', 'merge', 'merge-cancel'])
            control = root / 'case-fixtures/UX-P08-HDR-001'
            controls, _ = merge.official_fixture(control, owned_root=root, case_id=control.name)
            for path, checksum in controls.items():
                self.assertEqual(hashes[str(folder / Path(path).name)], checksum)
        outcome = self.run_cli(['UX-P10-UPG-012'], quality=True,
                               extra_flags=('--through', '1.0'))
        self.assertEqual((outcome['code'], outcome['base_calls'], len(outcome['calls'])), (0, 0, 1))

    def test_offline_daily_uses_real_owned_raws_without_network_fixtures(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            folder = root / 'case-fixtures/UX-P10-DAY-012'
            hashes, _ = merge.official_fixture(folder, owned_root=root, case_id=folder.name)
            control = root / 'case-fixtures/UX-P08-HDR-001'
            controls, _ = merge.official_fixture(control, owned_root=root, case_id=control.name)
            self.assertEqual(len(hashes), 3)
            for path, checksum in controls.items():
                self.assertEqual(hashes[str(folder / Path(path).name)], checksum)
        outcome = self.run_cli(['UX-P10-DAY-012'], quality=True,
                               extra_flags=('--through', '1.0'))
        self.assertEqual((outcome['code'], outcome['base_calls'], len(outcome['calls'])), (0, 0, 1))

    def test_daily_merge_supplies_both_previously_verified_source_sets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            daily = root / 'case-fixtures/UX-P10-DAY-011'
            hashes, profile = merge.official_fixture(daily, owned_root=root, case_id=daily.name)
            self.assertEqual((len(hashes), profile['original_checksum_count']), (5, 5))
            self.assertEqual(profile['workflow_variants'], ['hdr', 'panorama'])
            for identifier in ('UX-P08-HDR-001', 'UX-P08-PAN-001'):
                folder = root / 'case-fixtures' / identifier
                controls, _ = merge.official_fixture(folder, owned_root=root, case_id=identifier)
                for path, checksum in controls.items():
                    self.assertEqual(hashes[str(daily / Path(path).name)], checksum)
        outcome = self.run_cli(['UX-P10-DAY-011'], quality=True,
                               extra_flags=('--through', '1.0'))
        self.assertEqual((outcome['code'], outcome['base_calls'], len(outcome['calls'])), (0, 0, 1))


if __name__ == '__main__':
    unittest.main()
