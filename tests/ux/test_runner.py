"""Test release-gate accounting and process supervision, not application UX."""
import argparse
import importlib.util
import io
import json
import hashlib
import run_keyboard_regression as keyboard_runner
import run_mask_handle_regression as mask_runner
from pathlib import Path
import signal
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('ux_runner', Path(__file__).with_name('run_ux_suite.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class GateTests(unittest.TestCase):
    inventory = [{'id': 'A', 'mode': 'BLOCKING', 'scope': 'CURRENT'},
                 {'id': 'B', 'mode': 'BLOCKING', 'scope': '0.6'}]

    def test_missing_failed_and_incomplete_repetitions_block(self):
        for statuses in ([], ['PASS'], ['PASS', 'PASS'], ['FAIL', 'PASS', 'PASS'], ['NOT_IMPLEMENTED'] * 3,
                         ['SKIPPED'] * 3, ['FLAKY'] * 3):
            with self.subTest(statuses=statuses):
                rows = [{'id': 'A', 'status': status} for status in statuses]
                self.assertEqual(runner.qualify(rows, self.inventory, '0.5', True)[0], 'FAIL')

    def test_selection_does_not_claim_release_qualification(self):
        rows = [{'id': 'A', 'status': 'PASS'}] * 3
        self.assertEqual(runner.qualify(rows, self.inventory, '0.5', False)[0], 'NOT_QUALIFIED')
        self.assertEqual(runner.qualify(rows, self.inventory, '0.5', True)[0], 'PASS')
        self.assertEqual(runner.qualify(rows, self.inventory, '0.6', True)[0], 'FAIL')

    def test_subset_modes_never_claim_full_release_qualification(self):
        args = argparse.Namespace(release_qualification=True, case=[], suite='all', phase=None)
        self.assertTrue(runner.qualification_requested(args))
        for field, value in [('case', ['UX-APP-001']), ('suite', 'blocking'),
                             ('suite', 'regression'), ('phase', '0.5')]:
            with self.subTest(field=field, value=value):
                subset = argparse.Namespace(**{**vars(args), field: value})
                self.assertFalse(runner.qualification_requested(subset))

    def test_inventory_has_unique_ids(self):
        identifiers = [row['id'] for row in runner.INVENTORY]
        self.assertEqual(len(identifiers), len(set(identifiers)))
        self.assertEqual(len(identifiers), 770)

    def test_ci_output_paths_follow_build_and_preserve_explicit_overrides(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            build = root / 'build'
            build.mkdir()
            snapshot = {'source_tree_sha256': 'fixture', 'test_executable_sha256': 'fixture'}
            for explicit in (False, True):
                with self.subTest(explicit=explicit):
                    artifacts = root / 'custom-evidence' if explicit else build / 'ux-artifacts'
                    report = root / 'custom-report' if explicit else build / 'ux-report'
                    argv = ['run_ux_suite.py', '--build', str(build), '--suite', 'blocking']
                    if explicit:
                        argv += ['--artifacts', str(artifacts), '--report', str(report)]
                    opened = []
                    original_open = Path.open
                    def record_open(path, *args, **kwargs):
                        file = original_open(path, *args, **kwargs)
                        opened.append(file)
                        return file
                    # CLI routing test: no native process, fixture generation or screen ownership.
                    with (patch.object(runner, 'ROOT', root),
                          patch.object(runner, 'source_phase', return_value='0.5'),
                          patch.object(runner, 'INVENTORY', []),
                          patch.object(runner, 'fixtures', return_value={}) as fixtures,
                          patch.object(runner, 'fingerprint', return_value=snapshot),
                          patch.object(runner, 'build_provenance', return_value={'status': 'PASS'}),
                          patch.object(runner, 'write_reports') as reports,
                          patch.object(runner.fcntl, 'flock'),
                          patch.object(Path, 'open', record_open),
                          patch('sys.argv', argv), patch('builtins.print')):
                        self.assertEqual(runner.main(), 1)  # An empty suite never qualifies.
                        self.assertEqual(reports.call_args.args[1], report)
                        self.assertEqual(fixtures.call_args.args[0], artifacts / 'public-fixtures')
                        self.assertTrue(opened)
                        self.assertTrue(all(file.closed for file in opened),
                                        'Runner retained its screen lock after returning')

    def test_source_phase_tracks_the_build_and_rejects_unknown_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            configuration = root / 'CMakeLists.txt'
            configuration.write_text('project(Luma VERSION 0.10.7 LANGUAGES C CXX OBJCXX)')
            with patch.object(runner, 'ROOT', root):
                self.assertEqual(runner.source_phase(), '0.10')
                configuration.write_text('project(Unknown LANGUAGES CXX)')
                with self.assertRaises(ValueError):
                    runner.source_phase()

    def test_phase_order_is_numeric_and_target_is_never_current(self):
        self.assertTrue(runner.in_scope('0.6', '0.10'))
        self.assertFalse(runner.in_scope('0.10', '0.6'))
        self.assertFalse(runner.in_scope('TARGET', '1.0'))
        self.assertTrue(runner.in_scope('ALL', '0.5'))

    def test_nonblocking_polish_does_not_replace_the_severity_gate(self):
        rows = [{'id': 'A', 'status': 'PASS'}] * 3
        rows.append({'id': 'polish', 'status': 'FAIL', 'severity_if_failed': 'P2'})
        self.assertEqual(runner.qualify(rows, self.inventory, '0.5', True)[0], 'PASS')
        rows[-1]['severity_if_failed'] = 'P1'
        self.assertEqual(runner.qualify(rows, self.inventory, '0.5', True)[0], 'FAIL')

    def test_unadapted_current_regression_cannot_qualify_a_full_run(self):
        rows = [{'id': 'A', 'status': 'PASS'}] * 3
        rows.append({'id': 'regression', 'status': 'SKIPPED', 'scope': 'CURRENT',
                     'severity': 'P2', 'skip_reason': 'unadapted'})
        self.assertEqual(runner.qualify(rows, self.inventory, '0.5', True)[0], 'FAIL')
        rows[-1]['skip_reason'] = 'hardware'
        self.assertEqual(runner.qualify(rows, self.inventory, '0.5', True)[0], 'PASS')

    def test_omitted_current_nonblocking_result_cannot_qualify(self):
        rows = [{'id': 'A', 'status': 'PASS'}] * 3
        inventory = self.inventory + [{'id': 'regression', 'scope': 'CURRENT', 'mode': 'REGRESSION'}]
        self.assertEqual(runner.qualify(rows, inventory, '0.5', True)[0], 'FAIL')

    def test_build_provenance_requires_both_matching_source_and_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = {'source_tree_sha256': 'source', 'test_executable_sha256': 'binary'}
            self.assertEqual(runner.build_provenance(root, snapshot)['status'], 'UNVERIFIED')
            path = root / 'ux-build-provenance.json'
            path.write_text(json.dumps({'schema': 1, **snapshot}))
            self.assertEqual(runner.build_provenance(root, snapshot)['status'], 'PASS')
            for field in snapshot:
                changed = {**snapshot, field: 'changed'}
                self.assertEqual(runner.build_provenance(root, changed)['status'], 'UNVERIFIED')
            path.write_text('[]')
            self.assertEqual(runner.build_provenance(root, snapshot)['status'], 'UNVERIFIED')
            path.write_text('corrupt')
            self.assertEqual(runner.build_provenance(root, snapshot)['status'], 'UNVERIFIED')

    def _mocked_attestation_cli(self, flags=(), record='valid', changed_at_end=None,
                                changed_after_setup=None):
        """Exercise CLI gates with real provenance JSON, without fixtures or child processes."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            build = root / 'build'
            build.mkdir()
            start = {'source_tree_sha256': 'source', 'test_executable_sha256': 'binary',
                     'commit': 'fixture', 'worktree_dirty': False}
            if record != 'missing':
                recorded = {'schema': 1, **start, 'source_commit': 'fixture'}
                if record == 'source_mismatch':
                    recorded['source_tree_sha256'] = 'other-source'
                elif record == 'binary_mismatch':
                    recorded['test_executable_sha256'] = 'other-binary'
                (build / 'ux-build-provenance.json').write_text(json.dumps(recorded))
            prepared = dict(start)
            if changed_after_setup:
                prepared[changed_after_setup] = 'changed-during-fixture-setup'
            end = dict(prepared)
            if changed_at_end:
                end[changed_at_end] = 'changed-during-suite'
            strict = '--require-build-provenance' in flags or '--release-qualification' in flags
            snapshots = [start, prepared, end] if strict else [prepared, end]
            inventory = [{'id': 'A', 'mode': 'BLOCKING', 'scope': 'CURRENT',
                          'severity': 'P1', 'scenario': 'Mocked CLI gate'}]
            def fake_execute(row, destination, arguments, fixture_folder):
                destination.mkdir(parents=True)
                return {**row, 'status': 'PASS', 'child_pid': 123, 'artifacts': str(destination)}
            stderr = io.StringIO()
            argv = ['run_ux_suite.py', '--build', str(build), '--runs', '3', *flags]
            with (patch.object(runner, 'ROOT', root),
                  patch.object(runner, 'source_phase', return_value='0.5'),
                  patch.object(runner, 'INVENTORY', inventory),
                  patch.object(runner, 'NATIVE', {'A'}),
                  patch.object(runner, 'GLOBAL', set()),
                  patch.object(runner, 'MANIFEST', {'completed_phases': ['0.5']}),
                  patch.object(runner, 'fixtures', return_value={}) as fixtures,
                  patch.object(runner, 'fingerprint', side_effect=snapshots),
                  patch.object(runner, 'execute_scenario', side_effect=fake_execute) as execute,
                  patch.object(runner, 'write_reports') as reports,
                  patch.object(runner.fcntl, 'flock'),
                  patch.object(runner.tempfile, 'gettempdir', return_value=str(root)),
                  patch('sys.argv', argv), patch('sys.stderr', stderr), patch('builtins.print')):
                try:
                    code = runner.main()
                except SystemExit as error:
                    code = error.code
                return {'code': code, 'fixtures': fixtures.call_count, 'children': execute.call_count,
                        'summary': reports.call_args.args[0] if reports.called else None,
                        'stderr': stderr.getvalue()}

    def test_cli_matching_provenance_accepts_adhoc_explicit_and_release_modes(self):
        for flags in ((), ('--require-build-provenance',), ('--release-qualification',)):
            with self.subTest(flags=flags):
                result = self._mocked_attestation_cli(flags)
                self.assertEqual(result['code'], 0)
                self.assertEqual(result['fixtures'], 1)
                self.assertEqual(result['children'], 3)
                self.assertEqual(result['summary']['build_provenance']['status'], 'PASS')
                self.assertEqual(result['summary']['execution_status'], 'PASS')
                self.assertEqual(result['summary']['source_consistency'], 'PASS')
                expected = 'PASS' if '--release-qualification' in flags else 'NOT_QUALIFIED'
                self.assertEqual(result['summary']['release_gate'], expected)

    def test_cli_required_provenance_rejects_missing_and_mismatched_before_setup(self):
        for flags in (('--require-build-provenance',), ('--release-qualification',)):
            for record in ('missing', 'source_mismatch', 'binary_mismatch'):
                with self.subTest(flags=flags, record=record):
                    result = self._mocked_attestation_cli(flags, record)
                    self.assertEqual(result['code'], 2)
                    self.assertEqual(result['fixtures'], 0)
                    self.assertEqual(result['children'], 0)
                    self.assertIsNone(result['summary'])
                    self.assertIn('Build provenance preflight failed', result['stderr'])
                    self.assertIn('ux-build-provenance.json', result['stderr'])
                    self.assertIn('tests/ux/build_ux.py --build', result['stderr'])
                    if record != 'missing':
                        self.assertIn('does not match current sources and executable', result['stderr'])

    def test_cli_adhoc_unverified_build_preserves_functional_results(self):
        for record in ('missing', 'source_mismatch', 'binary_mismatch'):
            with self.subTest(record=record):
                result = self._mocked_attestation_cli(record=record)
                self.assertEqual(result['code'], 0)
                self.assertEqual(result['children'], 3)
                self.assertEqual(result['summary']['summary'], {'PASS': 3})
                self.assertEqual(result['summary']['build_provenance']['status'], 'UNVERIFIED')
                self.assertEqual(result['summary']['release_gate'], 'NOT_QUALIFIED')

    def test_cli_required_provenance_rechecks_sources_and_binary_after_setup(self):
        for flags in (('--require-build-provenance',), ('--release-qualification',)):
            for field in ('source_tree_sha256', 'test_executable_sha256', 'native_bundle_info'):
                with self.subTest(flags=flags, field=field):
                    result = self._mocked_attestation_cli(flags, changed_after_setup=field)
                    self.assertEqual(result['code'], 2)
                    self.assertEqual(result['fixtures'], 1)
                    self.assertEqual(result['children'], 0)
                    self.assertIsNone(result['summary'])
                    self.assertIn('Build provenance changed during fixture setup', result['stderr'])

    def test_cli_source_or_binary_drift_prevents_green_even_when_children_pass(self):
        for flags in ((), ('--require-build-provenance',), ('--release-qualification',)):
            for field in ('source_tree_sha256', 'test_executable_sha256', 'native_bundle_info'):
                with self.subTest(flags=flags, field=field):
                    result = self._mocked_attestation_cli(flags, changed_at_end=field)
                    self.assertEqual(result['code'], 1)
                    self.assertEqual(result['children'], 3)
                    self.assertEqual(result['summary']['summary'], {'PASS': 3})
                    self.assertEqual(result['summary']['source_consistency'], 'CHANGED_DURING_SUITE')
                    self.assertEqual(result['summary']['execution_status'], 'INCOMPLETE')
                    expected = 'FAIL' if '--release-qualification' in flags else 'NOT_QUALIFIED'
                    self.assertEqual(result['summary']['release_gate'], expected)

    def test_phase_selection_keeps_prior_blocking_regressions(self):
        for scope in ('ALL', 'CURRENT', '0.5', '0.6'):
            self.assertTrue(runner.phase_selected({'scope': scope, 'mode': 'BLOCKING'}, '0.6'))
        self.assertFalse(runner.phase_selected({'scope': '0.7', 'mode': 'BLOCKING'}, '0.6'))
        self.assertFalse(runner.phase_selected({'scope': '0.5', 'mode': 'REGRESSION'}, '0.6'))
        self.assertTrue(runner.phase_selected({'scope': '0.6', 'mode': 'REGRESSION'}, '0.6'))

    def test_scenario_deadlines_preserve_explicit_override(self):
        defaults = argparse.Namespace(timeout=None)
        self.assertEqual(runner.scenario_timeout('UX-NAV-001', defaults), 180)
        self.assertEqual(runner.scenario_timeout('UX-MAC-008', defaults), 300)
        self.assertEqual(runner.scenario_timeout('UX-UND-016', defaults), 600)
        self.assertEqual(runner.scenario_timeout('UX-ADV-013', defaults), 600)
        self.assertEqual(runner.scenario_timeout('UX-PRS-019', defaults), 900)
        self.assertEqual(runner.scenario_timeout('UX-IMP-019', defaults), 600)
        self.assertEqual(runner.scenario_timeout('UX-ADV-012', defaults), 600)
        self.assertEqual(runner.scenario_timeout('UX-NAV-002', defaults), 300)
        for value in (1, 17, 1200):
            override = argparse.Namespace(timeout=value)
            for identifier in ('UX-NAV-001', 'UX-UND-016', 'UX-PRS-019', 'UX-MAC-008'):
                self.assertEqual(runner.scenario_timeout(identifier, override), value)

    def test_release_repeats_blockers_without_tripling_default_soaks(self):
        args = argparse.Namespace(release_qualification=True, runs=1)
        self.assertEqual(runner.repetitions_for({'mode': 'BLOCKING'}, args), 3)
        self.assertEqual(runner.repetitions_for({'mode': 'SOAK'}, args), 1)
        self.assertEqual(runner.repetitions_for({'mode': 'REGRESSION'}, args), 1)
        args.runs = 5
        self.assertEqual(runner.repetitions_for({'mode': 'BLOCKING'}, args), 5)
        self.assertEqual(runner.repetitions_for({'mode': 'SOAK'}, args), 5)

    def test_original_mutation_and_integrity_only_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            source = root / 'Original.png'
            source.write_bytes(b'original')
            baseline = {str(source): runner.hashlib.sha256(source.read_bytes()).hexdigest()}
            self.assertEqual(runner.changed_originals(baseline), [])
            source.write_bytes(b'changed')
            self.assertEqual(runner.changed_originals(baseline), [str(source)])
            source.unlink()
            self.assertEqual(runner.changed_originals(baseline), [str(source)])
            summary = {'hardware': {'commit': 'fixture', 'worktree_dirty': True,
                                   'source_tree_sha256': 'fixture', 'test_executable_sha256': 'fixture'},
                       'execution_status': 'FAIL', 'release_gate': 'FAIL', 'summary': {'FAIL': 1},
                       'original_hashes': 'FAIL', 'blocking_missing_or_failed': ['A'],
                       'results': [{'id': 'ORIGINAL-INTEGRITY', 'status': 'FAIL', 'severity': 'P0'}]}
            runner.write_reports(summary, root)
            self.assertIn('ORIGINAL-INTEGRITY', (root / 'UX_REPORT.md').read_text())
            self.assertIn('Reproduced failures', (root / 'ROADMAP_CANDIDATES.md').read_text())

    def test_history_chaos_child_receives_exact_seed_count_and_replay(self):
        # Process argument routing only: this fake executable does not validate
        # a chaos schedule or exercise native UI/document-history behavior.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child = root / 'luma_ux_tests'
            child.write_text('#!/usr/bin/env python3\nimport json,sys\nfrom pathlib import Path\nr=Path(sys.argv[sys.argv.index("--root")+1])\n(r/"result.json").write_text(json.dumps({"id":"UX-UND-017","status":"PASS","forwarded_args":sys.argv[1:]}))\n')
            child.chmod(0o755)
            replay = root / 'mock-replay.jsonl'
            replay.write_text('mocked command boundary; not a native valid plan\n')
            for replay_path in (None, replay):
                with self.subTest(replay=replay_path):
                    result = runner.execute(
                        {'id': 'UX-UND-017', 'scope': 'CURRENT', 'mode': 'CHAOS',
                         'severity': 'P1', 'scenario': 'Fake argument routing'},
                        root / ('with-replay' if replay_path else 'seeded'),
                        argparse.Namespace(build=root, timeout=10, seed=0, actions=10001,
                                           replay=replay_path), root)
                    self.assertEqual(result['status'], 'PASS')
                    forwarded = result['forwarded_args']
                    self.assertEqual(forwarded[forwarded.index('--seed') + 1], '0')
                    self.assertEqual(forwarded[forwarded.index('--actions') + 1], '10001')
                    if replay_path:
                        self.assertEqual(forwarded[forwarded.index('--replay') + 1], str(replay))
                    else:
                        self.assertNotIn('--replay', forwarded)

    def test_zero_exit_cannot_hide_catalog_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child = root / 'luma_ux_tests'
            child.write_text('#!/usr/bin/env python3\nimport json,sys\nfrom pathlib import Path\nr=Path(sys.argv[sys.argv.index("--root")+1])\n(r/"catalog.sqlite").write_bytes(b"Corrupted database")\n(r/"result.json").write_text(json.dumps({"id":"A","status":"PASS"}))\n')
            child.chmod(0o755)
            connections = []
            original_connect = sqlite3.connect
            def record_connection(*args, **kwargs):
                connection = original_connect(*args, **kwargs)
                connections.append(connection)
                return connection
            with patch.object(runner.sqlite3, 'connect', record_connection):
                result = runner.execute({'id': 'A', 'scope': 'CURRENT', 'mode': 'BLOCKING',
                                         'severity': 'P0', 'scenario': 'Corrupt fake child'},
                                        root / 'result', argparse.Namespace(build=root, timeout=10), root)
            self.assertEqual(result['status'], 'FAIL')
            self.assertIn('P0 catalog integrity', result['message'])
            self.assertTrue(connections)
            for connection in connections:
                with self.assertRaises(sqlite3.ProgrammingError):
                    connection.cursor()

    def test_daily_restore_audits_the_opened_copy_after_child_exit(self):
        for restored_kind, status in [('valid', 'PASS'), ('corrupt', 'FAIL'), ('missing', 'FAIL')]:
            with self.subTest(restored_kind=restored_kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                state = root / 'state'
                (state / 'restored').mkdir(parents=True)
                connection = sqlite3.connect(state / 'catalog.sqlite')
                connection.execute('CREATE TABLE original (id INTEGER PRIMARY KEY)')
                connection.close()
                restored = state / 'restored/catalog.sqlite'
                if restored_kind == 'valid':
                    connection = sqlite3.connect(restored)
                    connection.execute('CREATE TABLE restored (id INTEGER PRIMARY KEY)')
                    connection.close()
                elif restored_kind == 'corrupt':
                    restored.write_bytes(b'Corrupted restored copy, valid original remains')
                child = root / 'luma_ux_tests'
                child.write_text('#!/usr/bin/env python3\nimport json,sys\nfrom pathlib import Path\nr=Path(sys.argv[sys.argv.index("--root")+1])\n(r/"result.json").write_text(json.dumps({"id":"UX-P10-DAY-010","status":"PASS"}))\n')
                child.chmod(0o755)
                result = runner.execute(
                    {'id': 'UX-P10-DAY-010', 'scope': '1.0', 'mode': 'BLOCKING',
                     'severity': 'P0', 'scenario': 'Mocked restored process audit'},
                    root / 'result', argparse.Namespace(build=root, timeout=10,
                                                        stage='verify', state_root=state), root)
                self.assertEqual(result['status'], status)
                self.assertEqual(result['catalog_audit_path'], str(restored))
                if status == 'FAIL':
                    self.assertIn('P0 ', result['message'])

    def test_restored_data_audit_overrides_a_child_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            state = root / 'state'
            (state / 'restored').mkdir(parents=True)
            restored = state / 'restored/catalog.sqlite'
            database = sqlite3.connect(restored)
            database.execute('CREATE TABLE fixture (id INTEGER PRIMARY KEY)')
            database.close()
            child = root / 'luma_ux_tests'
            child.write_text('#!/usr/bin/env python3\nimport json,sys\nfrom pathlib import Path\nr=Path(sys.argv[sys.argv.index("--root")+1])\n(r/"result.json").write_text(json.dumps({"id":"UX-P10-DAT-005","status":"PASS"}))\n')
            child.chmod(0o755)
            with patch.object(runner, 'audit_restored_workflow', side_effect=ValueError('orphan mask')) as audit:
                result = runner.execute(
                    {'id': 'UX-P10-DAT-005', 'scope': '1.0', 'mode': 'BLOCKING',
                     'severity': 'P0', 'scenario': 'Mocked restored data audit'},
                    root / 'result', argparse.Namespace(build=root, timeout=10,
                                                        stage='verify', state_root=state), root)
            self.assertEqual(result['status'], 'FAIL')
            self.assertIn('P0 restored data invariant: orphan mask', result['message'])
            self.assertEqual(audit.call_args.args[:4], (restored, state, root, root / 'result'))
            self.assertEqual(audit.call_args.args[4], result['child_pid'])
            self.assertTrue((root / 'result/catalog-copy.sqlite').is_file())

    def test_watchdog_overrides_a_forged_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child = root / 'luma_ux_tests'
            child.write_text('#!/usr/bin/env python3\nimport json,sys,time\nfrom pathlib import Path\nr=Path(sys.argv[sys.argv.index("--root")+1])\n(r/"result.json").write_text(json.dumps({"id":"A","status":"PASS"}))\ntime.sleep(30)\n')
            child.chmod(0o755)
            result = runner.execute({'id': 'A', 'scope': 'CURRENT', 'mode': 'BLOCKING',
                                     'severity': 'P0', 'scenario': 'Hung fake child'},
                                    root / 'result', argparse.Namespace(build=root, timeout=15), root)
            self.assertEqual(result['status'], 'FAIL')
            self.assertIn('P0 hard hang', result['message'])
            self.assertEqual(result['child_returncode'], -signal.SIGKILL)
            self.assertEqual(result['child_reported_status'], 'PASS')
            self.assertEqual(json.loads((root / 'result/child-result.json').read_text())['status'], 'PASS')

    def test_abnormal_exit_cannot_hide_behind_an_empty_child_pass_message(self):
        # Real isolated subprocesses exercise supervisor accounting, not native UX.
        cases = [('positive-exit', 'sys.exit(7)', 7, 'Child exited 7'),
                 ('signal-exit', 'os.kill(os.getpid(), signal.SIGTERM)',
                  -signal.SIGTERM, 'Child exited -15 (SIGTERM)')]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child = root / 'luma_ux_tests'
            for name, termination, expected_code, message in cases:
                with self.subTest(name=name):
                    child.write_text('''#!/usr/bin/env python3
import json,os,signal,sys
from pathlib import Path
r=Path(sys.argv[sys.argv.index("--root")+1])
(r/"result.json").write_text(json.dumps({"id":"A","status":"PASS","message":""}))
''' + termination + '\n')
                    child.chmod(0o755)
                    destination = root / name
                    result = runner.execute(
                        {'id': 'A', 'scope': 'CURRENT', 'mode': 'BLOCKING',
                         'severity': 'P0', 'scenario': 'Fake post-result abnormal exit'},
                        destination, argparse.Namespace(build=root, timeout=10), root)
                    self.assertEqual(result['status'], 'FAIL')
                    self.assertEqual(result['child_returncode'], expected_code)
                    self.assertEqual(result['child_reported_status'], 'PASS')
                    self.assertEqual(result['message'], message)
                    raw = json.loads((destination / 'child-result.json').read_text())
                    self.assertEqual(raw, {'id': 'A', 'status': 'PASS', 'message': ''})
                    self.assertEqual(json.loads((destination / 'result.json').read_text()), result)

    def test_nonempty_child_failure_survives_abnormal_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child = root / 'luma_ux_tests'
            child.write_text('''#!/usr/bin/env python3
import json,sys
from pathlib import Path
r=Path(sys.argv[sys.argv.index("--root")+1])
(r/"result.json").write_text(json.dumps({"id":"A","status":"FAIL","message":"Exact child assertion"}))
sys.exit(2)
''')
            child.chmod(0o755)
            result = runner.execute(
                {'id': 'A', 'scope': 'CURRENT', 'mode': 'BLOCKING',
                 'severity': 'P0', 'scenario': 'Fake assertion failure'},
                root / 'result', argparse.Namespace(build=root, timeout=10), root)
            self.assertEqual(result['status'], 'FAIL')
            self.assertEqual(result['message'], 'Exact child assertion')
            self.assertEqual(result['child_returncode'], 2)
            self.assertEqual(result['child_reported_status'], 'FAIL')

    def test_verified_intentional_kill_retains_exit_code_and_checkpoint_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child = root / 'luma_ux_tests'
            child.write_text('''#!/usr/bin/env python3
import json,os,sys,time
from pathlib import Path
r=Path(sys.argv[sys.argv.index("--root")+1])
(r/"metrics.json").write_text(json.dumps({"checkpoint_metric":42}))
(r/"kill-ready.json").write_text(json.dumps({"id":"UX-UND-014","stage":"prepare","pid":os.getpid(),"utc_ms":time.time_ns()//1000000}))
time.sleep(30)
''')
            child.chmod(0o755)
            result = runner.execute(
                {'id': 'UX-UND-014', 'scope': 'CURRENT', 'mode': 'BLOCKING',
                 'severity': 'P0', 'scenario': 'Verified fake persistence checkpoint'},
                root / 'result', argparse.Namespace(build=root, timeout=10, stage='prepare'), root)
            self.assertEqual(result['status'], 'PASS')
            self.assertTrue(result['intentional_kill'])
            self.assertEqual(result['child_returncode'], -signal.SIGKILL)
            self.assertIsNone(result['child_reported_status'])
            self.assertEqual(result['checkpoint_metric'], 42)
            self.assertFalse((root / 'result/child-result.json').exists())

    def test_valid_marker_does_not_excuse_exit_before_actual_sigkill(self):
        # Deterministically cover the race between marker validation and the
        # supervisor's final poll. No unrelated process or real signal is used.
        for exit_code in (0, 7, -signal.SIGTERM):
            with self.subTest(exit_code=exit_code), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                destination = root / 'result'

                class ExitedBeforeKill:
                    pid = 987654321
                    returncode = None
                    observed_running = False

                    def poll(self):
                        if not self.observed_running:
                            self.observed_running = True
                            return None
                        self.returncode = exit_code
                        return self.returncode

                def launch(*args, **kwargs):
                    (destination / 'result.json').write_text(json.dumps(
                        {'id': 'UX-UND-014', 'status': 'PASS', 'message': ''}))
                    (destination / 'kill-ready.json').write_text(json.dumps(
                        {'id': 'UX-UND-014', 'stage': 'prepare', 'pid': ExitedBeforeKill.pid}))
                    return ExitedBeforeKill()

                with patch.object(runner.subprocess, 'Popen', side_effect=launch), \
                     patch.object(runner.os, 'killpg') as kill:
                    result = runner.execute(
                        {'id': 'UX-UND-014', 'scope': 'CURRENT', 'mode': 'BLOCKING',
                         'severity': 'P0', 'scenario': 'Mocked checkpoint/exit race'},
                        destination, argparse.Namespace(build=root, timeout=10, stage='prepare'), root)
                kill.assert_not_called()
                self.assertEqual(result['status'], 'FAIL')
                self.assertEqual(result['child_returncode'], exit_code)
                self.assertEqual(result['child_reported_status'], 'PASS')
                self.assertTrue(result['intentional_kill_requested'])
                self.assertNotIn('intentional_kill', result)
                self.assertIn('not terminated by SIGKILL', result['message'])
                self.assertEqual(json.loads((destination / 'child-result.json').read_text())['status'], 'PASS')

    def test_restart_uses_new_process_and_cannot_hide_second_stage_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child = root / 'luma_ux_tests'
            child.write_text('''#!/usr/bin/env python3
import json,sys
from pathlib import Path
r=Path(sys.argv[sys.argv.index("--root")+1])
stage=sys.argv[sys.argv.index("--stage")+1]
state=Path(sys.argv[sys.argv.index("--state-root")+1])
state.mkdir(exist_ok=True)
if stage == "prepare":
    (state/"saved.txt").write_text("durable")
else:
    assert (state/"saved.txt").read_text() == "durable"
(r/"result.json").write_text(json.dumps({"id":"UX-APP-003","status":"PASS" if stage == "prepare" else "FAIL","message":stage}))
''')
            child.chmod(0o755)
            result = runner.execute_scenario({'id': 'UX-APP-003', 'scope': 'CURRENT', 'mode': 'REGRESSION',
                                             'severity': 'P1', 'scenario': 'Fake restart'},
                                            root / 'result', argparse.Namespace(build=root, timeout=10), root)
            self.assertEqual(result['status'], 'FAIL')
            self.assertEqual(len(result['stages']), 2)
            self.assertNotEqual(result['stages'][0]['child_pid'], result['stages'][1]['child_pid'])

    def test_commit_checkpoints_use_separate_state_pairs_and_propagate_failure(self):
        row = {'id': 'UX-P09-FLT-001', 'scope': '0.9', 'mode': 'BLOCKING',
               'severity': 'P0', 'scenario': 'Commit checkpoint supervisor accounting'}
        for failed in (None, 'prepare', 'verify', 'prepare-committed', 'verify-committed'):
            with self.subTest(failed=failed), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                calls = []
                def execute(row, destination, options, fixture):
                    calls.append((options.stage, options.state_root))
                    return {'id': row['id'], 'status': 'FAIL' if options.stage == failed else 'PASS',
                            'message': options.stage, 'wall_duration_ms': 1}
                with patch.object(runner, 'execute', side_effect=execute):
                    result = runner.execute_scenario(row, root / 'result', argparse.Namespace(), root)
                stages = ['prepare', 'verify', 'prepare-committed', 'verify-committed']
                required = stages if failed is None else stages[:stages.index(failed) + 1]
                self.assertEqual([stage for stage, _ in calls], required)
                self.assertEqual([state for _, state in calls], [root / 'result' /
                    ('state-committed' if stage.endswith('-committed') else 'state') for stage in required])
                self.assertEqual(result['status'], 'FAIL' if failed else 'PASS')
                self.assertEqual(result['wall_duration_ms'], len(required))

    def test_committed_checkpoint_requires_actual_sigkill(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child = root / 'luma_ux_tests'
            child.write_text('''#!/usr/bin/env python3
import json,os,sys,time
from pathlib import Path
r=Path(sys.argv[sys.argv.index("--root")+1])
(r/"metrics.json").write_text(json.dumps({"checkpoint_metric":42}))
(r/"kill-ready.json").write_text(json.dumps({"id":"UX-P09-FLT-001","stage":"prepare-committed","pid":os.getpid(),"utc_ms":time.time_ns()//1000000}))
time.sleep(30)
''')
            child.chmod(0o755)
            result = runner.execute({'id':'UX-P09-FLT-001','scope':'0.9','mode':'BLOCKING',
                                     'severity':'P0','scenario':'Fake committed checkpoint'}, root / 'result',
                                    argparse.Namespace(build=root, timeout=10, stage='prepare-committed'), root)
            self.assertEqual(result['status'], 'PASS')
            self.assertEqual(result['child_returncode'], -signal.SIGKILL)
            self.assertTrue(result['intentional_kill'])

    def _fake_isolated_variant_children(self, identifier, variants, failed_variant=None):
        # Infrastructure fixture only: no native application, volume or UI claim.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child = root / 'luma_ux_tests'
            child.write_text('''#!/usr/bin/env python3
import json,sys
from pathlib import Path
r=Path(sys.argv[sys.argv.index("--root")+1])
identifier=sys.argv[sys.argv.index("--case")+1]
variant=sys.argv[sys.argv.index("--variant")+1]
assert "--stage" not in sys.argv and "--state-root" not in sys.argv
sentinel=r/"process-state.txt"
assert not sentinel.exists()
sentinel.write_text(variant)
failed_variant=''' + repr(failed_variant) + '''
(r/"result.json").write_text(json.dumps({"id":identifier,"status":"FAIL" if variant == failed_variant else "PASS","message":variant}))
''')
            child.chmod(0o755)
            destination = root / 'result'
            # Deliberately contaminated caller namespace must not become shared state.
            arguments = argparse.Namespace(build=root, timeout=10, stage='prepare',
                                           state_root=root / 'forbidden-shared-state',
                                           variant='wrong-inherited-variant')
            row = {'id': identifier, 'scope': 'ALL', 'mode': 'REGRESSION',
                   'severity': 'P1', 'scenario': 'Fake isolated operation children'}
            result = runner.execute_scenario(row, destination, arguments, root)
            expected_status = 'FAIL' if failed_variant else 'PASS'
            self.assertEqual(result['id'], identifier)
            self.assertEqual(result['status'], expected_status)
            self.assertEqual(result['metric_scope'], 'final_stage')
            self.assertEqual(len(result['stages']), len(variants))
            self.assertEqual([stage['variant'] for stage in result['stages']], list(variants))
            self.assertEqual([stage['message'] for stage in result['stages']], list(variants))
            self.assertEqual(len({stage['child_pid'] for stage in result['stages']}), len(variants))
            expected_roots = [destination / f'{index + 1:02}-{variant}'
                              for index, variant in enumerate(variants)]
            self.assertEqual([Path(stage['artifacts']) for stage in result['stages']], expected_roots)
            self.assertEqual(len(set(expected_roots)), len(variants))
            for stage, variant, case_root in zip(result['stages'], variants, expected_roots):
                self.assertEqual(stage['id'], identifier)
                self.assertEqual(stage['child_returncode'], 0)
                command = json.loads((case_root / 'command.json').read_text())
                self.assertEqual(command[command.index('--case') + 1], identifier)
                self.assertEqual(command[command.index('--variant') + 1], variant)
                self.assertEqual(Path(command[command.index('--root') + 1]), case_root)
                self.assertNotIn('--stage', command)
                self.assertNotIn('--state-root', command)
                self.assertEqual((case_root / 'process-state.txt').read_text(), variant)
                self.assertEqual(json.loads((case_root / 'child-result.json').read_text())['id'], identifier)
            self.assertFalse((destination / 'state').exists())
            self.assertFalse(arguments.state_root.exists())
            self.assertEqual(arguments.stage, 'prepare')
            self.assertEqual(arguments.variant, 'wrong-inherited-variant')
            self.assertEqual(json.loads((destination / 'result.json').read_text())['status'], expected_status)

    def test_updater_checks_all_three_operations_in_distinct_processes(self):
        self._fake_isolated_variant_children('UX-UPD-003', ('import', 'export', 'batch'), 'batch')

    def test_mac008_requires_both_variants_in_distinct_processes_without_shared_state(self):
        self._fake_isolated_variant_children('UX-MAC-008', ('copy', 'export'))

    def test_daily_merge_requires_hdr_and_panorama_in_distinct_processes(self):
        self._fake_isolated_variant_children('UX-P10-DAY-011', ('hdr', 'panorama'))
        self._fake_isolated_variant_children('UX-P10-DAY-011', ('hdr', 'panorama'), 'panorama')

    def test_update_guard_requires_all_operations_including_cancelled_worker(self):
        variants = ('save', 'import', 'export', 'batch', 'merge', 'merge-cancel')
        self._fake_isolated_variant_children('UX-P10-UPG-012', variants)
        self._fake_isolated_variant_children('UX-P10-UPG-012', variants, 'merge-cancel')

    def test_offline_sandbox_is_scoped_to_the_actual_child_and_preserves_arguments(self):
        command = ['/owned path/Luma UX', '--root', '/owned path/evidence', '--case', 'UX-P10-DAY-012']
        wrapped = runner.offline_child_command('UX-P10-DAY-012', command)
        self.assertEqual(wrapped[:3], ['/usr/bin/sandbox-exec', '-p',
                                      '(version 1) (allow default) (deny network*)'])
        self.assertEqual(wrapped[3:], command)
        self.assertEqual(runner.offline_child_command('UX-P10-UPG-012', command), command)
        self.assertEqual(command[0], '/owned path/Luma UX')

    def _mac008_failure_aggregation(self, failed_variant, expected_variants):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            row = {'id': 'UX-MAC-008', 'scope': 'ALL', 'mode': 'REGRESSION',
                   'severity': 'P1', 'scenario': 'Mocked isolated variant failure accounting'}
            calls = []

            def fake_execute(received_row, destination, arguments, fixtures):
                self.assertEqual(received_row, row)
                self.assertEqual(fixtures, root)
                self.assertFalse(hasattr(arguments, 'stage'))
                self.assertFalse(hasattr(arguments, 'state_root'))
                calls.append((destination, arguments.variant))
                return {'id': row['id'], 'status': 'FAIL' if arguments.variant == failed_variant else 'PASS',
                        'message': arguments.variant, 'variant': arguments.variant,
                        'artifacts': str(destination), 'child_pid': 100 + len(calls),
                        'wall_duration_ms': 7}

            with patch.object(runner, 'execute', side_effect=fake_execute):
                result = runner.execute_scenario(
                    row, root / 'result', argparse.Namespace(build=root, stage='verify',
                                                            state_root=root / 'unexpected-shared-state'), root)
            self.assertEqual([variant for _, variant in calls], list(expected_variants))
            self.assertEqual([path.name for path, _ in calls],
                             [f'{index + 1:02}-{variant}' for index, variant in enumerate(expected_variants)])
            self.assertEqual([stage['variant'] for stage in result['stages']], list(expected_variants))
            self.assertEqual(result['status'], 'FAIL')
            self.assertEqual(result['message'], failed_variant)
            self.assertEqual(result['wall_duration_ms'], 7 * len(expected_variants))
            self.assertEqual(json.loads((root / 'result' / 'result.json').read_text())['status'], 'FAIL')

    def test_mac008_copy_failure_prevents_export_and_cannot_pass(self):
        self._mac008_failure_aggregation('copy', ('copy',))

    def test_mac008_export_failure_cannot_hide_behind_copy_pass(self):
        self._mac008_failure_aggregation('export', ('copy', 'export'))

    def test_invalid_kill_checkpoint_never_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            child = root / 'luma_ux_tests'
            child.write_text('''#!/usr/bin/env python3
import json,sys,time
from pathlib import Path
r=Path(sys.argv[sys.argv.index("--root")+1])
(r/"kill-ready.json").write_text(json.dumps({"id":"UX-UND-014","stage":"prepare","pid":-1}))
time.sleep(30)
''')
            child.chmod(0o755)
            result = runner.execute({'id': 'UX-UND-014', 'scope': 'CURRENT', 'mode': 'BLOCKING',
                                     'severity': 'P0', 'scenario': 'Invalid fake marker'},
                                    root / 'result', argparse.Namespace(build=root, timeout=10, stage='prepare'), root)
            self.assertEqual(result['status'], 'FAIL')
            self.assertEqual(result['message'], 'Invalid intentional-kill stage marker')


class SupplementalKeyboardTests(unittest.TestCase):
    def _cli(self, *, status='PASS', flags=(), record='valid', changed=False,
             module=keyboard_runner, report_name='ux-keyboard-report'):
        # Accounting/preflight fixture only: no application, screen or native inputs.
        subject = module.runner
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            build = root / 'build'
            build.mkdir()
            snapshot = {'source_tree_sha256': 'source', 'test_executable_sha256': 'binary',
                        'commit': 'fixture', 'worktree_dirty': False}
            if record != 'missing':
                recorded = {'schema': 1, **snapshot}
                if record == 'mismatch':
                    recorded['test_executable_sha256'] = 'other'
                (build / 'ux-build-provenance.json').write_text(json.dumps(recorded))
            end = {**snapshot, 'source_tree_sha256': 'changed' if changed else 'source'}
            (build / 'ux-report').mkdir()
            official_report = build / 'ux-report' / 'UX_REPORT.json'
            official_report.write_text('unchanged-official-report')
            argv = [module.__name__ + '.py', '--build', str(build), '--runs', '3',
                    '--require-build-provenance', *flags]
            stderr = io.StringIO()

            def fake_execute(row, destination, arguments, fixture_folder):
                destination.mkdir(parents=True)
                result_status = status.get(row['id'], 'PASS') if isinstance(status, dict) else status
                return {**row, 'status': result_status, 'child_pid': 123, 'artifacts': str(destination)}

            with (patch.object(subject, 'source_phase', return_value='0.5'),
                  patch.object(subject, 'fingerprint', side_effect=[snapshot, snapshot, end]),
                  patch.object(module, 'one_png_fixture', return_value={}) as fixtures,
                  patch.object(subject, 'execute_scenario', side_effect=fake_execute) as execute,
                  patch.object(subject, 'qualify') as qualify,
                  patch.object(subject, 'write_reports') as reports,
                  patch.object(subject.fcntl, 'flock'),
                  patch.object(subject.tempfile, 'gettempdir', return_value=str(root)),
                  patch('sys.argv', argv), patch('sys.stderr', stderr), patch('builtins.print')):
                try:
                    code = module.main()
                except SystemExit as error:
                    code = error.code
                qualify.assert_not_called()
                self.assertEqual(official_report.read_text(), 'unchanged-official-report')
                if reports.called:
                    self.assertEqual(reports.call_args.args[1], build / report_name)
                    self.assertEqual(fixtures.call_args.args[0].name, 'public-fixtures-supplemental')
                    self.assertEqual(fixtures.call_args.args[0].parent.parent, build / 'ux-artifacts')
                return {'code': code, 'children': execute.call_count, 'fixtures': fixtures.call_count,
                        'summary': reports.call_args.args[0] if reports.called else None,
                        'stderr': stderr.getvalue()}

    def test_supplemental_pass_fail_and_source_change_never_qualify_official_inventory(self):
        subject = keyboard_runner.runner
        original_ids = [row['id'] for row in subject.INVENTORY]
        self.assertEqual(len(original_ids), 770)
        self.assertEqual(keyboard_runner.IDENTIFIERS,
                         {keyboard_runner.IDENTIFIER, keyboard_runner.KEYPAD_IDENTIFIER})
        self.assertEqual({row['id'] for row in keyboard_runner.INVENTORY},
                         keyboard_runner.IDENTIFIERS)
        for identifier in sorted(keyboard_runner.IDENTIFIERS):
            with self.subTest(identifier=identifier):
                self.assertNotIn(identifier, original_ids)
                self.assertNotIn(identifier, subject.NATIVE)
        for status, changed, code, execution in (('PASS', False, 0, 'PASS'),
                                                  ('FAIL', False, 1, 'FAIL'),
                                                  ('PASS', True, 1, 'INCOMPLETE')):
            with self.subTest(status=status, changed=changed):
                result = self._cli(status=status, changed=changed)
                summary = result['summary']
                self.assertEqual(result['code'], code)
                self.assertEqual(result['children'], 6)
                self.assertEqual(summary['inventory_domain'], 'supplemental')
                self.assertEqual(summary['official_inventory_count'], 770)
                self.assertEqual(summary['selected_inventory_count'], 2)
                self.assertEqual(summary['release_gate'], 'NOT_QUALIFIED')
                self.assertEqual(summary['execution_status'], execution)
                self.assertEqual(summary['blocking_missing_or_failed'], [])
                self.assertEqual({row['id'] for row in summary['results']},
                                 keyboard_runner.IDENTIFIERS)
                self.assertEqual([row['id'] for row in subject.INVENTORY], original_ids)

    def test_supplemental_case_selects_each_keyboard_variant(self):
        for identifier in sorted(keyboard_runner.IDENTIFIERS):
            with self.subTest(identifier=identifier):
                result = self._cli(flags=('--case', identifier))
                self.assertEqual(result['code'], 0)
                self.assertEqual(result['children'], 3)
                self.assertEqual(result['fixtures'], 1)
                self.assertEqual(result['summary']['release_gate'], 'NOT_QUALIFIED')
                self.assertEqual({row['id'] for row in result['summary']['results']},
                                 {identifier})

    def test_supplemental_preflight_rejects_wrong_scope_flags_and_unattested_builds(self):
        for flags in (('--release-qualification',), ('--phase', '0.5'), ('--seed', '1'),
                      ('--actions', '1'), ('--replay', '/absent-replay'),
                      ('--suite', 'blocking'), ('--case', 'UX-KEY-015'), ('--quality-only',)):
            with self.subTest(flags=flags):
                result = self._cli(flags=flags)
                self.assertEqual(result['code'], 2)
                self.assertEqual(result['children'], 0)
                self.assertEqual(result['fixtures'], 0)
                self.assertIsNone(result['summary'])
        for record in ('missing', 'mismatch'):
            with self.subTest(record=record):
                result = self._cli(record=record)
                self.assertEqual(result['code'], 2)
                self.assertIn('Build provenance preflight failed', result['stderr'])
                self.assertEqual(result['children'], 0)
                self.assertEqual(result['fixtures'], 0)

    def test_keyboard_fixture_uses_exactly_one_small_png_and_does_not_rewrite_it(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / 'fixtures'
            with patch.object(keyboard_runner.runner, 'fixtures', side_effect=AssertionError('No bulk fixture setup')):
                checksums = keyboard_runner.one_png_fixture(folder)
                source = folder / 'Photo-0000.png'
                before = source.stat().st_mtime_ns
                self.assertEqual(keyboard_runner.one_png_fixture(folder), checksums)
            raw = source.read_bytes()
            self.assertEqual(raw[:8], b'\x89PNG\r\n\x1a\n')
            self.assertEqual(int.from_bytes(raw[16:20], 'big'), 256)
            self.assertEqual(int.from_bytes(raw[20:24], 'big'), 192)
            self.assertEqual(checksums, {str(source.resolve()): hashlib.sha256(raw).hexdigest()})
            self.assertEqual(list(folder.iterdir()), [source])
            self.assertEqual(source.stat().st_mtime_ns, before)


class SupplementalMaskHandleTests(unittest.TestCase):
    _cli = SupplementalKeyboardTests._cli

    def test_mask_variants_preserve_official_inventory_and_propagate_failure(self):
        subject = mask_runner.runner
        original = [row['id'] for row in subject.INVENTORY]
        self.assertEqual(len(original), 770)
        for identifier in mask_runner.IDENTIFIERS:
            self.assertNotIn(identifier, original)
            self.assertNotIn(identifier, subject.NATIVE)
        for status, changed, code, execution in (
                ('PASS', False, 0, 'PASS'),
                ({mask_runner.THIN_IDENTIFIER: 'FAIL'}, False, 1, 'FAIL'),
                ('PASS', True, 1, 'INCOMPLETE')):
            with self.subTest(status=status, changed=changed):
                result = self._cli(module=mask_runner, report_name='ux-mask-handles-report',
                                   status=status, changed=changed)
                summary = result['summary']
                self.assertEqual(result['code'], code)
                self.assertEqual(result['children'], 6)
                self.assertEqual(result['fixtures'], 1)
                self.assertEqual(summary['inventory_domain'], 'supplemental')
                self.assertEqual(summary['official_inventory_count'], 770)
                self.assertEqual(summary['selected_inventory_count'], 2)
                self.assertEqual(summary['execution_status'], execution)
                self.assertEqual(summary['release_gate'], 'NOT_QUALIFIED')
                self.assertEqual({r['id'] for r in summary['results']}, mask_runner.IDENTIFIERS)
                self.assertEqual([row['id'] for row in subject.INVENTORY], original)
                if isinstance(status, dict):
                    self.assertEqual(summary['summary'], {'PASS': 3, 'FAIL': 3})

    def test_each_mask_variant_can_run_in_its_own_process_cohort(self):
        for identifier in sorted(mask_runner.IDENTIFIERS):
            with self.subTest(identifier=identifier):
                result = self._cli(module=mask_runner, report_name='ux-mask-handles-report',
                                   flags=('--case', identifier))
                self.assertEqual(result['code'], 0)
                self.assertEqual(result['children'], 3)
                self.assertEqual(result['fixtures'], 1)
                self.assertEqual({r['id'] for r in result['summary']['results']}, {identifier})

    def test_mask_wrapper_rejects_official_qualification_and_missing_attestation(self):
        for flags in (('--release-qualification',), ('--phase', '0.5'), ('--quality-only',),
                      ('--suite', 'blocking'), ('--case', 'UX-MSK-023'), ('--seed', '1')):
            with self.subTest(flags=flags):
                result = self._cli(module=mask_runner, report_name='ux-mask-handles-report',
                                   flags=flags)
                self.assertEqual(result['code'], 2)
                self.assertEqual(result['children'], 0)
                self.assertEqual(result['fixtures'], 0)
        for record in ('missing', 'mismatch'):
            with self.subTest(record=record):
                result = self._cli(module=mask_runner, report_name='ux-mask-handles-report',
                                   record=record)
                self.assertEqual(result['code'], 2)
                self.assertEqual(result['children'], 0)
                self.assertEqual(result['fixtures'], 0)


if __name__ == '__main__':
    unittest.main()
