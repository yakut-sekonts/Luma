"""Catalog-scale routing must not silently generate image/load fixtures."""
from pathlib import Path
import tempfile
import unittest

import catalog_scale_fixtures as catalog
import test_thumbnail_response_fixtures as response_tests


class CatalogScaleFixtureTests(unittest.TestCase):
    run_cli = response_tests.ThumbnailFixtureTests.run_cli

    def test_owned_profiles_are_metadata_only_and_fresh(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for case, rows in catalog.CASE_ROWS.items():
                folder = root / 'case-fixtures' / case
                hashes, profile = catalog.official_fixture(folder, owned_root=root, case_id=case)
                self.assertEqual((hashes, profile['metadata_rows'], list(folder.iterdir())), ({}, rows, []))
                self.assertEqual(profile['original_checksum_count'], 0)
                with self.assertRaises(ValueError):
                    catalog.official_fixture(folder, owned_root=root, case_id=case)
            outside = root / 'foreign'
            outside.mkdir()
            with self.assertRaises(ValueError):
                catalog.official_fixture(outside, owned_root=root, case_id='UX-P09-CAT-001')
            alias = root / 'alias'
            alias.symlink_to(root, target_is_directory=True)
            with self.assertRaises(ValueError):
                catalog.official_fixture(alias / 'case-fixtures/UX-P09-CAT-001',
                                         owned_root=alias, case_id='UX-P09-CAT-001')

    def test_explicit_full_campaign_routes_all_sizes_without_generic_photos(self):
        result = self.run_cli(list(catalog.CASE_ROWS), quality=False,
                              extra_flags=('--through', '0.9'))
        self.assertEqual((result['code'], result['base_calls'], len(result['calls'])), (0, 0, 3))
        self.assertEqual({folder.name for _, folder in result['calls']}, set(catalog.CASE_ROWS))

    def test_quality_only_still_defers_scale_work(self):
        result = self.run_cli(list(catalog.CASE_ROWS), quality=True,
                              extra_flags=('--through', '0.9'))
        self.assertEqual((result['base_calls'], len(result['calls'])), (0, 0))


if __name__ == '__main__':
    unittest.main()
