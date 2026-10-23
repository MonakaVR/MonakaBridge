from pathlib import Path
import importlib.util,json,tempfile,hashlib,copy
ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
launch=module('launch',ROOT/'scripts/launch_artifact_set.py')
def reject(fn):
    try:fn()
    except (ValueError,KeyError,FileNotFoundError):return
    raise AssertionError('Invalid artifact accepted')
(ROOT/'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=ROOT/'build') as directory:
    root=Path(directory);package=root/'set';package.mkdir()
    manifest=dict(schema='monaka.hil-artifact-set.v1',compatibility_id='fixture',files_sha256={},receipts={},launch={'pico':dict(gui='gui.exe',service='service.exe',root='.')})
    for name in ['gui.exe','service.exe']:(package/name).write_bytes(b'fixture-not-executable')
    for name in ['build','full_regression','revision','lifecycle']:
        receipt=name+'.json';(package/receipt).write_text(json.dumps(dict(status='PASS',compatibility_id='fixture')));manifest['receipts'][name]=receipt
    manifest['files_sha256']={p.name:launch.sha(p) for p in package.iterdir()}
    (package/'artifact-manifest.json').write_text(json.dumps(manifest))
    argv,identity=launch.verify(package,'pico');assert identity=='fixture' and '--service-exe' in argv
    catalog=root/'catalog.json';catalog.write_text(json.dumps(dict(artifact_sets=[dict(package=str(package),status='PASS',components=['pico'],build_utc='2026-10-23T00:00:00Z')])))
    assert launch.select(catalog,'pico')==package
    (package/'service.exe').write_bytes(b'stale-build');reject(lambda:launch.verify(package,'pico'));reject(lambda:launch.select(catalog,'pico'))
    reject(lambda:launch.inside(package,'../outside'))
    (package/'service.exe').write_bytes(b'fixture-not-executable')
    for name in ['build','full_regression','revision','lifecycle']:
        bad=copy.deepcopy(manifest);receipt=name+'.json';previous=(package/receipt).read_bytes();(package/receipt).write_text(json.dumps(dict(status='PASS',compatibility_id='wrong')));bad['files_sha256'][receipt]=launch.sha(package/receipt);(package/'artifact-manifest.json').write_text(json.dumps(bad));reject(lambda:launch.verify(package,'pico'));(package/receipt).write_bytes(previous)
print('PASS launcher selection, every artifact hash, stale newest refusal, containment, four compatibility receipts; no process started')
