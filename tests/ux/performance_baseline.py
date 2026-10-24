"""Versioned performance evidence; schema validation never grants release approval."""
from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path
from jsonschema import Draft202012Validator, ValidationError

SCHEMA_PATH = Path(__file__).with_name('performance-baseline.schema.json')
SCHEMA = json.loads(SCHEMA_PATH.read_text())
Draft202012Validator.check_schema(SCHEMA)
VALIDATOR = Draft202012Validator(SCHEMA)


def statistics(samples):
    ordered = sorted(samples)
    return {'count': len(ordered), 'minimum': ordered[0], 'maximum': ordered[-1],
            **{f'p{p}': ordered[math.ceil(p / 100 * len(ordered)) - 1] for p in (50, 95, 99)}}


def validate(record):
    # JSON Schema numeric comparisons alone do not reject Python NaN values.
    json.dumps(record, allow_nan=False)
    VALIDATOR.validate(record)
    if record['statistics'] != statistics(record['samples']):
        raise ValueError('Baseline statistics differ from the retained samples')
    return record


def comparable(before, after):
    validate(before)
    validate(after)
    return all(before[key] == after[key]
               for key in ('case_id', 'measurement', 'hardware', 'workload'))


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def from_progressive_report(path):
    """Import three actual, successful PRG001 runs; never relabel a failed run."""
    path = Path(path).resolve()
    report = json.loads(path.read_text())
    if (report['source_consistency'] != 'PASS' or report['build_provenance']['status'] != 'PASS'
            or report['original_hashes'] != 'PASS'):
        raise ValueError('Baseline requires stable, attested source and preserved originals')
    rows = [row for row in report['results'] if row['id'] == 'UX-P06-PRG-001']
    if len(rows) < 3 or any(row['status'] != 'PASS' for row in rows):
        raise ValueError('Baseline requires at least three successful complete repetitions')
    run_root = Path(report['run_directory']).resolve()
    samples, hashes, fixture_hashes = [], [], set()
    profile = None
    for row in rows:
        folder = Path(row['artifacts']).resolve()
        if not folder.is_relative_to(run_root):
            raise ValueError('Measurement escaped its recorded run directory')
        measurement = folder / 'measurements.json'
        value = json.loads(measurement.read_text())
        if (value['case'] != row['id'] or value['schema'] != 1
                or value['display_scanout_measured'] is not False
                or value['readback_included_in_latency'] is not False
                or value['frame_requires_current_image_synchronization'] is not True):
            raise ValueError('Incompatible presentation measurement contract')
        current = {key: row[key] for key in ('platform', 'graphics_api', 'device_pixel_ratio')}
        current.update(window_width=row['window_size']['width'], window_height=row['window_size']['height'])
        if profile is not None and profile != current:
            raise ValueError('Cannot mix native rendering configurations')
        profile = current
        values = [sample['input_to_frame_queued_ms'] for sample in value['samples']]
        if len(values) != 10 or statistics(values)['p95'] != value['p95_ms'] or value['p95_ms'] > value['budget_ms']:
            raise ValueError('Incomplete samples or failed latency budget')
        samples.extend(values)
        hashes.append(digest(measurement))
        fixture_hashes.add(digest(folder / 'zoom-originals/Zoom.png'))
    if len(fixture_hashes) != 1:
        raise ValueError('Cannot mix different photographic inputs')
    host = report['hardware']
    record = {
        'schema_version': 1, 'case_id': rows[0]['id'],
        'measurement': {'name': 'clarity-input-to-current-frame-queued', 'unit': 'ms', 'direction': 'lower'},
        'hardware': {**{key: host[key] for key in ('model', 'chip', 'macos', 'qt')},
                     'ram_bytes': int(host['ram_bytes']), **profile},
        'source': {'commit': host['commit'], 'tree_sha256': host['source_tree_sha256'],
                   'executable_sha256': host['test_executable_sha256'], 'worktree_dirty': host['worktree_dirty']},
        'workload': {'description': '3072x2048 chart; 10 distinct held Clarity edits per repetition; queued native frame, separate CPU pixel/readback check; observed OS pressure is retained in hashed measurement evidence',
                     'fixture_sha256': fixture_hashes.pop()},
        'samples': samples, 'statistics': statistics(samples),
        'evidence': {'report_sha256': digest(path), 'measurement_sha256': hashes},
        'qualification': 'candidate',
    }
    return validate(record)


def schema_contract_checks():
    """Synthetic contract examples exercise format rejection, never performance."""
    valid = {
        'schema_version': 1, 'case_id': 'UX-P06-PRG-001',
        'measurement': {'name': 'synthetic-schema-test', 'unit': 'ms', 'direction': 'lower'},
        'hardware': {'model': 'test', 'chip': 'test', 'ram_bytes': 1, 'macos': 'test', 'qt': 'test',
                     'platform': 'test', 'graphics_api': 'test', 'device_pixel_ratio': 2,
                     'window_width': 1, 'window_height': 1},
        'source': {'commit': '0' * 40, 'tree_sha256': '0' * 64, 'executable_sha256': '0' * 64,
                   'worktree_dirty': True},
        'workload': {'description': 'Synthetic schema fixture, not measured performance', 'fixture_sha256': '0' * 64},
        'samples': [1, 2, 3], 'statistics': statistics([1, 2, 3]),
        'evidence': {'report_sha256': '0' * 64, 'measurement_sha256': ['0' * 64]},
        'qualification': 'candidate',
    }
    validate(valid)
    rejected = 0
    mutations = [
        lambda r: r.pop('source'), lambda r: r.update(unknown=True),
        lambda r: r.update(schema_version=2), lambda r: r.update(qualification='PASS'),
        lambda r: r['source'].update(tree_sha256='invalid'),
        lambda r: r['hardware'].update(ram_bytes='8589934592'),
        lambda r: r['hardware'].update(ram_bytes=True),
        lambda r: r['measurement'].update(unit='seconds'),
        lambda r: r.update(samples=[]), lambda r: r.update(samples=[-1]),
        lambda r: r.update(samples=[float('nan')]), lambda r: r.update(samples=[float('inf')]),
        lambda r: r['statistics'].update(p95=999), lambda r: r['statistics'].update(count=1),
    ]
    for mutate in mutations:
        bad = deepcopy(valid)
        mutate(bad)
        try:
            validate(bad)
        except (ValueError, TypeError, ValidationError):
            rejected += 1
        else:
            raise AssertionError('Malformed baseline was accepted')
    if not comparable(valid, deepcopy(valid)):
        raise AssertionError('Identical profiles must remain comparable')
    for key in ('hardware', 'workload', 'measurement'):
        other = deepcopy(valid)
        field = {'hardware': 'chip', 'workload': 'description', 'measurement': 'name'}[key]
        other[key][field] += '-different'
        if comparable(valid, other):
            raise AssertionError('Incompatible measurement profiles were mixed')
    return {'schema_sha256': digest(SCHEMA_PATH), 'malformed_records_rejected': rejected,
            'incompatible_profiles_rejected': 3, 'performance_measured': False}


def execute_schema_case(row, directory):
    directory.mkdir(parents=True)
    result = {**row, 'artifacts': str(directory), 'adapter_kind': 'python-schema',
              'performance_measured': False}
    try:
        result.update(status='PASS', checks=schema_contract_checks())
    except Exception as error:
        result.update(status='FAIL', message=str(error))
    (directory / 'schema-checks.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--progressive-report', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if bool(args.progressive_report) != bool(args.output):
        parser.error('--progressive-report and --output must be supplied together')
    if args.progressive_report:
        record = from_progressive_report(args.progressive_report)
        with args.output.open('x') as stream:
            json.dump(record, stream, indent=2, allow_nan=False)
            stream.write('\n')
    else:
        print(json.dumps(schema_contract_checks()))
