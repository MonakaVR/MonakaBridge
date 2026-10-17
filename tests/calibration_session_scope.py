"""Exact stacked B2 native session layer; B1 and older pins remain immutable."""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
BASE='7ac9ed9fe4a19391a1f88ed4d00adaefbdbbd64c'
PIN='dependencies/calibration-session-baseline.json'
FILES={'include/monaka_bridge/calibration_session.hpp','src/calibration/calibration_session.cpp',
 'src/calibration/calibration_drafts.cpp','tools/calibration_local_api.cpp','tools/steamvr_calibrator.cpp',
 'tests/calibration_fixture_provider.hpp','tests/calibration_session_tests.cpp','tests/calibration_session_process_fixture.cpp',
 'tests/calibration_session_process.py','tests/openvr_bootstrap_sentinel.cpp','CMakeLists.txt',
 'docs/calibration-session-contract.md','tests/calibration_session_scope.py','tests/calibration_session_scope_tests.py',
 'tests/measurement_target_scope.py','tests/measurement_target_scope_tests.py'}
OLD_PINS={'dependencies/measurement-target-baseline.json','dependencies/capabilities-runtime-baseline.json','dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json'}
def digest(b):return hashlib.sha256(b.replace(b'\r\n',b'\n')).hexdigest()
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',BASE+':'+name])
@lru_cache(maxsize=None)
def existed(repo,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',BASE+':'+name],capture_output=True).returncode==0
def approved_session(root,git_root=None):
 root=Path(root);repo=str(git_root or root);p=json.loads((root/PIN).read_bytes())
 if p['base_commit']!=BASE or set(p['files'])!=FILES:raise RuntimeError('B2 scope/base mismatch')
 for n in OLD_PINS:
  if digest((root/n).read_bytes())!=digest(historical(repo,n)):raise RuntimeError('B2 historical pin changed: '+n)
 for n,e in p['files'].items():
  if digest((root/n).read_bytes())!=e['sha256']:raise RuntimeError('B2 unreviewed current source: '+n)
  if existed(repo,n)!=e['existed_at_base']:raise RuntimeError('B2 predecessor presence: '+n)
  if e['existed_at_base'] and digest(historical(repo,n))!=e['base_sha256']:raise RuntimeError('B2 predecessor hash: '+n)
 return FILES|{PIN}
def verify_diff_scope(root):
 names=subprocess.check_output(['git','-C',str(root),'diff','--name-only',BASE,'--']).decode().splitlines()
 names+=subprocess.check_output(['git','-C',str(root),'ls-files','--others','--exclude-standard']).decode().splitlines()
 if set(names)-FILES-{PIN}:raise RuntimeError('outside B2 scope: '+','.join(sorted(set(names)-FILES-{PIN})))
def verify_legacy_bytes(root):
 root=Path(root);proof={}
 for n in ['include/monaka_bridge/measurement_target.hpp','src/calibration/measurement_target.cpp','include/monaka_bridge/trajectory_calibration.hpp','src/calibration/trajectory_calibration.cpp','src/calibration/world_calibration.cpp','src/calibration/calibration.cpp','src/mapping/config.cpp','src/mapping/config_transaction.cpp','src/routing/bridge.cpp','src/routing/runtime_normalization.cpp']:
  b=(root/n).read_bytes().replace(b'\r\n',b'\n');old=historical(str(root),n).replace(b'\r\n',b'\n')
  if b!=old:raise RuntimeError('B2 changed protected source: '+n)
  proof[n]=dict(bytes=len(b),sha256=digest(b),equal=True)
 n='tools/steamvr_calibrator.cpp';raw=(root/n).read_bytes().replace(b'\r\n',b'\n')
 old=historical(str(root),n).replace(b'\r\n',b'\n')
 raw=raw.replace(b'#include "monaka_bridge/calibration_session.hpp"\n',b'',1).replace(b'    const auto localApi=mb::local_calibration::calibrationLocalDispatch(argc,argv);\n    if(localApi>=0)return localApi;\n',b'',1)
 if raw!=old:raise RuntimeError('B2 changed legacy CLI beyond pre-dispatch')
 proof['legacy-cli-after-exact-predispatch-removal']=dict(bytes=len(old),sha256=digest(old),equal=True)
 cmake=(root/'CMakeLists.txt').read_text();assert 'tests/calibration_session_process_fixture.cpp' in cmake
 assert 'monaka_bridge_calibrator PRIVATE monaka_bridge_core monaka_bridge_transport openvr_api monaka_calibration_session' in cmake
 return proof
def predecessor(root,name,raw,git_root=None):
 root=Path(root);p=json.loads((root/PIN).read_bytes());e=p['files'].get(name)
 if not e:return raw
 if e['existed_at_base'] and digest(raw)==e['base_sha256']:return raw
 if digest(raw)!=e['sha256']:raise RuntimeError('B2 unreviewed projection: '+name)
 return historical(str(git_root or root),name) if e['existed_at_base'] else raw
def predecessor_text(root,text):
 root=Path(root);approved_session(root);p=json.loads((root/PIN).read_bytes())
 for n,e in p['files'].items():
  if e['existed_at_base'] and digest(text.encode())==e['sha256']:return historical(str(root),n).decode()
 return text
