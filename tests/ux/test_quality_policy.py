"""Execution-policy and tiny supplemental fixtures; no native child or UI."""
import argparse
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_card_flag_regression as card_flags
import run_thumbnail_response_regression as thumbnail_response
import quality_policy as policy
import run_ux_suite as runner


class QualityPolicyTests(unittest.TestCase):
    def test_every_native_adapter_has_an_explicit_source_review(self):
        by_id = {row['id']: row for row in runner.INVENTORY}
        missing = sorted(identifier for identifier in runner.NATIVE
                         if policy.quality_skip_reason(by_id[identifier])
                         and policy.quality_skip_reason(by_id[identifier])[0] == 'policy_unreviewed')
        self.assertEqual(missing, [], 'Review new adapters before quality-only CI can execute them')
        self.assertFalse(policy.QUALITY_ALLOWLIST.intersection(policy.DEFERRED_LOAD))
        self.assertIn('UX-MAC-008', policy.QUALITY_ALLOWLIST)

    def test_actual_workload_overrides_inventory_mode_without_relabelling_it(self):
        for identifier, mode in [('UX-APP-001', 'BLOCKING'), ('UX-LIB-010', 'BLOCKING'),
                                 ('UX-IMP-006', 'REGRESSION'), ('UX-BAT-013', 'REGRESSION'),
                                 ('UX-LIB-009', 'REGRESSION'),
                                 ('UX-PRV-013', 'BLOCKING')]:
            row = {'id': identifier, 'mode': mode}
            with self.subTest(identifier=identifier):
                self.assertEqual(policy.quality_skip_reason(row)[0], 'deferred_load')
                self.assertEqual(row['mode'], mode)
        for mode in ('STRESS', 'SOAK', 'PERF_LOCAL', 'CHAOS'):
            self.assertEqual(policy.quality_skip_reason({'id': 'future', 'mode': mode})[0],
                             'deferred_load')
        self.assertIsNone(policy.quality_skip_reason({'id': 'UX-SEL-002', 'mode': 'BLOCKING'}))
        self.assertEqual(policy.quality_skip_reason({'id': 'future', 'mode': 'BLOCKING'})[0],
                         'policy_unreviewed')

    def test_small_fixture_plan_and_full_release_accounting_are_separate(self):
        self.assertEqual(policy.quality_fixture_count([]), 0)
        self.assertEqual(policy.quality_fixture_count([{'id': 'UX-SEL-002'}]), 10)
        self.assertEqual(policy.quality_fixture_count([{'id': 'UX-SEL-002'},
                                                       {'id': 'UX-IMP-009'}]), 20)
        args = argparse.Namespace(quality_only=True, release_qualification=True,
                                  case=[], suite='all', phase=None)
        self.assertFalse(runner.qualification_requested(args))

    def run_cli(self, inventory, flags=(), native=None, manifest=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / 'build'
            build.mkdir()
            snapshot = {'source_tree_sha256': 'source', 'test_executable_sha256': 'binary'}
            def execute(row, directory, arguments, fixtures):
                directory.mkdir(parents=True)
                return {**row, 'status': 'PASS', 'child_pid': 123, 'artifacts': str(directory)}
            stderr = io.StringIO()
            argv = ['run_ux_suite.py', '--build', str(build), '--quality-only',
                    '--suite', 'blocking', *flags]
            with (patch.object(runner, 'ROOT', root),
                  patch.object(runner, 'source_phase', return_value='0.5'),
                  patch.object(runner, 'INVENTORY', inventory),
                  patch.object(runner, 'NATIVE', native if native is not None else {row['id'] for row in inventory}),
                  patch.object(runner, 'GLOBAL', set()),
                  patch.object(runner, 'MANIFEST', manifest if manifest is not None else {'completed_phases': ['0.5']}),
                  patch.object(runner, 'fingerprint', return_value=snapshot),
                  patch.object(runner, 'build_provenance', return_value={'status': 'PASS'}),
                  patch.object(runner, 'fixtures', return_value={}) as fixtures,
                  patch.object(runner, 'execute_scenario', side_effect=execute) as children,
                  patch.object(runner, 'write_reports') as reports,
                  patch.object(runner.fcntl, 'flock'),
                  patch.object(runner.tempfile, 'gettempdir', return_value=str(root)),
                  patch('sys.argv', argv), patch('sys.stderr', stderr), patch('builtins.print')):
                try:
                    code = runner.main()
                except SystemExit as error:
                    code = error.code
                return {'code': code, 'children': children.call_count,
                        'fixtures': fixtures.call_count,
                        'fixture_options': fixtures.call_args.kwargs if fixtures.called else None,
                        'summary': reports.call_args.args[0] if reports.called else None,
                        'stderr': stderr.getvalue()}

    @staticmethod
    def row(identifier):
        return {'id': identifier, 'scope': 'CURRENT', 'mode': 'BLOCKING',
                'severity': 'P1', 'scenario': 'Policy-only mocked adapter'}

    def test_only_named_source_implemented_target_enters_qualification(self):
        self.assertTrue(runner.in_scope('TARGET', '0.6', 'UX-PRV-012'))
        self.assertFalse(runner.in_scope('TARGET', '0.5', 'UX-PRV-012'))
        self.assertFalse(runner.in_scope('TARGET', '1.0', 'UX-PRV-011'))
        row = next(row for row in runner.INVENTORY if row['id'] == 'UX-PRV-012')
        self.assertEqual(row['scope'], 'TARGET')
        self.assertTrue(runner.phase_selected(row, '0.6'))
        self.assertFalse(runner.phase_selected(row, '0.5'))
        result = self.run_cli([row], flags=('--suite', 'regression', '--through', '0.6', '--case', row['id']), manifest=runner.MANIFEST)
        self.assertEqual((result['code'], result['children'], result['fixtures']), (0, 1, 1))

    def test_partial_release_workflows_do_not_admit_the_whole_phase(self):
        self.assertNotIn('1.0', runner.MANIFEST['completed_phases'])
        for identifier in ('UX-P10-DAY-007', 'UX-P10-DAY-010', 'UX-P10-DAY-013'):
            self.assertTrue(runner.capability_implemented('1.0', identifier))
            self.assertFalse(runner.in_scope('1.0', '0.9', identifier))
            row = next(row for row in runner.INVENTORY if row['id'] == identifier)
            self.assertEqual((row['scope'], row['mode']), ('1.0', 'BLOCKING'))
            result = self.run_cli([row], flags=('--through', '1.0', '--case', identifier),
                                  manifest=runner.MANIFEST)
            self.assertEqual((result['code'], result['children'], result['fixtures']), (0, 1, 1))
        self.assertFalse(runner.capability_implemented('1.0', 'UX-P10-DAY-008'))
        self.assertEqual(result['summary']['release_gate'], 'NOT_QUALIFIED')

    def test_quality_ci_runs_safe_case_and_retains_deferred_case_as_skipped(self):
        result = self.run_cli([self.row('UX-APP-002'), self.row('UX-APP-001')])
        self.assertEqual(result['code'], 0)
        self.assertEqual(result['children'], 1)
        self.assertEqual(result['fixture_options'], {'count': 10, 'include_large': False})
        summary = result['summary']
        self.assertEqual(summary['release_gate'], 'NOT_QUALIFIED')
        self.assertEqual(summary['deferred_load_implemented'], ['UX-APP-001'])
        deferred = next(row for row in summary['results'] if row['id'] == 'UX-APP-001')
        self.assertEqual((deferred['status'], deferred['skip_reason'], deferred['mode']),
                         ('SKIPPED', 'deferred_load', 'BLOCKING'))

    def test_deferred_only_and_unknown_adapter_never_generate_images_or_spawn_children(self):
        for identifier, expected_skip in [('UX-APP-001', 'deferred_load'),
                                          ('UX-NEW-999', 'policy_unreviewed')]:
            with self.subTest(identifier=identifier):
                result = self.run_cli([self.row(identifier)])
                self.assertEqual(result['code'], 1)
                self.assertEqual((result['children'], result['fixtures']), (0, 0))
                self.assertEqual(result['summary']['results'][0]['skip_reason'], expected_skip)

    def test_auto_completion_regression_keeps_bounded_fixture_and_separate_ci_selection(self):
        row = next(row for row in runner.INVENTORY if row['id'] == 'UX-RACE-005')
        self.assertEqual((row['scope'], row['mode'], row['severity']),
                         ('CURRENT', 'REGRESSION', 'P1'))
        self.assertIn(row['id'], runner.NATIVE)
        self.assertIsNone(policy.quality_skip_reason(row))
        self.assertEqual(policy.quality_fixture_count([row]), 1)
        self.assertEqual(policy.quality_fixture_count([row, {'id': 'UX-SEL-002'}]), 10)
        self.assertEqual(policy.quality_fixture_count([row, {'id': 'UX-IMP-009'}]), 20)
        selected = argparse.Namespace(case=[], phase=None, suite='blocking')
        self.assertFalse(runner.row_selected(row, selected))
        result = self.run_cli([row], flags=('--suite', 'regression', '--case', row['id']))
        self.assertEqual(result['code'], 0)
        self.assertEqual((result['children'], result['fixtures']), (1, 1))
        self.assertEqual(result['fixture_options'], {'count': 1, 'include_large': False})
        self.assertEqual(result['summary']['release_gate'], 'NOT_QUALIFIED')

    def test_supplemental_card_and_thumbnail_profiles_cannot_expand_into_load_fixtures(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / 'card'
            hashes = card_flags.two_png_fixture(folder)
            self.assertEqual(sorted(path.name for path in folder.iterdir()),
                             ['Photo-0000.png', 'Photo-0001.png'])
            self.assertEqual(len(hashes), 2)
            self.assertEqual(card_flags.two_png_fixture(folder), hashes)
            for count in (8, 12, 32):
                with self.subTest(count=count):
                    fixture = Path(temporary) / ('thumbnail-' + str(count))
                    def inspect(**kwargs):
                        self.assertEqual(kwargs['domain'], 'supplemental')
                        self.assertEqual(kwargs['executable_ids'], {thumbnail_response.IDENTIFIER})
                        self.assertEqual(len(kwargs['inventory']), 1)
                        hashes = kwargs['fixture_factory'](fixture)
                        self.assertEqual(len(hashes), count)
                        self.assertEqual(len(list(fixture.iterdir())), count)
                        colors = set()
                        for source in fixture.iterdir():
                            data = source.read_bytes()
                            self.assertEqual(source.suffix, '.png')
                            self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
                            self.assertEqual(data[16:24], b'\0\0\x01\0\0\0\0\xc0')
                            self.assertEqual(data[24:26], bytes((8, 2)))  # Opaque RGB.
                            colors.add(data)
                        self.assertEqual(len(colors), count)
                        with self.assertRaises(ValueError):
                            kwargs['fixture_factory'](fixture)
                        return 0
                    with (patch('sys.argv', ['wrapper', '--fixture-count', str(count)]),
                          patch.object(thumbnail_response.runner, 'main', side_effect=inspect)):
                        self.assertEqual(thumbnail_response.main(), 0)

    def test_thumbnail_profile_rejects_bulk_or_duplicate_counts_before_runner_setup(self):
        for options in (['--fixture-count', '1000'],
                        ['--fixture-count', '8', '--fixture-count', '32']):
            with self.subTest(options=options):
                with (patch('sys.argv', ['wrapper', *options]),
                      patch('sys.stderr', io.StringIO()),
                      patch.object(thumbnail_response.runner, 'main') as main):
                    with self.assertRaises(SystemExit) as error:
                        thumbnail_response.main()
                    self.assertEqual(error.exception.code, 2)
                    main.assert_not_called()

    def test_missing_functional_adapter_keeps_unadapted_accounting(self):
        result = self.run_cli([self.row('UX-APP-002'), self.row('UX-KEY-015')],
                              native={'UX-APP-002'})
        self.assertEqual(result['code'], 1)
        missing = next(row for row in result['summary']['results'] if row['id'] == 'UX-KEY-015')
        self.assertEqual(missing['skip_reason'], 'unadapted')
        self.assertEqual(result['summary']['unadapted_applicable'], ['UX-KEY-015'])
        self.assertEqual(result['summary']['policy_unreviewed'], [])

    def test_full_qualification_and_load_flags_are_rejected_before_setup(self):
        for flags in (['--release-qualification'], ['--actions', '10000'],
                      ['--suite', 'stress'], ['--suite', 'perf'], ['--suite', 'chaos']):
            with self.subTest(flags=flags):
                result = self.run_cli([self.row('UX-APP-002')], flags)
                self.assertEqual(result['code'], 2)
                self.assertEqual((result['children'], result['fixtures']), (0, 0))
                self.assertIsNone(result['summary'])


if __name__ == '__main__':
    unittest.main()
