"""Prospective CRP010 fixture/backend mocks; never runs an application."""
import hashlib
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

import crop_done_fixtures as crop
import export_watermark_fixtures as watermark
import quality_policy as policy
import run_ux_suite as runner
import test_thumbnail_response_fixtures as response_tests
import thumbnail_response_fixtures as palette


class CropDoneProfileTests(unittest.TestCase):
    run_cli = response_tests.ThumbnailFixtureTests.run_cli

    def test_exact_crc_literal_stripes_sha_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            folder = root / 'case-fixtures' / crop.OFFICIAL_CASE
            hashes, manifest = crop.official_fixture(folder, owned_root=root)
            source = folder / 'Photo-0000.png'
            self.assertEqual(list(folder.iterdir()), [source])
            self.assertEqual(
                hashes,
                {str(source): hashlib.sha256(source.read_bytes()).hexdigest()},
            )
            self.assertEqual(
                (manifest['name'], manifest['public_photo_count'],
                 manifest['original_checksum_count']),
                ('crop-done-stripes-rgb1-v1', 1, 1),
            )
            self.assertFalse(manifest['large_24mp_generated'])
            data = source.read_bytes()
            self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
            position = 8
            compressed = b''
            while position < len(data):
                size = struct.unpack('>I', data[position:position + 4])[0]
                kind = data[position + 4:position + 8]
                content = data[position + 8:position + 8 + size]
                self.assertEqual(
                    zlib.crc32(kind + content),
                    struct.unpack('>I', data[position + 8 + size:position + 12 + size])[0],
                )
                if kind == b'IHDR':
                    self.assertEqual(struct.unpack('>IIBBBBB', content), (256, 192, 8, 2, 0, 0, 0))
                if kind == b'IDAT':
                    compressed += content
                position += 12 + size
            self.assertEqual(position, len(data))
            expected = b'\0' + b''.join(
                bytes([255 if x // 64 % 2 == 0 else 0]) * 3 for x in range(256)
            )
            self.assertEqual(zlib.decompress(compressed), expected * 192)
            with self.assertRaises(ValueError):
                crop.official_fixture(folder, owned_root=root)
            self.assertEqual(runner.changed_originals(hashes), [])

    def test_foreign_relative_and_alias_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            folder = root / 'case-fixtures' / crop.OFFICIAL_CASE
            for path in [root / 'foreign', Path('case-fixtures') / crop.OFFICIAL_CASE]:
                with self.subTest(path=path), self.assertRaises(ValueError):
                    crop.official_fixture(path, owned_root=root)
            folder.parent.mkdir()
            outside = root / 'outside'
            outside.mkdir()
            folder.symlink_to(outside, target_is_directory=True)
            with self.assertRaises(ValueError):
                crop.official_fixture(folder, owned_root=root)
            self.assertEqual(list(outside.iterdir()), [])
            folder.unlink()
            folder.parent.rmdir()
            folder.parent.symlink_to(outside, target_is_directory=True)
            with self.assertRaises(ValueError):
                crop.official_fixture(folder, owned_root=root)
            self.assertEqual(list(outside.iterdir()), [])

    def test_solo_three_mock_children_only_one_tiny_profile(self):
        for quality in (False, True):
            with (self.subTest(quality=quality),
                  patch.dict(os.environ, {'GITHUB_ACTIONS': ''}),
                  patch.object(runner, 'crop_done_fixture', wraps=crop.official_fixture) as factory):
                outcome = self.run_cli([crop.OFFICIAL_CASE], quality=quality, runs=3)
                self.assertEqual(
                    (outcome['code'], outcome['base_calls'], factory.call_count,
                     len(outcome['calls'])),
                    (0, 0, 1, 3),
                )
                self.assertTrue(all(
                    folder.parts[-2:] == ('case-fixtures', crop.OFFICIAL_CASE)
                    for _, folder in outcome['calls']
                ))
                summary = outcome['summary']
                profile = summary['fixture_profile']
                self.assertEqual(
                    (profile['base_original_checksum_count'],
                     profile['extra_original_checksum_count'], profile['original_checksum_count']),
                    (0, 1, 1),
                )
                self.assertEqual(summary['inventory_domain'], 'specification')
                self.assertEqual(summary['release_gate'], 'NOT_QUALIFIED')
                self.assertEqual(summary['unsupported_backend'], [])

    def test_forced_native_only_skip_without_any_factory_or_child(self):
        for identifier in (crop.OFFICIAL_CASE, 'UX-MAC-001'):
            for quality in (False, True):
                with (self.subTest(identifier=identifier, quality=quality),
                      patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}),
                      patch.object(runner, 'crop_done_fixture', wraps=crop.official_fixture) as factory):
                    outcome = self.run_cli([identifier], quality=quality, runs=3)
                    self.assertEqual(
                        (outcome['code'], outcome['base_calls'], outcome['extra_calls'],
                         factory.call_count, len(outcome['calls'])),
                        (1, 0, 0, 0, 0),
                    )
                    summary = outcome['summary']
                    self.assertEqual(summary['summary'], {'SKIPPED': 1})
                    self.assertEqual(summary['execution_status'], 'INCOMPLETE')
                    self.assertEqual(summary['release_gate'], 'NOT_QUALIFIED')
                    self.assertEqual(summary['unsupported_backend'], [identifier])
                    self.assertEqual(summary['fixture_profile']['original_checksum_count'], 0)
                    row, = summary['results']
                    self.assertEqual(row['skip_reason'], 'unsupported_backend')
                    self.assertNotIn('child_pid', row)
                    self.assertTrue(row['adapter_implemented'])
                    evidence = row['backend_requirement']
                    self.assertEqual(
                        (evidence['requested_platform'], evidence['requested_graphics_api']),
                        ('offscreen', 'software'),
                    )
                    self.assertFalse(evidence['actual_runtime_observed'])
                    self.assertTrue(evidence['child_launch_suppressed'])

    def test_mixed_software_retains_other_workflow_and_execution_incomplete(self):
        with (patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}),
              patch.object(runner, 'crop_done_fixture', wraps=crop.official_fixture) as factory):
            outcome = self.run_cli(
                [crop.OFFICIAL_CASE, 'UX-MAC-001', 'UX-SEL-002', 'UX-UND-018']
            )
        self.assertEqual(
            (outcome['code'], outcome['base_calls'], factory.call_count, len(outcome['calls'])),
            (1, 1, 0, 1),
        )
        self.assertEqual(outcome['base_options'], {'count': 10, 'include_large': False})
        summary = outcome['summary']
        self.assertEqual(summary['summary'], {'SKIPPED': 2, 'PASS': 2})
        self.assertEqual(summary['execution_status'], 'INCOMPLETE')
        self.assertEqual(summary['fixture_profile']['original_checksum_count'], 12)
        integrity = next(row for row in summary['results'] if row['id'] == 'UX-UND-018')
        self.assertEqual((integrity['status'], integrity['original_count']), ('PASS', 12))

    def test_unforced_all_profile_union71_base83_or93(self):
        identifiers = [palette.OFFICIAL_CASE, palette.AWAY_BACK_CASE, palette.RACE_CASE,
                       watermark.OFFICIAL_CASE, crop.OFFICIAL_CASE]
        for generic, total in [(None, 71), ('UX-SEL-002', 83), ('UX-IMP-009', 93)]:
            with (self.subTest(generic=generic),
                  patch.dict(os.environ, {'GITHUB_ACTIONS': ''})):
                outcome = self.run_cli(
                    identifiers + ([generic] if generic else []) + ['UX-UND-018']
                )
                self.assertEqual(outcome['code'], 0)
                profile = outcome['summary']['fixture_profile']
                self.assertEqual(
                    (profile['extra_original_checksum_count'], profile['original_checksum_count']),
                    (71, total),
                )
                self.assertEqual(len(outcome['manifest']['original_sha256']), total)
                self.assertEqual(len(profile['case_profiles']), 5)
                self.assertEqual(len({p['directory'] for p in profile['case_profiles']}), 5)
                self.assertFalse(profile['large_24mp_generated'])
                self.assertEqual(
                    outcome['base_options'],
                    None if generic is None else {
                        'count': 20 if generic == 'UX-IMP-009' else 10,
                        'include_large': False,
                    },
                )

    def test_mutation_fail_and_ineligible_zero_factory(self):
        with patch.dict(os.environ, {'GITHUB_ACTIONS': ''}):
            for mutation in ('bytes', 'remove', 'symlink'):
                with self.subTest(mutation=mutation):
                    outcome = self.run_cli(
                        [crop.OFFICIAL_CASE], mutate=mutation, mutation_case=crop.OFFICIAL_CASE
                    )
                    self.assertEqual(outcome['code'], 1)
                    self.assertEqual(outcome['summary']['original_hashes'], 'FAIL')
            settings = [
                {'native': False},
                {'override_row': {'scope': '0.6'}},
                {'extra_flags': ('--suite', 'blocking')},
                {'denied': True},
                {'extra_flags': ('--require-build-provenance',), 'provenance': 'FAIL'},
            ]
            for options in settings:
                with (self.subTest(options=options),
                      patch.object(runner, 'crop_done_fixture', wraps=crop.official_fixture) as factory):
                    outcome = self.run_cli([crop.OFFICIAL_CASE], **options)
                    self.assertNotEqual(outcome['code'], 0)
                    self.assertEqual(
                        (outcome['base_calls'], factory.call_count, len(outcome['calls'])),
                        (0, 0, 0),
                    )

    def test_mixed_native_profile_filtered_or_ineligible_has_no_hidden_legacy_bulk(self):
        profiles = (palette.OFFICIAL_CASE, crop.OFFICIAL_CASE, watermark.OFFICIAL_CASE)
        for profile in profiles:
            for quality in (False, True):
                for options in ({'extra_flags': ('--suite', 'blocking')}, {'native': False}):
                    with (self.subTest(profile=profile, quality=quality, options=options),
                          patch.dict(os.environ, {'GITHUB_ACTIONS': ''}),
                          patch.object(runner, 'crop_done_fixture', wraps=crop.official_fixture) as factory):
                        outcome = self.run_cli(
                            ['UX-MAC-001', profile], quality=quality, **options
                        )
                        self.assertNotEqual(outcome['code'], 0)
                        self.assertEqual(
                            (outcome['base_calls'], outcome['extra_calls'],
                             factory.call_count, len(outcome['calls'])),
                            (0, 0, 0, 0),
                        )
                        self.assertEqual(outcome['summary']['execution_status'], 'INCOMPLETE')
                        self.assertEqual(outcome['summary']['release_gate'], 'NOT_QUALIFIED')
                        self.assertFalse(any(
                            row['status'] == 'PASS' for row in outcome['summary']['results']
                        ))

    def test_only_exact_controlled_route_classified_not_unforced_actual_backend(self):
        environment = {'GITHUB_ACTIONS': '', 'QT_QPA_PLATFORM': 'offscreen',
                       'QT_QUICK_BACKEND': 'software'}
        with patch.dict(os.environ, environment):
            self.assertIsNone(runner.unsupported_backend_requirement(crop.OFFICIAL_CASE))
            self.assertIsNone(runner.unsupported_backend_requirement('UX-MAC-001'))
        with patch.dict(os.environ, {'GITHUB_ACTIONS': 'false'}):
            self.assertIsNotNone(runner.unsupported_backend_requirement(crop.OFFICIAL_CASE))
            self.assertIsNone(
                runner.unsupported_backend_requirement('CANDIDATE-CRP010-DONE-RESIDUE')
            )
            self.assertIsNone(runner.unsupported_backend_requirement(palette.OFFICIAL_CASE))
            outcome = self.run_cli([palette.OFFICIAL_CASE])
            self.assertEqual((outcome['code'], len(outcome['calls'])), (0, 1))
            self.assertEqual(outcome['summary']['fixture_profile']['original_checksum_count'], 32)

    def test_unsupported_never_release_qualified_or_masks_real_failure(self):
        blocking = {'id': 'A', 'scope': 'CURRENT', 'mode': 'BLOCKING'}
        crop_row = {'id': crop.OFFICIAL_CASE, 'scope': 'CURRENT', 'mode': 'REGRESSION'}
        rows = [{**blocking, 'status': 'PASS'}] * 3 + [
            {**crop_row, 'status': 'SKIPPED', 'skip_reason': 'unsupported_backend'}
        ]
        self.assertEqual(
            runner.qualify(rows, [blocking, crop_row], '0.5', True)[0], 'NOT_QUALIFIED'
        )
        rows.append({'id': 'actual', 'status': 'FAIL', 'severity_if_failed': 'P1'})
        self.assertEqual(runner.qualify(rows, [blocking, crop_row], '0.5', True)[0], 'FAIL')
        rows = rows[:3] + [{**crop_row, 'status': 'SKIPPED', 'skip_reason': 'hardware'}]
        self.assertEqual(runner.qualify(rows, [blocking, crop_row], '0.5', True)[0], 'PASS')

    def test_current_inventory_policy_and_requirement_exact(self):
        row = next(row for row in runner.INVENTORY if row['id'] == crop.OFFICIAL_CASE)
        self.assertEqual((row['mode'], row['scope'], row['severity']),
                         ('REGRESSION', 'CURRENT', 'P1'))
        self.assertIn(crop.OFFICIAL_CASE, runner.NATIVE)
        self.assertIsNone(policy.quality_skip_reason(row))
        self.assertEqual(runner.NATIVE_ONLY_CONTRACTS[crop.OFFICIAL_CASE],
                         {'platform': 'cocoa', 'graphics_api': 'metal'})
        self.assertEqual(runner.NATIVE_ONLY_CONTRACTS['UX-MAC-001']['device_pixel_ratio'], 2)

    def test_native_performance_contracts_never_execute_on_forced_offscreen(self):
        identifiers = ['UX-NAV-002', 'UX-PRV-002', 'UX-ADV-012', 'UX-PERF-006']
        with patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
            outcome = self.run_cli(identifiers, quality=False, runs=3)
            self.assertEqual(outcome['code'], 1)
            self.assertEqual(outcome['calls'], [])
            self.assertEqual(outcome['base_calls'], 0)
            self.assertEqual(outcome['summary']['summary'], {'SKIPPED': 4})
            self.assertEqual(outcome['summary']['execution_status'], 'INCOMPLETE')
            self.assertEqual(outcome['summary']['release_gate'], 'NOT_QUALIFIED')
            self.assertEqual(set(outcome['summary']['unsupported_backend']), set(identifiers))
            for row in outcome['summary']['results']:
                self.assertEqual(row['skip_reason'], 'unsupported_backend')
                self.assertNotIn('child_pid', row)
            for identifier in ('UX-FLT-002', 'UX-TRN-013'):
                self.assertIsNone(runner.unsupported_backend_requirement(identifier))
        with patch.dict(os.environ, {'GITHUB_ACTIONS': ''}):
            for identifier in identifiers:
                self.assertIsNone(runner.unsupported_backend_requirement(identifier))
                self.assertEqual(runner.NATIVE_ONLY_CONTRACTS[identifier], {'platform': 'cocoa'})


if __name__ == '__main__':
    unittest.main()
