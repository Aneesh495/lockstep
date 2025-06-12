#!/usr/bin/env python3
import datetime
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import time
from zoneinfo import ZoneInfo
import make_report as report

ROOT = Path(__file__).resolve().parents[1]


def main():
    revision = report.git(ROOT, 'rev-parse', 'HEAD').decode().strip()
    source = report.source_manifest(ROOT)
    report.require(source == report.source_manifest(ROOT, revision), 'Acceptance requires committed implementation')
    starting = 'aff8df02f28036a1dd8310e74cbfc6f35c306a55'
    report.require(report.docs_manifest(ROOT) == report.docs_manifest(ROOT, starting), 'README/prose changed before acceptance')
    stamp = datetime.datetime.now(ZoneInfo('America/Indianapolis')).strftime('%Y%m%dT%H%M%S')
    evidence = ROOT / 'results/evidence' / f'{revision[:12]}-{stamp}'
    evidence.mkdir(parents=True)
    (evidence/'logs').mkdir()
    build = 'build-acceptance'
    formatter = os.environ.get('CLANG_FORMAT', 'clang-format')
    compiler = os.environ.get('CXX', 'c++')
    commands = []
    checkpoint = ROOT / 'results/verified/CHECKPOINT.json'

    def save(current, status):
        report.write_json(checkpoint, {'implementation_revision': revision, 'source_manifest': source, 'evidence_dir': str(evidence.relative_to(ROOT)), 'status': status, 'current_command': current, 'commands': commands, 'completed_phases': ['baseline', 'command_replay', 'dual_feed_implementation'], 'remaining': [name for name in report.REQUIRED_CHECKS if name not in {c['name'] for c in commands if c['exit_status'] == 0}]})

    def run(name, steps):
        path = evidence/'logs'/f'{name}.log'
        save(name, 'running')
        start = time.monotonic()
        code = 0
        with path.open('w') as log:
            for step in steps:
                log.write(json.dumps({'command': step})+'\n'); log.flush()
                print(f'Acceptance: {name}: {step}', flush=True)
                code = subprocess.call(step, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
                if code:
                    break
        commands.append({'name': name, 'commands': steps, 'exit_status': code, 'duration_seconds': time.monotonic()-start, 'log': str(path.relative_to(ROOT)), 'skipped': False})
        save(name, 'failed' if code else 'running')
        report.require(code == 0, f'{name} failed with exit {code}; log: {path}')
        report.require(report.source_manifest(ROOT) == source, 'Source changed during acceptance')
        report.require(report.docs_manifest(ROOT) == report.docs_manifest(ROOT, starting), 'A target changed prose documentation')

    report.write_json(evidence/'host.json', {'platform': platform.platform(), 'host': platform.node(), 'python': sys.version, 'implementation_revision': revision, 'source_manifest_sha256': report.sha(json.dumps(source, sort_keys=True).encode()), 'started': stamp, 'CXX': compiler, 'CLANG_FORMAT': formatter})
    save('', 'running')
    try:
        run('release', [['cmake','-S','.','-B',build,'-DCMAKE_BUILD_TYPE=Release','-DLOCKSTEP_ENABLE_NATIVE=ON'], ['cmake','--build',build,'--parallel']])
        shutil.copyfile(ROOT/build/'compile_commands.json', evidence/'compile_commands.json')
        with (evidence/'test-discovery.json').open('w') as output:
            subprocess.run(['ctest','--test-dir',build,'--show-only=json-v1'],cwd=ROOT,stdout=output,check=True)
        discovery = report.read_json(evidence/'test-discovery.json')
        report.require(len(discovery['tests']) >= 19, 'Zero/short test discovery')
        run('test', [['make','test','BUILD_DIR=build-acceptance-portable']])
        run('sanitize', [['make','sanitize',f'BUILD_DIR={build}']])
        run('tsan', [['make','tsan',f'BUILD_DIR={build}']])
        run('fuzz', [['make','fuzz-smoke',f'BUILD_DIR={build}']])
        run('demo', [['make','demo',f'BUILD_DIR={build}']])
        run('headers', [[sys.executable,'tools/check_headers.py','--compiler',compiler]])
        run('format', [[sys.executable,'tools/check_headers.py','--format-only','--clang-format',formatter]])
        run('benchmark', [[f'{build}/lockstep_bench','--output',str(evidence/'benchmark'),'--repetitions','10','--operations','1000000','--latency-operations','100000']])
        run('fault', [[f'{build}/lockstep_fault_stress','--events','100000000','--shards','4','--seed','12345','--output',str(evidence/'stress')]])
        run('crash', [[f'{build}/lockstep_crash_matrix','--trials','10000','--seed','12345','--output',str(evidence/'stress')]])
        with tarfile.open(evidence/'stress/persisted-trials.tar.gz','w:gz') as archive:
            archive.add(evidence/'stress/trials',arcname='trials')
        shutil.rmtree(evidence/'stress/trials')
        ci = ROOT/'artifacts/hosted-ci/ci.json'
        hosted = None
        if ci.is_file():
            shutil.copytree(ci.parent,evidence/'hosted-ci')
            hosted = evidence/'hosted-ci/ci.json'
        baseline = ROOT/'artifacts/resume-baseline'
        if baseline.is_dir():
            shutil.copytree(baseline,evidence/'baseline')
        summary = report.generate(ROOT,evidence,commands,revision,starting,hosted)
        save('verify', 'complete' if summary['all_targets_met'] else 'gates_unmet')
        report.require(summary['all_targets_met'], 'Full gates unmet; machine results retained')
        report.verify(ROOT)
        print(json.dumps(summary,indent=2),flush=True)
        return 0
    except Exception as e:
        save('', 'failed')
        report.write_json(evidence/'failure.json', {'error': str(e), 'commands': commands})
        print(e,file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
