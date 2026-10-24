from pathlib import Path
import json,shutil,uuid
from continuous_monitor_scope import FILES,PIN,OLD_PINS,approved_continuous_monitor,historical,verify_diff_scope,predecessor
from native_capture_scope import OLD_PINS as NATIVE_OLD_PINS
root=Path(__file__).resolve().parents[1];approved=approved_continuous_monitor(root);verify_diff_scope(root)
scratch=root/'build/n0b-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
for name in approved|OLD_PINS|NATIVE_OLD_PINS:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
approved_continuous_monitor(scratch,root);count=0
for name in sorted(FILES|OLD_PINS):
 p=scratch/name;old=p.read_bytes();p.write_bytes(old+b'\n# deliberate drift\n')
 try:approved_continuous_monitor(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('N0b cached/accepted drift: '+name)
 p.write_bytes(old)
p=scratch/'src/calibration/calibration_session.cpp';old=p.read_bytes();p.write_bytes(historical(str(root),'src/calibration/calibration_session.cpp'))
try:approved_continuous_monitor(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('N0b accepted session rollback')
p.write_bytes(old);p=scratch/PIN;old=p.read_bytes()
for change in ['scope','base','predecessor','presence']:
 v=json.loads(old)
 if change=='scope':v['files']['src/mapping/config_transaction.cpp']={}
 if change=='base':v['base_commit']='0'*40
 if change=='predecessor':v['files']['CMakeLists.txt']['base_sha256']='0'*64
 if change=='presence':v['files']['CMakeLists.txt']['existed_at_base']=False
 p.write_text(json.dumps(v),encoding='utf-8')
 try:approved_continuous_monitor(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('N0b accepted metadata '+change)
 p.write_bytes(old)
try:predecessor(root,'CMakeLists.txt',(root/'CMakeLists.txt').read_bytes()+b'drift')
except RuntimeError:count+=1
else:raise RuntimeError('N0b projection accepted unknown bytes')
assert count==len(FILES|OLD_PINS)+6
print(f'PASS N0b exact scope; {count} current-source/old-pin/rollback/metadata/projection mutants rejected; no current-byte approval cache')
