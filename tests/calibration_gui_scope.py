"""U1/U2 exact current bytes over literal J0; no historical validators imported."""
from pathlib import Path
from functools import lru_cache
import ast,hashlib,json,subprocess
PARENT='9dd8ac0a562f27460358d822ad9b57b7e59329d4'
PIN='dependencies/calibration-gui-baseline.json'
FILES={
 'CMakeLists.txt','scripts/build_gui.ps1','scripts/control_gui.cmake',
 'scripts/build_control_toolset.py','scripts/package_calibration_control.py','scripts/verify_control_toolset.py',
 'src/calibration/calibration_runtime_provider.cpp','src/calibration/calibration_session.cpp','tools/calibration_local_api.cpp',
 'tests/calibration_runtime_fake.hpp','tests/calibration_runtime_process_fixture.cpp','tests/calibration_runtime_provider_tests.cpp',
 'tests/integration_scope.py','tests/native_capture_scope_tests.py','tests/calibration_gui_scope.py','tests/calibration_gui_scope_tests.py','tests/run_calibration_managed.py',
 'tests/calibration_client_process_tests.cs','tests/calibration_job_fixture.cs','tests/calibration_job_tests.cs',
 'tests/calibration_json_tests.cs','tests/calibration_launch_tests.cs','tests/calibration_model_tests.cs','tests/calibration_profile_tests.cs',
 'tests/calibration_protocol_fault_fixture.cs','tests/calibration_wpf_tests.cs',
 'ui/installation_control.cs','ui/steamvr_control_tray.cs','ui/steamvr_control_wpf.cs',
 'ui/calibration_json.cs','ui/calibration_models.cs','ui/calibration_owned_process.cs','ui/calibration_panel.cs',
 'ui/calibration_pipe_frames.cs','ui/calibration_process_client.cs','ui/calibration_profile.cs','ui/calibration_profile_dialog.cs',
 'docs/calibration-gui.md',
}
PINS={
 'dependencies/native-control-integration-baseline.json','dependencies/calibration-session-baseline.json',
 'dependencies/measurement-target-baseline.json','dependencies/capabilities-runtime-baseline.json',
 'dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json','dependencies/provider-options-baseline.json',
 'dependencies/writer-fixture-baseline.json','dependencies/writer-fixture-n0a-baseline.json','dependencies/continuous-monitor-baseline.json',
 'dependencies/native-capture-baseline.json','dependencies/control-foundation-baseline.json',
}
PROTECTED={
 'src/calibration/calibration_capture_monitor.cpp','src/calibration/calibration_drafts.cpp',
 'src/calibration/calibration_production_entry.cpp','src/calibration/measurement_target.cpp',
 'src/calibration/trajectory_calibration.cpp','src/calibration/world_calibration.cpp',
 'src/mapping/config_transaction.cpp','src/routing/runtime_normalization.cpp','src/routing/bridge.cpp','tools/steamvr_calibrator.cpp',
 'include/monaka_bridge/calibration_capture_monitor.hpp','include/monaka_bridge/calibration_runtime_provider.hpp',
 'include/monaka_bridge/calibration_session.hpp','include/monaka_bridge/trajectory_calibration.hpp','ui/control_models.cs',
}
NATIVE_PROTECTED=['tools/steamvr_calibrator.cpp','src/calibration/world_calibration.cpp',
 'src/calibration/trajectory_calibration.cpp','src/calibration/calibration_session.cpp',
 'src/calibration/calibration_capture_monitor.cpp','src/mapping/config_transaction.cpp',
 'src/routing/runtime_normalization.cpp','src/routing/bridge.cpp']
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def read(root,name):
 try:return (Path(root)/name).read_bytes()
 except OSError as e:raise RuntimeError('GUI required actual input missing: '+name) from e
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',PARENT+':'+name])
@lru_cache(maxsize=None)
def existed(repo,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',PARENT+':'+name],capture_output=True).returncode==0
def verify_connections(root):
 module=ast.parse(read(root,'tests/integration_scope.py').decode('utf-8-sig'))
 imported={a.name for n in module.body if isinstance(n,ast.ImportFrom) and n.module=='calibration_gui_scope' for a in n.names}
 if imported!={'approved_gui','gui_project','gui_project_text'}:raise RuntimeError('GUI lost exact child import')
 for name,required in [('read','gui_project'),('approved_integration','approved_gui'),('project','gui_project'),('project_text','gui_project_text')]:
  found=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name==name]
  if len(found)!=1 or not any(isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id==required for n in ast.walk(found[0])):raise RuntimeError('GUI lost delegation: '+name)
 fn=next(n for n in module.body if isinstance(n,ast.FunctionDef) and n.name=='approved_integration')
 if not any(isinstance(n,ast.Return) and isinstance(n.value,ast.BinOp) and ast.dump(n.value,include_attributes=False)==ast.dump(ast.parse('FILES|set(PIN_OWNERS)|{PIN}|extra',mode='eval').body,include_attributes=False) for n in ast.walk(fn)):raise RuntimeError('GUI lost approval union')
 native=ast.parse(read(root,'tests/native_capture_scope_tests.py').decode('utf-8-sig'))
 imported={a.name for n in native.body if isinstance(n,ast.ImportFrom) and n.module=='calibration_gui_scope' for a in n.names}
 if imported!={'approved_gui','gui_project'}:raise RuntimeError('GUI lost native test child import')
 if not any(isinstance(n,ast.Expr) and isinstance(n.value,ast.Call) and ast.dump(n.value,include_attributes=False)==ast.dump(ast.parse('approved_gui(root)',mode='eval').body,include_attributes=False) for n in native.body):raise RuntimeError('GUI lost actual native test approval')
 declarations=[n for n in native.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='protected' for t in n.targets)]
 if len(declarations)!=1 or ast.literal_eval(declarations[0].value)!=NATIVE_PROTECTED:raise RuntimeError('GUI lost native protected source list')
 expected='approved_bytes.replace(b"\\r\\n",b"\\n")==historical(str(root),name).replace(b"\\r\\n",b"\\n")'
 if not any(isinstance(n,ast.Assert) and ast.dump(n.test,include_attributes=False)==ast.dump(ast.parse(expected,mode='eval').body,include_attributes=False) for n in ast.walk(native)):raise RuntimeError('GUI lost native historical equality')
 if not any(isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='approved_bytes' for t in n.targets) and ast.dump(n.value,include_attributes=False)==ast.dump(ast.parse('gui_project(root,name,(root/name).read_bytes())',mode='eval').body,include_attributes=False) for n in ast.walk(native)):raise RuntimeError('GUI lost native explicit projection')
 leaf=ast.parse(read(root,'tests/calibration_gui_scope.py').decode('utf-8-sig'))
 imports={n.module for n in ast.walk(leaf) if isinstance(n,ast.ImportFrom)}|{a.name for n in ast.walk(leaf) if isinstance(n,ast.Import) for a in n.names}
 if not imports<={'pathlib','functools','ast','hashlib','json','subprocess'}:raise RuntimeError('GUI leaf imports historical validator/cycle')
def approved_gui(root,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return set()
 repo=str(git_root or root);p=json.loads(read(root,PIN))
 if set(p)!={'parent','files','pins','protected'} or p['parent']!=PARENT or set(p['files'])!=FILES or set(p['pins'])!=PINS or set(p['protected'])!=PROTECTED:raise RuntimeError('GUI exact scope/parent mismatch')
 for name in sorted(PINS):
  raw=historical(repo,name)
  if read(root,name)!=raw or hashlib.sha256(raw).hexdigest()!=p['pins'][name]:raise RuntimeError('GUI immutable pin drift: '+name)
 for name,e in p['files'].items():
  if set(e)!={'sha256','parent'} or digest(read(root,name))!=e['sha256']:raise RuntimeError('GUI unreviewed actual bytes: '+name)
  parent=e['parent'];exists=existed(repo,name)
  if set(parent)!=({'existed','sha256'} if exists else {'existed'}) or type(parent['existed']) is not bool or parent['existed']!=exists:raise RuntimeError('GUI predecessor presence: '+name)
  if exists and digest(historical(repo,name))!=parent['sha256']:raise RuntimeError('GUI predecessor hash: '+name)
 for name,sha in p['protected'].items():
  if digest(read(root,name))!=sha or digest(historical(repo,name))!=sha:raise RuntimeError('GUI protected source changed: '+name)
 verify_connections(root)
 return FILES|PINS|PROTECTED|{PIN}
def gui_project(root,name,raw,git_root=None,allow_older=False):
 root=Path(root)
 if not (root/PIN).exists():return raw
 e=json.loads(read(root,PIN))['files'].get(name)
 if not e:return raw
 if digest(raw)==e['sha256']:return historical(str(git_root or root),name) if e['parent']['existed'] else raw
 if e['parent']['existed'] and digest(raw)==e['parent']['sha256']:return raw
 if allow_older and e['parent']['existed']:return raw
 raise RuntimeError('GUI unknown/foreign projection: '+name)
def gui_project_text(root,text):
 root=Path(root)
 if not (root/PIN).exists():return text
 approved_gui(root);matches=set()
 for name,e in json.loads(read(root,PIN))['files'].items():
  if digest(text.encode())==e['sha256'] and e['parent']['existed']:matches.add(historical(str(root),name).decode())
 if len(matches)>1:raise RuntimeError('GUI ambiguous text projection')
 return next(iter(matches)) if matches else text
