"""Reviewed native child; actual bytes are admitted before predecessor views."""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
PARENT='7e95371b145a516e51dbba3beb8d526b4c91dd9f'
PIN='dependencies/world-approval-baseline.json'
FILES={'tests/measurement_target_scope.py','CMakeLists.txt','tools/bridge_config.cpp','src/calibration/world_calibration.cpp','include/monaka_bridge/world_calibration.hpp','tests/world_calibration_tests.cpp','tests/trajectory_calibration_tests.cpp','docs/configuration.md','docs/world-approval-invalidation.md','tests/responsibility_scope.py','tests/world_approval_scope.py','tests/world_approval_scope_tests.py','tests/world_approval_invalidation_tests.cpp','tests/world_approval_b4_tests.cpp','tests/world_approval_cli_tests.py','tests/world_approval_gui_tests.cs','tests/run_world_approval_gui.py','tests/fixtures/world-approval/approved-groups.json'}
EXISTING={'tests/measurement_target_scope.py','CMakeLists.txt','tools/bridge_config.cpp','src/calibration/world_calibration.cpp','include/monaka_bridge/world_calibration.hpp','tests/world_calibration_tests.cpp','tests/trajectory_calibration_tests.cpp','docs/configuration.md','tests/responsibility_scope.py'}
# Filled from the immutable parent by the reviewed receipt preparation script.
FROZEN={'src/calibration/calibration_runtime_provider.cpp', 'AGENTS.md', 'dependencies/calibration-review-r1-baseline.json', 'dependencies/provider-options-baseline.json', 'tests/calibration_b4_gui_tests.cs', 'ui/steamvr_control_wpf.cs', 'src/mapping/config.cpp', 'src/routing/bridge.cpp', 'dependencies/measurement-target-baseline.json', 'dependencies/writer-fixture-baseline.json', 'dependencies/writer-fixture-n0a-baseline.json', 'scripts/build_control_toolset.py', 'dependencies/calibration-session-baseline.json', 'README.md', 'dependencies/capabilities-runtime-baseline.json', 'ui/steamvr_control_tray.cs', 'ui/calibration_process_client.cs', 'tests/hil_discovery_scope.py', 'dependencies/responsibility-baseline.json', 'src/calibration/calibration_session.cpp', 'ui/calibration_profile.cs', 'tests/responsibility_scope_tests.py', 'dependencies/calibration-gui-baseline.json', 'tests/operator_gui_launcher_tests.py', 'dependencies/continuous-monitor-baseline.json', 'src/calibration/trajectory_calibration.cpp', 'dependencies/control-foundation-baseline.json', 'scripts/operator_gui_launcher.py', 'scripts/control_gui.cmake', 'dependencies/calibration-b4-baseline.json', 'dependencies/hil-discovery-baseline.json', 'src/calibration/calibration_production_entry.cpp', 'src/routing/health.cpp', 'ui/control_models.cs', 'dependencies/native-control-integration-baseline.json', 'dependencies/native-capture-baseline.json', 'src/routing/runtime_normalization.cpp', 'ui/calibration_panel.cs', 'tests/ui_production_tests.cs', 'src/calibration/measurement_target.cpp', 'dependencies/config-write-baseline.json', 'src/observation/registry.cpp', 'src/mapping/config_transaction.cpp', 'docs/responsibility-refactor.md', 'dependencies/p2r-runtime-baseline.json'}
CMAKE_APPEND_SHA='c364c1eb5117056bad11d1da94473e6747330b46688639ab7e6a503f98542243'
NATIVE_PATCHES={
 'tools/bridge_config.cpp':('  for(auto& [k,b]:c.bindings)if(b.source==selected->source&&b.inputSpace==selected->inputSpace&&b.inputRevision==selected->inputRevision)b.world.translation=t;', '  for(auto& [k,b]:c.bindings)if(b.source==selected->source&&b.inputSpace==selected->inputSpace&&b.inputRevision==selected->inputRevision){\n   b.world.translation=t;b.spaceApproved=false;\n  }'),
 'src/calibration/world_calibration.cpp':('            binding.inputRevision == measured.binding.inputRevision)\n            binding.world = transform;', '            binding.inputRevision == measured.binding.inputRevision) {\n            binding.world = transform;\n            binding.spaceApproved = false;\n        }')}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def actual(root,name):
 try:return (Path(root)/name).read_bytes()
 except OSError as e:raise RuntimeError('World approval missing actual source: '+name) from e
@lru_cache(maxsize=None)
def repository(location):
 path=Path(location).absolute()
 while not (path/'.git').exists():
  if path.parent==path:raise RuntimeError('World approval history repository unavailable')
  path=path.parent
 return str(path)
@lru_cache(maxsize=None)
def immutable(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',PARENT+':'+name])
def historical(root,name):return immutable(repository(str(root)),name)
def metadata(root):
 try:
  p=json.loads(actual(root,PIN))
  if set(p)!={'parent','files','frozen'} or p['parent']!=PARENT or set(p['files'])!=FILES or set(p['frozen'])!=FROZEN:raise RuntimeError('World approval exact scope/parent mismatch')
  for name,e in p['files'].items():
   if set(e)!={'sha256','parent'}:raise RuntimeError('World approval malformed file receipt')
   if (name in EXISTING)!=(isinstance(e['parent'],str) and len(e['parent'])==64) or (name not in EXISTING and e['parent'] is not None):raise RuntimeError('World approval predecessor presence drift')
  return p
 except (KeyError,TypeError,ValueError) as e:raise RuntimeError('World approval malformed receipt') from e
def approved_world_approval(root,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return set()
 p=metadata(root);repo=git_root or root
 for name,e in p['files'].items():
  if digest(actual(root,name))!=e['sha256']:raise RuntimeError('World approval actual source drift: '+name)
  if e['parent'] is not None and digest(historical(repo,name))!=e['parent']:raise RuntimeError('World approval predecessor drift: '+name)
 for name,sha in p['frozen'].items():
  if digest(actual(root,name))!=sha or digest(historical(repo,name))!=sha:raise RuntimeError('World approval frozen source drift: '+name)
 # A metadata rehash cannot widen either reviewed native change.
 for name,(before,after) in NATIVE_PATCHES.items():
  old=historical(repo,name).replace(b'\r\n',b'\n').decode()
  if old.count(before)!=1 or digest(actual(root,name))!=digest(old.replace(before,after).encode()):raise RuntimeError('World approval native change exceeds exact reviewed patch: '+name)
 parent_cmake=historical(repo,'CMakeLists.txt').replace(b'\r\n',b'\n')
 cmake=actual(root,'CMakeLists.txt').replace(b'\r\n',b'\n')
 if not cmake.startswith(parent_cmake) or digest(cmake[len(parent_cmake):])!=CMAKE_APPEND_SHA:raise RuntimeError('World approval CMake change exceeds regression-only append')
 hook=actual(root,'tests/responsibility_scope.py').decode()
 for token in ['extra=approved_world_approval(root,git_root)','return world_project(root,name,','raw=world_project(root,name,raw,git_root)','text=world_project_text(root,text)','|{PIN}|extra']:
  if token not in hook:raise RuntimeError('World approval predecessor validation hook lost: '+token)
 if 'digest(world_project(root,name,(root/name).read_bytes(),git_root))' not in actual(root,'tests/measurement_target_scope.py').decode():raise RuntimeError('World approval B1 protected-source hook lost')
 return FILES|FROZEN|{PIN}
@lru_cache(maxsize=None)
def versions(repo,name):
 values={digest(immutable(repo,name))}
 for pin in FROZEN:
  if not pin.endswith('-baseline.json'):continue
  e=json.loads(immutable(repo,pin)).get('files',{}).get(name)
  if isinstance(e,str):values.add(e)
  elif isinstance(e,dict):
   for key in ['sha256','base_sha256']:
    if isinstance(e.get(key),str):values.add(e[key])
   parent=e.get('parent')
   if isinstance(parent,str):values.add(parent)
   elif isinstance(parent,dict) and isinstance(parent.get('sha256'),str):values.add(parent['sha256'])
  protected=json.loads(immutable(repo,pin)).get('protected',{}).get(name)
  if isinstance(protected,str):values.add(protected)
 return values
def world_project(root,name,raw,git_root=None):
 root=Path(root)
 if name not in FILES or not (root/PIN).exists():return raw
 e=metadata(root)['files'][name];sha=digest(raw)
 if sha==e['sha256']:return historical(git_root or root,name) if e['parent'] is not None else raw
 if e['parent'] is not None and sha in versions(repository(str(git_root or root)),name):return raw
 raise RuntimeError('World approval foreign predecessor view: '+name)
def world_project_text(root,text):
 root=Path(root)
 if not (root/PIN).exists():return text
 sha=digest(text.encode());matches=[n for n,e in metadata(root)['files'].items() if e['parent'] is not None and sha==e['sha256']]
 if not matches:return text
 approved_world_approval(root);values={historical(root,n).decode() for n in matches}
 if len(values)!=1:raise RuntimeError('World approval ambiguous source view')
 return values.pop()
