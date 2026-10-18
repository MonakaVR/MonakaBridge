"""Explicit child of PR9; predecessor receipts remain immutable evidence."""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
PARENT='a03557d242c7d0588b3e3012d69a6de737e79715'
PIN='dependencies/responsibility-baseline.json'
FILES={'AGENTS.md','README.md','docs/responsibility-refactor.md','ui/steamvr_control_wpf.cs','ui/steamvr_control_tray.cs','ui/control_models.cs','ui/calibration_panel.cs',
 'tests/ui_production_tests.cs','tests/calibration_b4_gui_tests.cs','tests/hil_discovery_scope.py',
 'tests/responsibility_scope.py','tests/responsibility_scope_tests.py','scripts/operator_gui_launcher.py','tests/operator_gui_launcher_tests.py'}
PROTECTED={'dependencies/hil-discovery-baseline.json', 'src/observation/registry.cpp', 'src/routing/bridge.cpp', 'src/mapping/config.cpp', 'dependencies/measurement-target-baseline.json', 'src/mapping/config_transaction.cpp', 'dependencies/calibration-b4-baseline.json', 'dependencies/p2r-runtime-baseline.json', 'CMakeLists.txt', 'dependencies/native-control-integration-baseline.json', 'scripts/build_control_toolset.py', 'scripts/control_gui.cmake', 'src/calibration/trajectory_calibration.cpp', 'src/calibration/calibration_session.cpp', 'dependencies/control-foundation-baseline.json', 'src/routing/health.cpp', 'dependencies/writer-fixture-n0a-baseline.json', 'dependencies/writer-fixture-baseline.json', 'dependencies/native-capture-baseline.json', 'src/routing/runtime_normalization.cpp', 'dependencies/continuous-monitor-baseline.json', 'dependencies/calibration-review-r1-baseline.json', 'dependencies/calibration-gui-baseline.json', 'dependencies/config-write-baseline.json', 'src/calibration/calibration_production_entry.cpp', 'ui/calibration_process_client.cs', 'dependencies/provider-options-baseline.json', 'dependencies/calibration-session-baseline.json', 'src/calibration/measurement_target.cpp', 'ui/calibration_profile.cs', 'src/calibration/world_calibration.cpp', 'dependencies/capabilities-runtime-baseline.json', 'src/calibration/calibration_runtime_provider.cpp'}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',str(repo),'show',PARENT+':'+name])
def approved_responsibility(root,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return set()
 p=json.loads((root/PIN).read_bytes())
 if p['parent']!=PARENT or set(p['files'])!=FILES or set(p['protected'])!=PROTECTED:raise RuntimeError('Responsibility exact source scope mismatch')
 for name,entry in p['files'].items():
  if digest((root/name).read_bytes())!=entry['sha256']:raise RuntimeError('Responsibility source drift: '+name)
  if entry['parent'] is not None and digest(historical(git_root or root,name))!=entry['parent']:raise RuntimeError('Responsibility predecessor drift: '+name)
 # Preserve every frozen receipt and native safety/contract/solver surface.
 for name,sha in p['protected'].items():
  raw=(root/name).read_bytes()
  if digest(raw)!=sha or digest(historical(git_root or root,name))!=sha:raise RuntimeError('Responsibility protected drift: '+name)
 model=(root/'ui/control_models.cs').read_text();wpf=(root/'ui/steamvr_control_wpf.cs').read_text();tray=(root/'ui/steamvr_control_tray.cs').read_text();panel=(root/'ui/calibration_panel.cs').read_text();launcher=(root/'scripts/operator_gui_launcher.py').read_text()
 if '&& !sameApproval' not in model and '&&!sameApproval' not in model:raise RuntimeError('Responsibility missing approval guard')
 for token in ['source_id','device_id','profile','input_space','input_revision','world_space']:
  if '"'+token+'"' not in model.split('bool sameApproval=')[1].split('if(selected==null){')[0]:raise RuntimeError('Responsibility incomplete approval identity')
 if 'approved.IsEnabled=false;' not in wpf:raise RuntimeError('Responsibility editable approval checkbox')
 for source in [wpf,tray]:
  if 'new[]{"steamvr","monaka","both","disabled"}' in source or 'legacy' not in source.lower():raise RuntimeError('Responsibility normal Both or missing migration notice')
 if 'MEASURED (NOT SAVED)' not in panel or 'PREPARED (NOT SAVED)' not in panel or 's.ActualOutcome' not in panel:raise RuntimeError('Responsibility result/save stage regression')
 if "raise ValueError('The configured PICO mapping/publisher changed" in launcher or "p['selection_matches']" not in launcher:raise RuntimeError('Responsibility GUI entry selection gate')
 return FILES|set(p['protected'])|{PIN}
def responsibility_project(root,name,raw,git_root=None,allow_older=False):
 root=Path(root)
 if name not in FILES or not (root/PIN).exists():return raw
 p=json.loads((root/PIN).read_bytes());e=p['files'][name];sha=digest(raw)
 if sha==e['sha256']:
  if e['parent'] is not None:return historical(git_root or root,name)
  return raw
 if sha==e['parent']:return raw
 # Predecessor layers explicitly identify older historical views. This is a
 # view-only pass-through, never approval of current on-disk bytes; admission
 # still validates all current files through approved_responsibility.
 if allow_older and e['parent'] is not None:return raw
 raise RuntimeError('Responsibility foreign projection: '+name)
def responsibility_project_text(root,text):
 root=Path(root)
 if not (root/PIN).exists():return text
 p=json.loads((root/PIN).read_bytes());sha=digest(text.encode())
 candidates=[n for n,e in p['files'].items() if e['parent'] is not None and sha==e['sha256']]
 if not candidates:return text
 approved_responsibility(root)
 values={historical(root,n).decode() for n in candidates}
 if len(values)!=1:raise RuntimeError('Responsibility ambiguous view')
 return values.pop()
