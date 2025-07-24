#!/usr/bin/env python3
"""Machine evidence only. Verification never executes the expensive workloads."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
PREFIXES = ('include/', 'src/', 'apps/', 'tests/', 'bench/', 'fuzz/', 'tools/', 'scripts/', 'cmake/', 'config/', '.github/')
ROOT_FILES = {'CMakeLists.txt', 'Makefile', '.clang-format', '.clang-tidy', '.gitignore'}
REQUIRED_CHECKS = {'release', 'test', 'sanitize', 'tsan', 'fuzz', 'demo', 'headers', 'format', 'benchmark', 'fault', 'crash'}
CI_JOBS = {'build-linux-gcc', 'build-linux-clang', 'build-macos-arm64', 'sanitize-linux', 'tsan-linux', 'format-check'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text())


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args])


def source_manifest(root, revision=None):
    if revision:
        files = git(root, 'ls-tree', '-r', '--name-only', '-z', revision).decode().split('\0')
    else:
        files = git(root, 'ls-files', '--cached', '--others', '--exclude-standard', '-z').decode().split('\0')
    files = sorted(set(p for p in files if p in ROOT_FILES or p.startswith(PREFIXES)))
    files = [p for p in files if not p.lower().endswith('.md') and '__pycache__' not in p]
    return {p: sha(git(root, 'show', f'{revision}:{p}') if revision else (root / p).read_bytes()) for p in files}


def docs_manifest(root, revision=None):
    files = git(root, 'ls-tree', '-r', '--name-only', '-z', revision or 'HEAD').decode().split('\0')
    return {p: sha(git(root, 'show', f'{revision}:{p}') if revision else (root / p).read_bytes()) for p in files if p.lower().endswith('.md')}


def resolve(root, name):
    require(isinstance(name, str) and not Path(name).is_absolute() and '..' not in Path(name).parts, 'Unsafe evidence path')
    path = root / name
    require(path.is_file() and not path.is_symlink(), f'Missing artifact: {name}')
    return path


def benchmark(directory):
    data = read_json(directory / 'benchmark.json')
    require(data['schema'] == 2 and data['clock_steady'] and data['allocation_hook_connected'], 'Invalid benchmark prerequisites')
    runs = data['runs']
    require(len(runs) >= 10 and len(runs) == data['repetitions'], 'Shortened benchmark repetitions')
    throughputs, p99s, allocations = [], [], []
    for index, run in enumerate(runs):
        require(run['repetition'] == index and run['seed'] == data['seed'] + index, 'Benchmark seed/repetition mismatch')
        for mode in ('throughput', 'latency'):
            result = run[mode]
            require(result['operations'] > 0 and result['duration_ns'] > 0, 'Zero benchmark progress')
            require(result['accepted'] + result['rejected'] == result['operations'], 'Inconsistent benchmark totals')
            require(all(result[k] > 0 for k in ('matches', 'resting_adds', 'successful_cancels', 'successful_modifies', 'marketable_commands', 'rejected', 'output_digest', 'state_digest')), 'Unbalanced benchmark workload')
            require(result['accepted'] > result['rejected'], 'Reject-dominated benchmark')
            rate = result['operations'] * 1e9 / result['duration_ns']
            require(math.isclose(result['throughput'], rate, rel_tol=1e-9), 'Falsified throughput')
            allocations.append(result['allocations'])
        result = run['latency']
        samples = array.array('Q')
        name = result['samples']
        require(Path(name).name == name, 'Unsafe latency sample path')
        samples.frombytes((directory / name).read_bytes())
        require(result['sample_byte_order'] in {'little', 'big'}, 'Missing sample byte order')
        if result['sample_byte_order'] != sys.byteorder:
            samples.byteswap()
        require(len(samples) == result['operations'], 'Missing latency samples')
        values = sorted(samples)
        for key, q in [('p50_ns', .50), ('p95_ns', .95), ('p99_ns', .99), ('p999_ns', .999)]:
            require(result[key] == values[int(q * (len(values) - 1))], 'Falsified latency quantile')
        require(result['max_ns'] == values[-1] and values[-1] > 0, 'Invalid latency tail')
        throughputs.append(run['throughput']['throughput'])
        p99s.append(result['p99_ns'])
    throughput = statistics.median(throughputs)
    latency = statistics.median(p99s)
    allocation_count = sum(allocations)
    worst_p99 = max(p99s)
    return {'median_commands_per_second': throughput, 'median_p99_ns': latency, 'max_p99_ns': worst_p99, 'hot_path_allocations': allocation_count, 'repetitions': len(runs), 'eligible': throughput >= 5000000 and worst_p99 < 1000 and allocation_count == 0}


def fault_campaign(directory):
    summary = read_json(directory / 'fault_stress.json')
    totals = {}
    for shard in range(summary['shards']):
        data = read_json(directory / f'fault-shard-{shard}.json')
        require(data['shard'] == shard and data['seed'] == summary['base_seed'] + shard * 10007, 'Fault shard identity mismatch')
        require(data['state_comparisons'] > 0 and data['logical_events'] > 0, 'Empty fault shard')
        require(data['events_applied'] + data['snapshot_covered_events'] == data['logical_events'], 'Fault events lost from accounting')
        for key, value in data.items():
            if key not in {'shard', 'seed'}:
                require(isinstance(value, int) and value >= 0, 'Invalid fault counter')
                totals[key] = totals.get(key, 0) + value
    require(totals == summary['totals'] and totals['logical_events'] == summary['requested_events'], 'Falsified fault counts')
    require(totals['actual_deliveries'] == totals['deliveries_a'] + totals['deliveries_b'], 'Inconsistent delivery counts')
    for channel in ('a', 'b'):
        require(all(totals[f'{fault}_{channel}'] > 0 for fault in ('dropped', 'duplicated', 'reordered', 'corrupted', 'delayed', 'outages', 'deliveries')), 'Missing fault category/channel')
    require(totals['events_applied'] > 0 and totals['decoded_records'] >= totals['events_applied'] and totals['duplicates_discarded'] > 0 and totals['corrupt_rejections'] > 0 and totals['gaps_detected'] > 0 and totals['snapshots_installed'] > 0, 'Zero fault/reconciliation progress')
    eligible = totals['logical_events'] >= 100000000 and totals['mismatches'] == 0 and totals['state_comparisons'] > 0
    require(summary['complete'] == eligible, 'Incorrect fault completion flag')
    return {'logical_events': totals['logical_events'], 'events_applied': totals['events_applied'], 'snapshot_covered_events': totals['snapshot_covered_events'], 'state_comparisons': totals['state_comparisons'], 'mismatches': totals['mismatches'], 'eligible': eligible}


def fnv(data):
    h = 1469598103934665603
    for byte in data:
        h = ((h ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return h


def crash_campaign(directory):
    summary = read_json(directory / 'recovery.json')
    trials = [json.loads(line) for line in (directory / 'recovery-trials.jsonl').read_text().splitlines()]
    require(len(trials) == summary['requested_trials'], 'Falsified crash trial total')
    totals = {k: 0 for k in summary['totals']}
    coverage = {}
    files = {}
    retained_bytes = 0
    # Read compressed evidence once. Random seeks would repeatedly decompress
    # the complete preceding archive for every trial comparison.
    with tarfile.open(directory / 'persisted-trials.tar.gz', 'r|gz') as archive:
        for member in archive:
            if Path(member.name).name not in {'expected.txt', 'actual.txt', 'recovery-error.txt', 'wal.log'}:
                continue
            require(member.isfile() and member.size < 1000000 and member.name not in files, 'Invalid persisted trial artifact')
            retained_bytes += member.size
            require(retained_bytes <= 512 * 1024 * 1024 and len(files) < 4 * len(trials), 'Oversized persisted evidence')
            files[member.name] = archive.extractfile(member).read()
    for index, trial in enumerate(trials):
        require(trial['trial'] == index and trial['seed'] == summary['seed'] + index * 10007, 'Crash trial identity mismatch')
        require(trial['writer_signal'] == 9, 'A trial did not interrupt a real child')
        require(trial['recovered_prefix'] >= trial['acknowledged_prefix'] and trial['acknowledged_prefix'] > 0, 'Acknowledged prefix lost')
        mode = trial['mode']
        coverage[mode] = coverage.get(mode, 0) + 1
        if mode.startswith('snapshot_before') or mode.startswith('snapshot_after'):
            key = f'snapshot_stage_{trial["stage"]}'
            coverage[key] = coverage.get(key, 0) + 1
        def get(name):
            data = files.get(f'trials/{index}/{name}')
            require(data is not None, f'Missing persisted trial file {index}/{name}')
            return data
        expected = get('expected.txt')
        require(fnv(expected) == trial['expected_digest'], 'Expected trial state hash mismatch')
        get('wal.log')
        totals['trials_run'] += 1
        totals['interrupted_processes'] += 1
        totals['restarted_processes'] += 1
        if mode == 'interior_corruption':
            actual = get('recovery-error.txt')
            require(trial['restart_exit'] == 2 and not trial['comparison_ran'] and b'CRC' in actual, 'Corruption counted as recovery')
            totals['expected_corruption_rejections'] += 1
            passed = True
        else:
            actual = get('actual.txt')
            require(trial['comparison_ran'] and trial['restart_exit'] == 0, 'State comparison did not execute')
            totals['state_comparisons'] += 1
            passed = expected == actual
            if passed:
                totals['successful_recoveries'] += 1
        require(fnv(actual) == trial['actual_digest'], 'Actual trial state hash mismatch')
        if not passed:
            totals['unexpected_failures'] += 1
        require(trial['passed'] == passed, 'Falsified trial outcome')
    require(totals == summary['totals'] and coverage == summary['coverage'], 'Falsified recovery aggregation')
    required_modes = {'wal_only', 'snapshot_plus_wal', 'periodic_sync', 'durable_prefix', 'torn_tail', 'interior_corruption', 'snapshot_before_rename', 'recovery_then_trading', 'random_timing', 'snapshot_after_rename'}
    require(required_modes.issubset(coverage) and all(coverage.get(f'snapshot_stage_{i}', 0) > 0 for i in range(4)), 'Missing crash scenario')
    eligible = len(trials) >= 10000 and totals['unexpected_failures'] == 0 and totals['state_comparisons'] > 0
    require(summary['complete'] == eligible, 'Incorrect crash completion flag')
    return {**totals, 'eligible': eligible}


def calculate(root, manifest):
    commands = {c['name']: c for c in manifest['commands']}
    require(set(commands) == REQUIRED_CHECKS and len(commands) == len(manifest['commands']), 'Missing/duplicate required command')
    for name, command in commands.items():
        require(command['exit_status'] == 0 and command.get('skipped', False) is False, f'Required command failed/skipped: {name}')
        require(command['duration_seconds'] > 0, 'Zero command duration')
        log = resolve(root, command['log']).read_text(errors='replace')
        if name in {'test', 'sanitize', 'tsan'}:
            matches = re.findall(r'100% tests passed, 0 tests failed out of (\d+)', log)
            require(matches and int(matches[-1]) >= (3 if name == 'tsan' else 19), f'Zero/short test run: {name}')
            require('LockstepTests' in log if name != 'tsan' else all(f'Start' in log and t in log for t in ('spsc', 'network', 'udp')), 'Missing actual concurrency suites')
        if name == 'fuzz':
            counts = re.findall(r'Total tests: (\d+)', log)
            require(len(counts) == 3 and all(int(c) > 0 for c in counts), 'Empty fuzz run')
        if name == 'headers':
            require(re.search(r'Standalone headers compiled: [1-9]\d*', log), 'No standalone headers compiled')
        if name == 'format':
            require(re.search(r'Formatted inputs checked: [1-9]\d*', log), 'No formatting inputs')
        if name == 'demo':
            require('Invariants: OK' in log and 'Reference match: OK' in log and 'Demo complete!' in log and 'FAILED' not in log, 'Demo did not establish successful checks')
    directory = resolve(root, manifest['benchmark_summary']).parent
    perf = benchmark(directory)
    stress_dir = resolve(root, manifest['fault_summary']).parent
    faults = fault_campaign(stress_dir)
    crashes = crash_campaign(stress_dir)
    ci_path = manifest.get('hosted_ci')
    ci_ok = False
    if ci_path:
        ci = read_json(resolve(root, ci_path))
        require(ci['status'] == 'completed' and ci['conclusion'] == 'success', 'Hosted CI failed/incomplete')
        require(source_manifest(root, ci['headSha']) == manifest['source_manifest'], 'Hosted CI source differs')
        jobs = {j['name']: j for j in ci['jobs']}
        require(CI_JOBS.issubset(jobs) and all(jobs[name]['conclusion'] == 'success' for name in CI_JOBS), 'Missing/failed hosted CI job')
        ci_ok = True
    resilience = faults['eligible'] and crashes['eligible']
    return {'implementation_revision': manifest['implementation_revision'], 'performance': perf, 'fault_campaign': faults, 'crash_campaign': crashes, 'performance_bullet_eligible': perf['eligible'], 'resilience_bullet_eligible': resilience, 'hosted_ci_passed': ci_ok, 'all_targets_met': perf['eligible'] and resilience and ci_ok}


def verify(root=ROOT, acceptance=None):
    acceptance = Path(acceptance) if acceptance else root / 'results/verified/ACCEPTANCE.json'
    require(acceptance.is_file(), 'Missing acceptance evidence')
    result = read_json(acceptance)
    manifest_path = resolve(root, result['manifest'])
    require(sha(manifest_path.read_bytes()) == result['manifest_sha256'], 'Modified acceptance manifest')
    manifest = read_json(manifest_path)
    require(source_manifest(root) == manifest['source_manifest'], 'Stale implementation source')
    require(source_manifest(root, manifest['implementation_revision']) == manifest['source_manifest'], 'Evidence not bound to committed implementation')
    require(docs_manifest(root) == manifest['docs_manifest'] == docs_manifest(root, manifest['starting_revision']), 'README/prose changed')
    for name, checksum in manifest['artifacts'].items():
        require(sha(resolve(root, name).read_bytes()) == checksum, f'Modified artifact: {name}')
    required = {str(p.relative_to(root)) for p in manifest_path.parent.rglob('*') if p.is_file() and p.name != 'manifest.json'} | {c['log'] for c in manifest['commands']} | {manifest['benchmark_summary'], manifest['fault_summary'], manifest['crash_summary']}
    require(required.issubset(manifest['artifacts']), 'Unhashed required evidence')
    summary = calculate(root, manifest)
    require(result['summary'] == summary, 'Falsified acceptance fields')
    require(summary['all_targets_met'], 'Full acceptance gates unmet')
    return summary


def generate(root, evidence, commands, revision, starting, hosted=None):
    source = source_manifest(root)
    require(source == source_manifest(root, revision), 'Commit implementation before acceptance')
    artifacts = {str(p.relative_to(root)): sha(p.read_bytes()) for p in sorted(evidence.rglob('*')) if p.is_file() and p.name != 'manifest.json'}
    manifest = {'schema': 2, 'implementation_revision': revision, 'starting_revision': starting, 'source_manifest': source, 'docs_manifest': docs_manifest(root, starting), 'commands': commands, 'artifacts': artifacts,
                'benchmark_summary': str((evidence/'benchmark/benchmark.json').relative_to(root)), 'fault_summary': str((evidence/'stress/fault_stress.json').relative_to(root)), 'crash_summary': str((evidence/'stress/recovery.json').relative_to(root))}
    if hosted:
        manifest['hosted_ci'] = str(hosted.relative_to(root))
    summary = calculate(root, manifest)
    path = evidence / 'manifest.json'
    write_json(path, manifest)
    result = {'manifest': str(path.relative_to(root)), 'manifest_sha256': sha(path.read_bytes()), 'summary': summary}
    write_json(root/'results/verified/ACCEPTANCE.json', result)
    write_json(root/'results/verified/RESUME_METRICS.json', summary)
    return summary


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--verify', action='store_true')
    p.add_argument('--benchmark', type=Path)
    p.add_argument('--root', type=Path, default=ROOT)
    a = p.parse_args()
    try:
        if a.verify:
            summary = verify(a.root)
            print(json.dumps(summary, indent=2))
        elif a.benchmark:
            summary = benchmark(a.benchmark)
            write_json(a.benchmark/'summary.json', summary)
            print(json.dumps(summary, indent=2))
        else:
            p.error('Choose --verify or --benchmark')
    except (ValueError, KeyError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as e:
        print(f'FAIL: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
