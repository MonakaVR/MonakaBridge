"""J0 actual composite bytes, explicit native/control views; no parent imports."""
from pathlib import Path
from functools import lru_cache
import ast,hashlib,json,subprocess
PIN='dependencies/native-control-integration-baseline.json'
COMMON_BASE='279a01363f47ad3564b5cd8b9346180c49ee41d3'
PARENTS={'native': 'bf637c228c2f6fd4d972614660386702e194bdbb', 'control': '1b02d3628e11254bb0584e546c8a17713a6e6e26'}
FILES={
 'CMakeLists.txt',
 'dependencies/calibration-openvr-sdk.json',
 'docs/bridge-control-foundation.md',
 'docs/calibration-session-contract.md',
 'docs/continuous-capture-monitor.md',
 'docs/measurement-target.md',
 'docs/native-calibration-capture.md',
 'docs/native-control-integration.md',
 'docs/provider-open-options.md',
 'docs/writer-fixture-events.md',
 'docs/ws01a-audit-correction.md',
 'include/monaka_bridge/calibration_capture_monitor.hpp',
 'include/monaka_bridge/calibration_runtime_provider.hpp',
 'include/monaka_bridge/calibration_session.hpp',
 'include/monaka_bridge/measurement_target.hpp',
 'include/monaka_bridge/trajectory_calibration.hpp',
 'scripts/build_gui.ps1',
 'scripts/package_control.ps1',
 'scripts/verify_calibration_sdk.py',
 'src/calibration/calibration_capture_monitor.cpp',
 'src/calibration/calibration_drafts.cpp',
 'src/calibration/calibration_production_entry.cpp',
 'src/calibration/calibration_runtime_provider.cpp',
 'src/calibration/calibration_session.cpp',
 'src/calibration/measurement_target.cpp',
 'src/calibration/trajectory_calibration.cpp',
 'tests/calibration_capture_monitor_process.py',
 'tests/calibration_capture_monitor_tests.cpp',
 'tests/calibration_fixture_provider.hpp',
 'tests/calibration_review_events.hpp',
 'tests/calibration_review_events.py',
 'tests/calibration_runtime_fake.hpp',
 'tests/calibration_runtime_process_fixture.cpp',
 'tests/calibration_runtime_provider_tests.cpp',
 'tests/calibration_session_process.py',
 'tests/calibration_session_process_fixture.cpp',
 'tests/calibration_session_scope.py',
 'tests/calibration_session_scope_tests.py',
 'tests/calibration_session_tests.cpp',
 'tests/capability_runtime_scope.py',
 'tests/capability_runtime_scope_tests.py',
 'tests/config_write_scope_tests.py',
 'tests/continuous_monitor_scope.py',
 'tests/continuous_monitor_scope_tests.py',
 'tests/control_foundation_scope.py',
 'tests/control_foundation_scope_tests.py',
 'tests/control_foundation_tests.cs',
 'tests/control_foundation_tests.ps1',
 'tests/control_helper_fixture.cs',
 'tests/control_packaged_smoke.py',
 'tests/integration_scope.py',
 'tests/integration_scope_tests.py',
 'tests/measurement_target_scope.py',
 'tests/measurement_target_scope_tests.py',
 'tests/measurement_target_tests.cpp',
 'tests/native_capture_scope.py',
 'tests/native_capture_scope_tests.py',
 'tests/openvr_bootstrap_sentinel.cpp',
 'tests/provider_options_scope.py',
 'tests/provider_options_scope_tests.py',
 'tests/trajectory_cli_sentinel.py',
 'tests/ui_production_tests.cs',
 'tests/writer_fixture_scope.py',
 'tests/writer_fixture_scope_tests.py',
 'tools/calibration_local_api.cpp',
 'tools/steamvr_calibrator.cpp',
 'ui/control_models.cs',
 'ui/installation_control.cs',
 'ui/steamvr_control_gui_main.cs',
 'ui/steamvr_control_tray.cs',
 'ui/steamvr_control_wpf.cs',
}
PIN_OWNERS={'dependencies/calibration-session-baseline.json': ['native'], 'dependencies/measurement-target-baseline.json': ['native'], 'dependencies/capabilities-runtime-baseline.json': ['native', 'control'], 'dependencies/config-write-baseline.json': ['native', 'control'], 'dependencies/p2r-runtime-baseline.json': ['native', 'control'], 'dependencies/provider-options-baseline.json': ['native'], 'dependencies/writer-fixture-baseline.json': ['native'], 'dependencies/writer-fixture-n0a-baseline.json': ['native'], 'dependencies/continuous-monitor-baseline.json': ['native'], 'dependencies/native-capture-baseline.json': ['native'], 'dependencies/control-foundation-baseline.json': ['control']}
OVERLAP=['tests/capability_runtime_scope.py','tests/capability_runtime_scope_tests.py','tests/config_write_scope_tests.py']

PROTECTED_PARENTS={'include/monaka_bridge/calibration_capture_monitor.hpp': 'native', 'include/monaka_bridge/calibration_runtime_provider.hpp': 'native', 'include/monaka_bridge/calibration_session.hpp': 'native', 'include/monaka_bridge/measurement_target.hpp': 'native', 'include/monaka_bridge/trajectory_calibration.hpp': 'native', 'scripts/build_gui.ps1': 'control', 'scripts/package_control.ps1': 'control', 'src/calibration/calibration_capture_monitor.cpp': 'native', 'src/calibration/calibration_drafts.cpp': 'native', 'src/calibration/calibration_production_entry.cpp': 'native', 'src/calibration/calibration_runtime_provider.cpp': 'native', 'src/calibration/calibration_session.cpp': 'native', 'src/calibration/measurement_target.cpp': 'native', 'src/calibration/trajectory_calibration.cpp': 'native', 'tools/calibration_local_api.cpp': 'native', 'tools/steamvr_calibrator.cpp': 'native', 'ui/control_models.cs': 'control', 'ui/installation_control.cs': 'control', 'ui/steamvr_control_gui_main.cs': 'control', 'ui/steamvr_control_tray.cs': 'control', 'ui/steamvr_control_wpf.cs': 'control', 'src/calibration/world_calibration.cpp': 'native', 'src/mapping/config_transaction.cpp': 'native', 'src/routing/runtime_normalization.cpp': 'native', 'src/routing/bridge.cpp': 'native'}
FILES|=set(PROTECTED_PARENTS)

def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def read(root,name):
 try:return (Path(root)/name).read_bytes()
 except OSError as e:raise RuntimeError('J0 required current file missing/unreadable: '+name) from e
@lru_cache(maxsize=None)
def historical(repo,view,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',PARENTS[view]+':'+name])
@lru_cache(maxsize=None)
def existed(repo,view,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',PARENTS[view]+':'+name],capture_output=True).returncode==0
@lru_cache(maxsize=None)
def parent_base(repo):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'merge-base',*PARENTS.values()],text=True).strip()
def tree(root,name):
 try:return ast.parse(read(root,name).decode('utf-8-sig'))
 except (SyntaxError,UnicodeError) as e:raise RuntimeError('J0 invalid current Python source: '+name) from e
def function(module,name):
 found=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name==name]
 if len(found)!=1:raise RuntimeError('J0 missing/ambiguous connection: '+name)
 return found[0]
def expression(s):return ast.dump(ast.parse(s,mode='eval').body,include_attributes=False)
def verify_connections(root):
 # Current connector semantics remain independently checked even if a mutant
 # changes its metadata hash to match a deleted delegation.
 module=tree(root,'tests/capability_runtime_scope.py');fn=function(module,'approved_capabilities')
 extras=[n.value for n in ast.walk(fn) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='extra' for t in n.targets)]
 if len(extras)!=1 or ast.dump(extras[0],include_attributes=False)!=expression('approved_measurement(root,git_root)|approved_control(root,git_root)'):raise RuntimeError('J0 lost native/control approval delegation')
 for path,view,entry in [('tests/native_capture_scope.py','native','approved_native_capture'),('tests/control_foundation_scope.py','control','approved_control')]:
  module=tree(root,path);fn=function(module,entry)
  if not any(isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id=='approved_integration' for n in ast.walk(fn)):raise RuntimeError('J0 lost current-tree approval connection: '+view)
  calls=[n for n in ast.walk(module) if isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id=='integration_project']
  if len(calls)<2 or any(len(n.args)<2 or not isinstance(n.args[1],ast.Constant) or n.args[1].value!=view for n in calls):raise RuntimeError('J0 wrong explicit projection view: '+view)
 module=tree(root,'tests/integration_scope.py');imports={n.module for n in ast.walk(module) if isinstance(n,ast.ImportFrom)}
 imports|={a.name for n in ast.walk(module) if isinstance(n,ast.Import) for a in n.names}
 if not imports<={'pathlib','functools','ast','hashlib','json','subprocess'}:raise RuntimeError('J0 leaf imports a parent/cycle')
def approved_integration(root,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return set()
 repo=str(git_root or root);p=json.loads(read(root,PIN))
 if set(p)!={'common_base','parents','files','pins','overlap'} or p['common_base']!=COMMON_BASE or p['parents']!=PARENTS or set(p['files'])!=FILES or set(p['pins'])!=set(PIN_OWNERS) or p['overlap']!=OVERLAP:raise RuntimeError('J0 exact scope/parents/overlap mismatch')
 if parent_base(repo)!=COMMON_BASE:raise RuntimeError('J0 parent ancestry/base mismatch')
 for name,owners in PIN_OWNERS.items():
  if set(p['pins'][name])!=set(owners):raise RuntimeError('J0 pin owner mismatch: '+name)
  for view in owners:
   raw=historical(repo,view,name)
   if hashlib.sha256(raw).hexdigest()!=p['pins'][name][view] or read(root,name)!=raw:raise RuntimeError('J0 immutable parent pin changed: '+name)
 for name,e in p['files'].items():
  if set(e)!={'sha256','parents'} or digest(read(root,name))!=e['sha256'] or set(e['parents'])!=set(PARENTS):raise RuntimeError('J0 unreviewed current composite: '+name)
  for view,v in e['parents'].items():
   exists=existed(repo,view,name)
   if type(v['existed']) is not bool or exists!=v['existed'] or set(v)!=({'existed','sha256'} if exists else {'existed'}):raise RuntimeError('J0 parent presence: '+view+':'+name)
   if exists and digest(historical(repo,view,name))!=v['sha256']:raise RuntimeError('J0 parent blob hash: '+view+':'+name)
 for name,view in PROTECTED_PARENTS.items():
  if digest(read(root,name))!=digest(historical(repo,view,name)):raise RuntimeError('J0 protected product source changed: '+name)
 verify_connections(root)
 return FILES|set(PIN_OWNERS)|{PIN}
def project(root,view,name,raw,git_root=None,allow_older=False):
 root=Path(root)
 if view not in PARENTS:raise RuntimeError('J0 unknown parent view')
 if not (root/PIN).exists():return raw
 e=json.loads(read(root,PIN))['files'].get(name)
 if not e:return raw
 parent=e['parents'][view]
 if digest(raw)==e['sha256']:return historical(str(git_root or root),view,name) if parent['existed'] else raw
 if parent['existed'] and digest(raw)==parent['sha256']:return raw
 # Only historical parents may receive an already-projected older blob. Their
 # own immutable validator still checks it. Public direct projection is strict.
 if allow_older and parent['existed']:return raw
 raise RuntimeError('J0 unknown/foreign projection: '+view+':'+name)
def project_text(root,view,text):
 root=Path(root)
 if not (root/PIN).exists():return text
 approved_integration(root);matches=set()
 for name,e in json.loads(read(root,PIN))['files'].items():
  if digest(text.encode())==e['sha256'] and e['parents'][view]['existed']:matches.add(historical(str(root),view,name).decode())
 if len(matches)>1:raise RuntimeError('J0 ambiguous text projection')
 return next(iter(matches)) if matches else text
def verify_diff_scope(root):
 names=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'diff','--name-only',COMMON_BASE,'--']).decode().splitlines()
 names+=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'ls-files','--others','--exclude-standard']).decode().splitlines()
 unexpected=set(names)-approved_integration(root)
 if unexpected:raise RuntimeError('outside J0 composite scope: '+','.join(sorted(unexpected)))
