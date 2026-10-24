"""Verify real TIFF/EXIF values independently and isolated runner routing."""
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

import metadata_display_fixtures as metadata
import test_thumbnail_response_fixtures as response_tests


class MetadataDisplayTests(unittest.TestCase):
    run_cli = response_tests.ThumbnailFixtureTests.run_cli

    def test_exif_ifd_values_and_owned_path_guards(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            folder = root / 'case-fixtures' / metadata.OFFICIAL_CASE
            hashes, profile = metadata.official_fixture(folder, owned_root=root)
            source = folder / 'stress/Lens.DNG'
            encoded = source.read_bytes()
            self.assertEqual(hashes, {str(source): hashlib.sha256(encoded).hexdigest()})
            self.assertEqual(encoded[:4], b'II*\0')

            def directory(offset):
                count, = struct.unpack_from('<H', encoded, offset)
                return {struct.unpack_from('<H', encoded, offset + 2 + 12 * i)[0]:
                        encoded[offset + 2 + 12 * i:offset + 14 + 12 * i]
                        for i in range(count)}

            main = directory(struct.unpack_from('<I', encoded, 4)[0])
            exif = directory(struct.unpack_from('<I', main[34665], 8)[0])
            self.assertEqual(set(exif), {33434, 33437, 34855, 37386, 42036})
            self.assertFalse(set(exif).intersection(main))
            self.assertEqual(struct.unpack_from('<HHIH', exif[34855]), (34855, 3, 1, 100))
            for tag, expected in {33434: (1, 125), 33437: (560, 100), 37386: (1800, 100)}.items():
                offset, = struct.unpack_from('<I', exif[tag], 8)
                self.assertEqual(struct.unpack_from('<II', encoded, offset), expected)
            _, kind, count, offset = struct.unpack('<HHII', exif[42036])
            self.assertEqual(kind, 2)
            self.assertEqual(encoded[offset:offset + count], b'AF-S DX Nikkor 18-140mm f/3.5-5.6G ED VR\0')
            self.assertEqual((profile['public_photo_count'], profile['original_checksum_count']), (0, 1))
            self.assertEqual(list(folder.rglob('*.DNG')), [source])
            self.assertFalse(list(folder.rglob('*.png')))
            with self.assertRaises(ValueError):
                metadata.official_fixture(folder, owned_root=root)
            with self.assertRaises(ValueError):
                metadata.official_fixture(root / 'foreign', owned_root=root)
            self.assertEqual(hashes[str(source)], hashlib.sha256(source.read_bytes()).hexdigest())

    def test_single_and_mixed_runner_preserve_base_fixtures(self):
        for quality in (True, False):
            with self.subTest(quality=quality):
                solo = self.run_cli([metadata.OFFICIAL_CASE], quality=quality, runs=3, extra_flags=('--through', '0.9'))
                self.assertEqual((solo['code'], solo['base_calls'], len(solo['calls'])), (0, 0, 3))
                self.assertTrue(all(folder.parts[-2:] == ('case-fixtures', metadata.OFFICIAL_CASE)
                                    for _, folder in solo['calls']))
        mixed = self.run_cli([metadata.OFFICIAL_CASE, 'UX-P09-META-006'], quality=True, extra_flags=('--through', '0.9'))
        self.assertEqual(mixed['code'], 0)
        self.assertEqual(mixed['base_calls'], 1)
        self.assertEqual(len(mixed['calls']), 2)


if __name__ == '__main__':
    unittest.main()
