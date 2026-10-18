"""Additive HIL discovery leaf. Actual bytes checked before historical views."""
from pathlib import Path
from functools import lru_cache
import ast,hashlib,json,subprocess
PARENT='db338fa21467417987bc8f2e99f68027ca387f33'
PIN='dependencies/hil-discovery-baseline.json'
FILES={'CMakeLists.txt','tools/bridge_service.cpp','include/monaka_bridge/discovery.hpp','src/observation/discovery.cpp',
 'ui/discovery_models.cs','ui/discovery_panel.cs','ui/discovery_window.cs','ui/installation_control.cs','ui/steamvr_control_wpf.cs',
 'scripts/control_gui.cmake','scripts/build_gui.ps1','tests/run_calibration_managed.py','tests/hil_discovery_tests.cpp','tests/hil_discovery_process.py',
 'tests/run_hil_discovery_managed.py','tests/hil_discovery_gui_tests.cs','tests/hil_discovery_scope.py','tests/hil_discovery_scope_tests.py',
 'tests/calibration_b4_scope.py','tests/calibration_b4_scope_tests.py','tests/fixtures/hil/pico-five.jsonl','tests/fixtures/hil/provenance.json','docs/hil-minimal-discovery.md'}
PINS={'dependencies/'+name+'-baseline.json' for name in ['native-control-integration','calibration-session','measurement-target','capabilities-runtime','config-write','p2r-runtime','provider-options','writer-fixture','writer-fixture-n0a','continuous-monitor','native-capture','control-foundation','calibration-gui','calibration-review-r1','calibration-b4']}
PROTECTED={'src/routing/bridge.cpp','src/routing/runtime_normalization.cpp','src/routing/health.cpp','src/observation/registry.cpp',
 'src/mapping/config.cpp','src/mapping/config_transaction.cpp','ui/control_models.cs','ui/calibration_profile.cs','ui/calibration_models.cs','ui/calibration_panel.cs','ui/calibration_process_client.cs',
 'src/calibration/calibration_session.cpp','src/calibration/calibration_runtime_provider.cpp','src/calibration/calibration_capture_monitor.cpp','src/calibration/static_calibration_session.cpp','src/calibration/calibration_drafts.cpp','src/calibration/calibration_production_entry.cpp','src/calibration/measurement_target.cpp','src/calibration/trajectory_calibration.cpp','src/calibration/world_calibration.cpp',
 'scripts/build_control_toolset.py','scripts/package_calibration_control.py','scripts/verify_control_toolset.py','tools/steamvr_calibrator.cpp','tools/calibration_local_api.cpp'}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def read(root,name):
 try:return (Path(root)/name).read_bytes()
 except OSError as e:raise RuntimeError('HIL missing actual source: '+name) from e
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',PARENT+':'+name])
@lru_cache(maxsize=None)
def existed(repo,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',PARENT+':'+name],capture_output=True).returncode==0
def connections(root):
 module=ast.parse(read(root,'tests/calibration_b4_scope.py').decode())
 imports={a.name for n in module.body if isinstance(n,ast.ImportFrom) and n.module=='hil_discovery_scope' for a in n.names}
 if imports!={'approved_hil','hil_project','hil_project_text'}:raise RuntimeError('HIL missing B4 child import')
 for name,call in [('read','hil_project'),('approved_b4','approved_hil'),('b4_project','hil_project'),('b4_project_text','hil_project_text')]:
  functions=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name==name]
  if len(functions)!=1 or not any(isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id==call for n in ast.walk(functions[0])):raise RuntimeError('HIL missing outer validation/view hook: '+name)
 model=read(root,'ui/discovery_models.cs').decode();window=read(root,'ui/discovery_window.cs').decode();native=read(root,'src/observation/discovery.cpp').decode()
 for token in ['RequireOffline(snapshot,old)','ConfiguredReceipts.TryGetValue','RequireContract(contract,current)','space_approved",false','binding["mount"]=Identity()','MappingEditor.Candidate(baseDoc,-1,edit)','row.SeenAge<3000','Foreign publisher health','Runtime/config revision differs','if(!selected.Same(current)||!current.Fresh)','old["mount"]']:
  if token not in model:raise RuntimeError('HIL lost explicit guard: '+token)
 for token in ['new ConfigHelperClient(target).Commit(expected,candidate,cancel,delegate','DiscoveryModel.Recheck(loaded,health.Value','calibration.Active','Bridge restart required','object.ReferenceEquals(target,options)']:
  if token not in window:raise RuntimeError('HIL lost final commit/ownership guard: '+token)
 for token in ['const bool accepted=bridge.receive(raw,peer,now);','if(accepted)try{if(decoded)received(e,now,bridge)','return accepted;','entries_.size()>=maxDevices','sources_.size()>=maxSources','s.retired.count(m.session)','m.sequence<=sequence','raw.size()<=4096']:
  if token not in native:raise RuntimeError('HIL lost diagnostic bound/lifetime: '+token)
 if 'config.bindings[' in native or 'sourceAuthorized=' in native or 'validity' in native:raise RuntimeError('HIL diagnostics acquire tracking authority or infer validity')
 observe=native.split('void DiscoveryCache::observe(',1)[1].split('nlohmann::json DiscoveryCache::snapshot(',1)[0]
 if 'receipts_' in observe:raise RuntimeError('HIL pre-admission diagnostics mutate accepted liveness')
 if 'std::map<Key,std::int64_t> receipts_' not in read(root,'include/monaka_bridge/discovery.hpp').decode():raise RuntimeError('HIL liveness duplicates Registry sequence/lifetime authority')
def approved_hil(root,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return set()
 repo=str(git_root or root);p=json.loads(read(root,PIN))
 if set(p)!={'parent','files','pins','protected'} or p['parent']!=PARENT or set(p['files'])!=FILES or set(p['pins'])!=PINS or set(p['protected'])!=PROTECTED:raise RuntimeError('HIL exact scope/parent mismatch')
 for name,sha in p['pins'].items():
  raw=historical(repo,name)
  if read(root,name)!=raw or hashlib.sha256(raw).hexdigest()!=sha:raise RuntimeError('HIL immutable accepted pin drift: '+name)
 for name,e in p['files'].items():
  if set(e)!={'sha256','parent'} or digest(read(root,name))!=e['sha256']:raise RuntimeError('HIL unreviewed actual source: '+name)
  exists=existed(repo,name);parent=e['parent']
  if set(parent)!=({'existed','sha256'} if exists else {'existed'}) or type(parent['existed']) is not bool or parent['existed']!=exists:raise RuntimeError('HIL predecessor presence: '+name)
  if exists and digest(historical(repo,name))!=parent['sha256']:raise RuntimeError('HIL predecessor hash: '+name)
 for name,sha in p['protected'].items():
  if digest(read(root,name))!=sha or digest(historical(repo,name))!=sha:raise RuntimeError('HIL protected source changed: '+name)
 connections(root);return FILES|PINS|PROTECTED|{PIN}
def hil_project(root,name,raw,git_root=None,allow_older=False):
 root=Path(root)
 if name not in FILES or not (root/PIN).exists():return raw
 e=json.loads(read(root,PIN))['files'].get(name)
 if not e:return raw
 if digest(raw)==e['sha256']:return historical(str(git_root or root),name) if e['parent']['existed'] else raw
 if e['parent']['existed'] and digest(raw)==e['parent']['sha256']:return raw
 if allow_older and e['parent']['existed']:return raw
 raise RuntimeError('HIL foreign projection: '+name)
def hil_project_text(root,text):
 root=Path(root)
 if not (root/PIN).exists():return text
 sha=digest(text.encode());candidates=[n for n,e in json.loads(read(root,PIN))['files'].items() if e['parent']['existed'] and sha==e['sha256']]
 if not candidates:return text
 approved_hil(root);matches={historical(str(root),n).decode() for n in candidates}
 if len(matches)>1:raise RuntimeError('HIL ambiguous historical view')
 return next(iter(matches)) if matches else text
