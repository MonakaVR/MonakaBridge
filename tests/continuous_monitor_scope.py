"""N0b exact current-source layer over accepted N0a; old pins stay raw immutable."""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
from native_capture_scope import approved_native_capture,predecessor as native_predecessor,predecessor_text as native_predecessor_text
BASE='29245bd885ffd502df52a9a654887aed208d3bf2'
PIN='dependencies/continuous-monitor-baseline.json'
FILES={'include/monaka_bridge/calibration_session.hpp','include/monaka_bridge/calibration_capture_monitor.hpp',
 'src/calibration/calibration_session.cpp','src/calibration/calibration_capture_monitor.cpp','tools/calibration_local_api.cpp',
 'tests/calibration_fixture_provider.hpp','tests/calibration_session_tests.cpp','tests/calibration_session_process_fixture.cpp',
 'tests/calibration_capture_monitor_tests.cpp','tests/calibration_capture_monitor_process.py',
 'tests/continuous_monitor_scope.py','tests/continuous_monitor_scope_tests.py',
 'tests/writer_fixture_scope.py','tests/writer_fixture_scope_tests.py','CMakeLists.txt','docs/continuous-capture-monitor.md'}
OLD_PINS={'dependencies/calibration-session-baseline.json','dependencies/measurement-target-baseline.json',
 'dependencies/capabilities-runtime-baseline.json','dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json',
 'dependencies/provider-options-baseline.json','dependencies/writer-fixture-baseline.json','dependencies/writer-fixture-n0a-baseline.json'}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',BASE+':'+name])
@lru_cache(maxsize=None)
def existed(repo,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',BASE+':'+name],capture_output=True).returncode==0
def approved_continuous_monitor(root,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return set()
 repo=str(git_root or root);extra=approved_native_capture(root,git_root);p=json.loads((root/PIN).read_bytes())
 if p['base_commit']!=BASE or set(p['files'])!=FILES:raise RuntimeError('N0b scope/base mismatch')
 for name in OLD_PINS:
  if (root/name).read_bytes()!=historical(repo,name):raise RuntimeError('N0b old pin changed: '+name)
 for name,e in p['files'].items():
  if digest(native_predecessor(root,name,(root/name).read_bytes(),git_root))!=e['sha256']:raise RuntimeError('N0b unreviewed current source: '+name)
  if existed(repo,name)!=e['existed_at_base']:raise RuntimeError('N0b predecessor presence: '+name)
  if e['existed_at_base'] and digest(historical(repo,name))!=e['base_sha256']:raise RuntimeError('N0b predecessor hash: '+name)
 return FILES|{PIN}|extra
def predecessor(root,name,raw,git_root=None):
 root=Path(root);raw=native_predecessor(root,name,raw,git_root,allow_older=True)
 if not (root/PIN).exists():return raw
 e=json.loads((root/PIN).read_bytes())['files'].get(name)
 if not e:return raw
 if e['existed_at_base'] and digest(raw)==e['base_sha256']:return raw
 if digest(raw)!=e['sha256']:raise RuntimeError('N0b unreviewed projection: '+name)
 return historical(str(git_root or root),name) if e['existed_at_base'] else raw
def predecessor_text(root,text):
 root=Path(root);text=native_predecessor_text(root,text)
 if not (root/PIN).exists():return text
 approved_continuous_monitor(root)
 for name,e in json.loads((root/PIN).read_bytes())['files'].items():
  if e['existed_at_base'] and digest(text.encode())==e['sha256']:return historical(str(root),name).decode()
 return text
def verify_diff_scope(root):
 names=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'diff','--name-only',BASE,'--']).decode().splitlines()
 names+=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'ls-files','--others','--exclude-standard']).decode().splitlines()
 unexpected=set(names)-approved_continuous_monitor(root)
 if unexpected:raise RuntimeError('outside N0b scope: '+','.join(sorted(unexpected)))
