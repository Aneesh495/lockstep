#!/usr/bin/env python3
"""Negative tests mutate copies of actual acceptance evidence, never campaign fixtures."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import make_report as report

root=Path(__file__).resolve().parents[1]
report.verify(root)
original=report.read_json(root/'results/verified/ACCEPTANCE.json')
manifest=report.read_json(root/original['manifest'])
results=[]
with tempfile.TemporaryDirectory(prefix='lockstep-verifier-') as temp:
    target=Path(temp)/'repo'
    subprocess.run(['git','clone','--quiet','--shared',str(root),str(target)],check=True)
    for directory in ('results',):
        if (target/directory).exists(): shutil.rmtree(target/directory)
        shutil.copytree(root/directory,target/directory)
    def fails(name,mutation,restore):
        mutation()
        process=subprocess.run(['python3',str(root/'tools/make_report.py'),'--verify','--root',str(target)],capture_output=True,text=True)
        results.append({'case':name,'exit_status':process.returncode,'output':process.stderr.strip()})
        restore()
        if process.returncode==0: raise SystemExit(f'Verifier accepted negative case: {name}')
    artifact=next(n for n in manifest['artifacts'] if n.endswith('latency-0.bin'))
    p=target/artifact; saved=p.read_bytes()
    fails('missing_artifact',lambda:p.unlink(),lambda:p.write_bytes(saved))
    fails('modified_artifact_byte',lambda:p.write_bytes(bytes([saved[0]^1])+saved[1:]),lambda:p.write_bytes(saved))
    source=target/'src/persistence/recovery.cpp'; saved_source=source.read_bytes()
    fails('stale_source',lambda:source.write_bytes(saved_source+b'\n// verifier mutation\n'),lambda:source.write_bytes(saved_source))
    manifest_path=target/original['manifest']; saved_manifest=manifest_path.read_bytes()
    acceptance=target/'results/verified/ACCEPTANCE.json'; saved_acceptance=acceptance.read_bytes()
    fault_path=target/manifest['fault_summary']; saved_fault=fault_path.read_bytes()
    def count_mutation():
        data=json.loads(saved_fault); data['totals']['logical_events']+=1
        report.write_json(fault_path,data)
        altered=copy.deepcopy(manifest); altered['artifacts'][manifest['fault_summary']]=report.sha(fault_path.read_bytes())
        report.write_json(manifest_path,altered)
        result=copy.deepcopy(original); result['manifest_sha256']=report.sha(manifest_path.read_bytes()); report.write_json(acceptance,result)
    def restore():
        fault_path.write_bytes(saved_fault); manifest_path.write_bytes(saved_manifest); acceptance.write_bytes(saved_acceptance)
    fails('falsified_count_with_updated_hash',count_mutation,restore)
    def skipped():
        altered=copy.deepcopy(manifest); altered['commands'][0]['skipped']=True
        report.write_json(manifest_path,altered)
        result=copy.deepcopy(original); result['manifest_sha256']=report.sha(manifest_path.read_bytes()); report.write_json(acceptance,result)
    fails('required_check_skipped_with_updated_hash',skipped,restore)
report.write_json(root/'results/verified/negative-verifier-tests.json',results)
print(f'{len(results)} negative verifier cases rejected')
