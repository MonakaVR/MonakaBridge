from pathlib import Path
import json,shutil,subprocess,sys,uuid
from native_capture_scope import BASE,FILES,PIN,OLD_PINS,approved_native_capture,historical,verify_diff_scope,predecessor
from calibration_gui_scope import approved_gui,gui_project
root=Path(__file__).resolve().parents[1];approved=approved_native_capture(root);verify_diff_scope(root)
scratch=root/'build/n1-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
for name in approved|OLD_PINS:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
approved_native_capture(scratch,root);count=0
for name in sorted(FILES|OLD_PINS):
 p=scratch/name;old=p.read_bytes();p.write_bytes(old+b'\n# deliberate drift\n')
 try:approved_native_capture(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('N1 cached/accepted current drift: '+name)
 p.write_bytes(old)
p=scratch/'tools/calibration_local_api.cpp';old=p.read_bytes();p.write_bytes(historical(str(root),'tools/calibration_local_api.cpp'))
try:approved_native_capture(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('N1 accepted predecessor rollback')
p.write_bytes(old);p=scratch/PIN;old=p.read_bytes()
for change in ['scope','base','predecessor','presence']:
 v=json.loads(old)
 if change=='scope':v['files']['tools/steamvr_calibrator.cpp']={}
 if change=='base':v['base_commit']='0'*40
 if change=='predecessor':v['files']['CMakeLists.txt']['base_sha256']='0'*64
 if change=='presence':v['files']['CMakeLists.txt']['existed_at_base']=False
 p.write_text(json.dumps(v),encoding='utf-8')
 try:approved_native_capture(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('N1 accepted metadata '+change)
 p.write_bytes(old)
for name in ['CMakeLists.txt','src/calibration/calibration_runtime_provider.cpp']:
 try:predecessor(root,name,(root/name).read_bytes()+b'drift')
 except RuntimeError:count+=1
 else:raise RuntimeError('N1 accepted unknown projection '+name)
assert count==len(FILES|OLD_PINS)+7
# Existing CLI, solver, native writer, runtime contract and protocol source stay
# outside N1. Approve the actual U1/U2 tree first, then compare its explicit
# fixed J0 projection to N1's predecessor. Old baseline bytes stay immutable.
approved_gui(root)
protected=['tools/steamvr_calibrator.cpp','src/calibration/world_calibration.cpp',
 'src/calibration/trajectory_calibration.cpp','src/calibration/calibration_session.cpp',
 'src/calibration/calibration_capture_monitor.cpp','src/mapping/config_transaction.cpp',
 'src/routing/runtime_normalization.cpp','src/routing/bridge.cpp']
for name in protected:
 approved_bytes=gui_project(root,name,(root/name).read_bytes())
 assert approved_bytes.replace(b'\r\n',b'\n')==historical(str(root),name).replace(b'\r\n',b'\n'),name
if len(sys.argv)>1:
 sdk=Path(sys.argv[1]);copy=scratch/'sdk';lock=json.loads((root/'dependencies/calibration-openvr-sdk.json').read_bytes())
 for name in lock['files']:
  target=copy/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(sdk/name,target)
 header=copy/'headers/openvr.h';header.write_bytes(header.read_bytes()+b'\n// deliberate SDK drift\n')
 check=subprocess.run([sys.executable,'-B',str(root/'scripts/verify_calibration_sdk.py'),str(copy)],capture_output=True)
 assert check.returncode!=0 and b'calibration SDK artifact drift: headers/openvr.h' in check.stderr,check.stderr
 print('PASS actual SDK byte-copy drift fails before Git identity check; no SDK execution')
print(f'PASS N1 exact scope; {count} current-source/9 immutable pins/rollback/metadata/projection mutants rejected; {len(protected)} protected N1-view sources equal {BASE}; actual GUI tree approved before projection; current checks uncached')
