from pathlib import Path
import json,shutil,uuid
from measurement_target_scope import FILES,PIN,OLD_PINS,UNCHANGED,approved_measurement,historical,verify_diff_scope,verify_solver_bytes
root=Path(__file__).resolve().parents[1];approved_measurement(root);verify_diff_scope(root)
print('PASS protected solver bytes '+json.dumps(verify_solver_bytes(root),sort_keys=True))
scratch=root/'build/measurement-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
for name in approved_measurement(root)|OLD_PINS|UNCHANGED|{PIN}:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
approved_measurement(scratch,root);count=0
for name in sorted(FILES|OLD_PINS|UNCHANGED):
 p=scratch/name;old=p.read_bytes();p.write_bytes(old+b'\n# deliberate drift\n')
 try:approved_measurement(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('Unreviewed B1 drift accepted after prior approval: '+name)
 p.write_bytes(old)
p=scratch/'src/calibration/trajectory_calibration.cpp';old=p.read_bytes();p.write_bytes(historical(str(root),'src/calibration/trajectory_calibration.cpp'))
try:approved_measurement(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('Predecessor rollback accepted')
p.write_bytes(old)
p=scratch/PIN;old=p.read_bytes()
for label,mutate in [
 ('scope',lambda v:v['files'].update({'tools/bridge_service.cpp':{}})),
 ('base',lambda v:v.update(base_commit='0'*40)),
 ('predecessor',lambda v:v['files']['CMakeLists.txt'].update(base_sha256='0'*64)),
 ('presence',lambda v:v['files']['CMakeLists.txt'].update(existed_at_base=False))]:
 value=json.loads(old);mutate(value);p.write_text(json.dumps(value),encoding='utf-8')
 try:approved_measurement(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('B1 metadata mutant accepted: '+label)
 p.write_bytes(old)
assert count==len(FILES|OLD_PINS|UNCHANGED)+5
print(f'PASS B1 exact scope; {count} current-source/old-pin/model/rollback/scope/base/predecessor mutants rejected; no mutable-source approval cache')
