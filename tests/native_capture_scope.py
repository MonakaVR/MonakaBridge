"""N1 exact current-source layer; immutable N0b and older pins are raw bytes."""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
BASE='959a33e0847007b1d2ab7c5c7c8a12f8ea6554c4'
PIN='dependencies/native-capture-baseline.json'
FILES={'CMakeLists.txt','tools/calibration_local_api.cpp',
 'include/monaka_bridge/calibration_runtime_provider.hpp',
 'src/calibration/calibration_runtime_provider.cpp','src/calibration/calibration_production_entry.cpp',
 'dependencies/calibration-openvr-sdk.json','scripts/verify_calibration_sdk.py',
 'tests/calibration_runtime_fake.hpp','tests/calibration_runtime_provider_tests.cpp',
 'tests/calibration_runtime_process_fixture.cpp','tests/trajectory_cli_sentinel.py',
 'tests/calibration_session_process.py','tests/calibration_capture_monitor_tests.cpp',
 'tests/continuous_monitor_scope.py','tests/continuous_monitor_scope_tests.py',
 'tests/native_capture_scope.py','tests/native_capture_scope_tests.py','docs/native-calibration-capture.md'}
OLD_PINS={'dependencies/calibration-session-baseline.json','dependencies/measurement-target-baseline.json',
 'dependencies/capabilities-runtime-baseline.json','dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json',
 'dependencies/provider-options-baseline.json','dependencies/writer-fixture-baseline.json',
 'dependencies/writer-fixture-n0a-baseline.json','dependencies/continuous-monitor-baseline.json'}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',BASE+':'+name])
@lru_cache(maxsize=None)
def existed(repo,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',BASE+':'+name],capture_output=True).returncode==0
def approved_native_capture(root,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return set()
 repo=str(git_root or root);p=json.loads((root/PIN).read_bytes())
 if p['base_commit']!=BASE or set(p['files'])!=FILES:raise RuntimeError('N1 scope/base mismatch')
 for name in OLD_PINS:
  if (root/name).read_bytes()!=historical(repo,name):raise RuntimeError('N1 immutable old pin changed: '+name)
 for name,e in p['files'].items():
  if digest((root/name).read_bytes())!=e['sha256']:raise RuntimeError('N1 unreviewed current source: '+name)
  if existed(repo,name)!=e['existed_at_base']:raise RuntimeError('N1 predecessor presence: '+name)
  if e['existed_at_base'] and digest(historical(repo,name))!=e['base_sha256']:raise RuntimeError('N1 predecessor hash: '+name)
 return FILES|{PIN}
def predecessor(root,name,raw,git_root=None,allow_older=False):
 root=Path(root)
 if not (root/PIN).exists():return raw
 e=json.loads((root/PIN).read_bytes())['files'].get(name)
 if not e:return raw
 if digest(raw)==e['sha256']:return historical(str(git_root or root),name) if e['existed_at_base'] else raw
 if e['existed_at_base'] and digest(raw)==e['base_sha256']:return raw
 # An older parent may receive its own already-projected historical bytes.
 # It must validate them against its own immutable manifest. Current approval
 # above always reads/validates actual on-disk bytes before any projection.
 if allow_older and e['existed_at_base']:return raw
 raise RuntimeError('N1 unreviewed projection: '+name)
def predecessor_text(root,text):
 root=Path(root)
 if not (root/PIN).exists():return text
 approved_native_capture(root)
 for name,e in json.loads((root/PIN).read_bytes())['files'].items():
  if e['existed_at_base'] and digest(text.encode())==e['sha256']:return historical(str(root),name).decode()
 return text
def verify_diff_scope(root):
 names=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'diff','--name-only',BASE,'--']).decode().splitlines()
 names+=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'ls-files','--others','--exclude-standard']).decode().splitlines()
 unexpected=set(names)-approved_native_capture(root)
 if unexpected:raise RuntimeError('outside N1 scope: '+','.join(sorted(unexpected)))
