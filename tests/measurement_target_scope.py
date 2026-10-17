"""Exact B1 configuration-freeze layer; historical runtime/config pins immutable."""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
from calibration_session_scope import approved_session,predecessor as session_predecessor,predecessor_text as session_predecessor_text
BASE='279a01363f47ad3564b5cd8b9346180c49ee41d3'
PIN='dependencies/measurement-target-baseline.json'
FILES={
 'include/monaka_bridge/measurement_target.hpp','src/calibration/measurement_target.cpp',
 'include/monaka_bridge/trajectory_calibration.hpp','src/calibration/trajectory_calibration.cpp',
 'tests/measurement_target_tests.cpp','CMakeLists.txt',
 'tests/measurement_target_scope.py','tests/measurement_target_scope_tests.py',
 'tests/capability_runtime_scope.py','tests/capability_runtime_scope_tests.py',
 'tests/config_write_scope_tests.py','docs/measurement-target.md'}
OLD_PINS={'dependencies/capabilities-runtime-baseline.json','dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json'}
# A new native field requires explicit snapshot/coverage review, not a silent
# claim that a previously reviewed field list still covers the changed model.
UNCHANGED={'include/monaka_bridge/config.hpp','include/monaka_bridge/calibration.hpp',
 'include/monaka_bridge/registry.hpp','include/monaka_bridge/world_calibration.hpp',
 'src/calibration/world_calibration.cpp'}

def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',BASE+':'+name])
@lru_cache(maxsize=None)
def existed(repo,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',BASE+':'+name],capture_output=True).returncode==0

def approved_measurement(root,git_root=None):
 root=Path(root);repo=str(git_root or root);p=json.loads((root/PIN).read_text(encoding='utf-8'))
 extra=approved_session(root,git_root)
 if p['base_commit']!=BASE or set(p['files'])!=FILES:raise RuntimeError('B1 scope/base mismatch')
 for name in OLD_PINS|UNCHANGED:
  if digest((root/name).read_bytes())!=digest(historical(repo,name)):raise RuntimeError('B1 protected input changed: '+name)
 for name,e in p['files'].items():
  if digest(session_predecessor(root,name,(root/name).read_bytes(),git_root))!=e['sha256']:raise RuntimeError('unreviewed B1 drift: '+name)
  if existed(repo,name)!=e['existed_at_base']:raise RuntimeError('B1 predecessor presence drift: '+name)
  if e['existed_at_base'] and digest(historical(repo,name))!=e['base_sha256']:raise RuntimeError('B1 predecessor drift: '+name)
 return FILES|{PIN}|extra

def verify_diff_scope(root):
 names=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'diff','--name-only',BASE,'--']).decode().splitlines()
 names+=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'ls-files','--others','--exclude-standard']).decode().splitlines()
 unexpected=set(names)-FILES-{PIN}-approved_session(root)
 if unexpected:raise RuntimeError('outside B1 scope: '+','.join(sorted(unexpected)))

def verify_solver_bytes(root):
 root=Path(root)
 name='src/calibration/trajectory_calibration.cpp'
 old=historical(str(root),name).replace(b'\r\n',b'\n');new=(root/name).read_bytes().replace(b'\r\n',b'\n')
 # Only the new ctor/checkConfig can differ. Preserve validator, legacy ctor,
 # require(), and every math/recording/quality/pairing/default byte separately.
 start=b'void validateTrajectoryTarget(';tail=b'bool TrajectoryRecorder::observation('
 ctor=b'TrajectoryRecorder::TrajectoryRecorder(';require=b'void TrajectoryRecorder::require('
 check=b'void TrajectoryRecorder::checkConfig('
 pieces={
  'trajectory-math':(old.split(start)[0],new.split(start)[0]),
  'legacy-validator':(old.split(start)[1].split(ctor)[0],new.split(start)[1].split(ctor)[0]),
  'legacy-constructor':(old.split(ctor)[1].split(require)[0],new.split(ctor)[1]),
  'sticky-failure':(old.split(require)[1].split(check)[0],new.split(require)[1].split(check)[0]),
  'observation-state-pairing-quality-solve':(old.split(tail)[1],new.split(tail)[1])}
 header='include/monaka_bridge/trajectory_calibration.hpp'
 a=historical(str(root),header).replace(b'\r\n',b'\n');b=(root/header).read_bytes().replace(b'\r\n',b'\n')
 pieces['trajectory-numeric-defaults-and-solver-api']=(a.split(b'struct TrajectoryOptions')[1].split(b'class TrajectoryRecorder')[0],b.split(b'struct TrajectoryOptions')[1].split(b'class TrajectoryRecorder')[0])
 for name,(a,b) in pieces.items():
  if a!=b:raise RuntimeError('B1 changed protected solver/legacy bytes: '+name)
 return {name:{'sha256':hashlib.sha256(a).hexdigest(),'bytes':len(a),'equal':True} for name,(a,b) in pieces.items()}

def predecessor(root,name,raw,git_root=None):
 root=Path(root);raw=session_predecessor(root,name,raw,git_root);p=json.loads((root/PIN).read_text(encoding='utf-8'));e=p['files'].get(name)
 if not e:return raw
 if e['existed_at_base'] and digest(raw)==e['base_sha256']:return raw
 if digest(raw)!=e['sha256']:raise RuntimeError('unreviewed B1 drift: '+name)
 return historical(str(git_root or root),name) if e['existed_at_base'] else raw

def predecessor_text(root,text):
 root=Path(root);text=session_predecessor_text(root,text);approved_measurement(root);p=json.loads((root/PIN).read_text(encoding='utf-8'))
 for name,e in p['files'].items():
  if e['existed_at_base'] and digest(text.encode())==e['sha256']:return historical(str(root),name).decode()
 return text
