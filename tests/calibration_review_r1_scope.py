"""Independent-review R1: exact current bytes, immutable accepted 76 baseline."""
from pathlib import Path
from functools import lru_cache
import ast,hashlib,json,re,subprocess
PARENT='76c4bbe973d63557b9b11f98a4e33630cd5e9c93'
PIN='dependencies/calibration-review-r1-baseline.json'
FILES={'src/calibration/calibration_runtime_provider.cpp','tests/calibration_runtime_provider_tests.cpp',
 'tests/calibration_session_process.py','tests/calibration_gui_scope.py','tests/calibration_gui_scope_tests.py',
 'tests/calibration_review_r1_scope.py','tests/calibration_review_r1_scope_tests.py','docs/calibration-review-r1.md'}
PINS={'dependencies/native-control-integration-baseline.json','dependencies/calibration-session-baseline.json',
 'dependencies/measurement-target-baseline.json','dependencies/capabilities-runtime-baseline.json',
 'dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json','dependencies/provider-options-baseline.json',
 'dependencies/writer-fixture-baseline.json','dependencies/writer-fixture-n0a-baseline.json','dependencies/continuous-monitor-baseline.json',
 'dependencies/native-capture-baseline.json','dependencies/control-foundation-baseline.json','dependencies/calibration-gui-baseline.json'}
PROTECTED={'src/calibration/calibration_session.cpp','src/calibration/calibration_capture_monitor.cpp',
 'src/calibration/calibration_drafts.cpp','src/calibration/calibration_production_entry.cpp',
 'src/calibration/measurement_target.cpp','src/calibration/trajectory_calibration.cpp','src/calibration/world_calibration.cpp',
 'src/mapping/config_transaction.cpp','src/routing/runtime_normalization.cpp','src/routing/bridge.cpp','tools/steamvr_calibrator.cpp',
 'ui/calibration_process_client.cs','ui/calibration_models.cs','ui/calibration_profile.cs','ui/calibration_panel.cs'}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def read(root,name):
 try:return (Path(root)/name).read_bytes()
 except OSError as e:raise RuntimeError('R1 missing actual input: '+name) from e
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',PARENT+':'+name])
@lru_cache(maxsize=None)
def existed(repo,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',PARENT+':'+name],capture_output=True).returncode==0
def verify_connections(root):
 module=ast.parse(read(root,'tests/calibration_gui_scope.py').decode())
 imports={a.name for n in module.body if isinstance(n,ast.ImportFrom) and n.module=='calibration_review_r1_scope' for a in n.names}
 if imports!={'approved_r1','r1_project','r1_project_text'}:raise RuntimeError('R1 lost GUI child import')
 for name,call in [('read','r1_project'),('approved_gui','approved_r1'),('gui_project','r1_project'),('gui_project_text','r1_project_text')]:
  functions=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name==name]
  if len(functions)!=1 or not any(isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id==call for n in ast.walk(functions[0])):raise RuntimeError('R1 lost delegation: '+name)
 fn=next(n for n in module.body if isinstance(n,ast.FunctionDef) and n.name=='approved_gui')
 expected=ast.dump(ast.parse('FILES|PINS|PROTECTED|{PIN}|extra',mode='eval').body,include_attributes=False)
 if not any(isinstance(n,ast.Return) and ast.dump(n.value,include_attributes=False)==expected for n in ast.walk(fn)):raise RuntimeError('R1 lost approval union')
 source=read(root,'src/calibration/calibration_runtime_provider.cpp').decode()
 source=re.sub(r'//[^\n]*|/\*.*?\*/','',source,flags=re.S)
 configure=source.split('void RuntimeCaptureProvider::validateConfiguration(',1)[1].split('void RuntimeCaptureProvider::requireOwner',1)[0]
 if 'capabilityProfile' in configure:raise RuntimeError('R1 declaration moved back into configure/open')
 discover=source.split('ProviderBatch RuntimeCaptureProvider::discover(',1)[1].split('ProviderBatch RuntimeCaptureProvider::guard',1)[0]
 guard='need(configuration()->capabilityProfile().has_value(),"EXPLICIT_DECLARATION_REQUIRED");'
 if discover.count(guard)!=1 or not discover.index(guard)<discover.index('started_=true')<discover.index('api_->initialize(')<discover.index('transport_->open('):raise RuntimeError('R1 declaration guard missing/after resource acquisition')
 leaf=ast.parse(read(root,'tests/calibration_review_r1_scope.py').decode())
 imported={n.module for n in ast.walk(leaf) if isinstance(n,ast.ImportFrom)}|{a.name for n in ast.walk(leaf) if isinstance(n,ast.Import) for a in n.names}
 if not imported<={'pathlib','functools','ast','hashlib','json','re','subprocess'}:raise RuntimeError('R1 leaf imports historical validator/cycle')
def approved_r1(root,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return set()
 repo=str(git_root or root);p=json.loads(read(root,PIN))
 if set(p)!={'parent','files','pins','protected'} or p['parent']!=PARENT or set(p['files'])!=FILES or set(p['pins'])!=PINS or set(p['protected'])!=PROTECTED:raise RuntimeError('R1 exact scope/parent mismatch')
 for name,sha in p['pins'].items():
  raw=historical(repo,name)
  if read(root,name)!=raw or hashlib.sha256(raw).hexdigest()!=sha:raise RuntimeError('R1 immutable accepted pin drift: '+name)
 for name,e in p['files'].items():
  if set(e)!={'sha256','parent'} or digest(read(root,name))!=e['sha256']:raise RuntimeError('R1 unreviewed actual source: '+name)
  parent=e['parent'];exists=existed(repo,name)
  if set(parent)!=({'existed','sha256'} if exists else {'existed'}) or type(parent['existed']) is not bool or parent['existed']!=exists:raise RuntimeError('R1 predecessor presence: '+name)
  if exists and digest(historical(repo,name))!=parent['sha256']:raise RuntimeError('R1 predecessor hash: '+name)
 for name,sha in p['protected'].items():
  if digest(read(root,name))!=sha or digest(historical(repo,name))!=sha:raise RuntimeError('R1 protected source changed: '+name)
 verify_connections(root)
 return FILES|PINS|PROTECTED|{PIN}
def r1_project(root,name,raw,git_root=None,allow_older=False):
 root=Path(root)
 if not (root/PIN).exists():return raw
 e=json.loads(read(root,PIN))['files'].get(name)
 if not e:return raw
 if digest(raw)==e['sha256']:return historical(str(git_root or root),name) if e['parent']['existed'] else raw
 if e['parent']['existed'] and digest(raw)==e['parent']['sha256']:return raw
 if allow_older and e['parent']['existed']:return raw
 raise RuntimeError('R1 unknown/foreign projection: '+name)
def r1_project_text(root,text):
 root=Path(root)
 if not (root/PIN).exists():return text
 approved_r1(root);matches={historical(str(root),name).decode() for name,e in json.loads(read(root,PIN))['files'].items() if e['parent']['existed'] and digest(text.encode())==e['sha256']}
 if len(matches)>1:raise RuntimeError('R1 ambiguous text projection')
 return next(iter(matches)) if matches else text
