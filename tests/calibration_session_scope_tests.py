from pathlib import Path
import json,shutil,uuid
from calibration_session_scope import FILES,PIN,OLD_PINS,approved_session,historical,verify_diff_scope,verify_legacy_bytes
root=Path(__file__).resolve().parents[1];approved_session(root);verify_diff_scope(root)
print('PASS B2 unchanged source/solver/legacy CLI '+json.dumps(verify_legacy_bytes(root),sort_keys=True))
scratch=root/'build/b2-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
for n in FILES|OLD_PINS|{PIN}:
 p=scratch/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/n,p)
approved_session(scratch,root);count=0
for n in sorted(FILES|OLD_PINS):
 p=scratch/n;old=p.read_bytes();p.write_bytes(old+b'\n# drift\n')
 try:approved_session(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('B2 accepted drift '+n)
 p.write_bytes(old)
p=scratch/'tools/steamvr_calibrator.cpp';old=p.read_bytes();p.write_bytes(historical(str(root),'tools/steamvr_calibrator.cpp'))
try:approved_session(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('B2 accepted predecessor rollback')
p.write_bytes(old);p=scratch/PIN;old=p.read_bytes()
for change in ['scope','base','predecessor','presence']:
 v=json.loads(old)
 if change=='scope':v['files']['tools/bridge_service.cpp']={}
 if change=='base':v['base_commit']='0'*40
 if change=='predecessor':v['files']['CMakeLists.txt']['base_sha256']='0'*64
 if change=='presence':v['files']['CMakeLists.txt']['existed_at_base']=False
 p.write_text(json.dumps(v))
 try:approved_session(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('B2 accepted metadata '+change)
 p.write_bytes(old)
assert count==25
print('PASS B2 exact scope;25 current-source/old-pin/rollback/scope/base/predecessor mutants rejected')
