"""B4 exact current-source leaf; predecessor pins are immutable evidence."""
from pathlib import Path
from functools import lru_cache
import ast,hashlib,json,re,subprocess
PARENT='aff03d248ae9abbf6b32857705ffcef98aab8514'
PIN='dependencies/calibration-b4-baseline.json'
FILES={
 'CMakeLists.txt','scripts/control_gui.cmake','scripts/build_control_toolset.py','scripts/package_calibration_control.py',
 'include/monaka_bridge/calibration_session.hpp','include/monaka_bridge/calibration_capture_monitor.hpp',
 'include/monaka_bridge/static_calibration_session.hpp',
 'src/calibration/calibration_session.cpp','src/calibration/calibration_capture_monitor.cpp',
 'src/calibration/calibration_runtime_provider.cpp','src/calibration/static_calibration_session.cpp',
 'ui/calibration_models.cs','ui/calibration_process_client.cs','ui/calibration_panel.cs',
 'tests/calibration_runtime_process_fixture.cpp','tests/run_calibration_managed.py',
 'tests/calibration_b4_fixture.hpp','tests/calibration_b4_tests.cpp','tests/calibration_b4_gui_tests.cs','tests/calibration_b4_product_process.py',
 'tests/calibration_b4_scope.py','tests/calibration_b4_scope_tests.py',
 'tests/calibration_review_r1_scope.py','tests/calibration_review_r1_scope_tests.py',
 'docs/calibration-b4.md',
}
PINS={'dependencies/'+name+'-baseline.json' for name in [
 'native-control-integration','calibration-session','measurement-target','capabilities-runtime','config-write',
 'p2r-runtime','provider-options','writer-fixture','writer-fixture-n0a','continuous-monitor','native-capture',
 'control-foundation','calibration-gui','calibration-review-r1']}
PROTECTED={
 'src/calibration/calibration_drafts.cpp','src/calibration/calibration_production_entry.cpp',
 'src/calibration/measurement_target.cpp','src/calibration/trajectory_calibration.cpp','src/calibration/world_calibration.cpp',
 'src/mapping/config_transaction.cpp','src/routing/runtime_normalization.cpp','src/routing/bridge.cpp',
 'tools/steamvr_calibrator.cpp','tools/calibration_local_api.cpp','ui/calibration_profile.cs',
 'ui/installation_control.cs','ui/control_models.cs','ui/steamvr_control_wpf.cs','ui/steamvr_control_tray.cs',
 'scripts/verify_control_toolset.py',
}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def read(root,name):
 try:return (Path(root)/name).read_bytes()
 except OSError as e:raise RuntimeError('B4 missing actual input: '+name) from e
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',PARENT+':'+name])
@lru_cache(maxsize=None)
def existed(repo,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',PARENT+':'+name],capture_output=True).returncode==0
def connections(root):
 module=ast.parse(read(root,'tests/calibration_review_r1_scope.py').decode())
 imported={a.name for n in module.body if isinstance(n,ast.ImportFrom) and n.module=='calibration_b4_scope' for a in n.names}
 if imported!={'approved_b4','b4_project','b4_project_text'}:raise RuntimeError('B4 missing predecessor hook')
 for name,call in [('read','b4_project'),('approved_r1','approved_b4'),('r1_project','b4_project'),('r1_project_text','b4_project_text')]:
  fn=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name==name]
  if len(fn)!=1 or not any(isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id==call for n in ast.walk(fn[0])):raise RuntimeError('B4 missing validation/projection hook: '+name)
 source=read(root,'src/calibration/calibration_session.cpp').decode()
 runtime=read(root,'src/calibration/calibration_runtime_provider.cpp').decode()
 configure=runtime.split('void RuntimeCaptureProvider::validateConfiguration(',1)[1].split('void RuntimeCaptureProvider::requireOwner',1)[0]
 discover=runtime.split('ProviderBatch RuntimeCaptureProvider::discover(',1)[1].split('ProviderBatch RuntimeCaptureProvider::guard',1)[0]
 guard='need(configuration()->capabilityProfile().has_value(),"EXPLICIT_DECLARATION_REQUIRED");'
 if 'capabilityProfile' in configure or discover.count(guard)!=1 or not discover.index(guard)<discover.index('started_=true')<discover.index('api_->initialize(')<discover.index('transport_->open('):raise RuntimeError('B4 lost accepted R1 production guard order')
 for required in ['PHYSICAL_CRITERIA_NOT_ADOPTED','READONLY_APPROVAL_REJECTED','APPROVAL_SELECTION_REQUIRED','APPROVAL_MEMBER_INELIGIBLE','APPROVAL_CRITERIA_CHANGED_REVIEW_REQUIRED','APPROVAL_ARTIFACT_CHANGED','STATIC_POINT_TOKEN_INVALID','STATIC_RUN_TIMEOUT','STATIC_POINT_WAIT_TIMEOUT']:
  if required not in source:raise RuntimeError('B4 missing native refusal: '+required)
 if 'env.outputApprovalAuthority=' in re.sub(r'\s','',source):raise RuntimeError('B4 product installs a policy')
 entry=read(root,'src/calibration/calibration_production_entry.cpp').decode()
 if 'outputApprovalAuthority' in entry or 'b4_fixture' in entry:raise RuntimeError('B4 product authority injection')
 static=read(root,'src/calibration/static_calibration_session.cpp').decode()
 for required in ['points>=3&&points<=16','samples<=4096','points*samples<=16384','totalReference_<=16384','totalSource_<=16384','!best->valid','usedReference_.count(best->timeNs)','t<start_||t<=lastSource_','std::llabs(best->timeNs-s.timeNs)>20000000']:
  if required not in static:raise RuntimeError('B4 missing static bound/pairing: '+required)
 leaf=ast.parse(read(root,'tests/calibration_b4_scope.py').decode())
 imports={n.module for n in ast.walk(leaf) if isinstance(n,ast.ImportFrom)}|{a.name for n in ast.walk(leaf) if isinstance(n,ast.Import) for a in n.names}
 if not imports<={'pathlib','functools','ast','hashlib','json','re','subprocess'}:raise RuntimeError('B4 leaf imports parent validator/cycle')
def approved_b4(root,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return set()
 repo=str(git_root or root);p=json.loads(read(root,PIN))
 if set(p)!={'parent','files','pins','protected'} or p['parent']!=PARENT or set(p['files'])!=FILES or set(p['pins'])!=PINS or set(p['protected'])!=PROTECTED:raise RuntimeError('B4 exact scope/parent mismatch')
 for name,sha in p['pins'].items():
  raw=historical(repo,name)
  if read(root,name)!=raw or hashlib.sha256(raw).hexdigest()!=sha:raise RuntimeError('B4 immutable accepted pin drift: '+name)
 for name,e in p['files'].items():
  if set(e)!={'sha256','parent'} or digest(read(root,name))!=e['sha256']:raise RuntimeError('B4 unreviewed actual source: '+name)
  exists=existed(repo,name);parent=e['parent']
  if set(parent)!=({'existed','sha256'} if exists else {'existed'}) or type(parent['existed']) is not bool or parent['existed']!=exists:raise RuntimeError('B4 predecessor presence: '+name)
  if exists and digest(historical(repo,name))!=parent['sha256']:raise RuntimeError('B4 predecessor hash: '+name)
 for name,sha in p['protected'].items():
  if digest(read(root,name))!=sha or digest(historical(repo,name))!=sha:raise RuntimeError('B4 protected source changed: '+name)
 connections(root)
 return FILES|PINS|PROTECTED|{PIN}
def b4_project(root,name,raw,git_root=None,allow_older=False):
 root=Path(root)
 if not (root/PIN).exists():return raw
 e=json.loads(read(root,PIN))['files'].get(name)
 if not e:return raw
 if digest(raw)==e['sha256']:return historical(str(git_root or root),name) if e['parent']['existed'] else raw
 if e['parent']['existed'] and digest(raw)==e['parent']['sha256']:return raw
 if allow_older and e['parent']['existed']:return raw
 raise RuntimeError('B4 foreign projection: '+name)
def b4_project_text(root,text):
 root=Path(root)
 if not (root/PIN).exists():return text
 approved_b4(root);matches={historical(str(root),name).decode() for name,e in json.loads(read(root,PIN))['files'].items() if e['parent']['existed'] and digest(text.encode())==e['sha256']}
 if len(matches)>1:raise RuntimeError('B4 ambiguous text projection')
 return next(iter(matches)) if matches else text
