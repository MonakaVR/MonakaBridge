from pathlib import Path
import json,shutil,uuid
from writer_fixture_scope import PIN,profile,approved_writer_fixture,verify_diff_scope,historical,predecessor,check_names
root=Path(__file__).resolve().parents[1];approved_writer_fixture(root);verify_diff_scope(root);base,pin,files,old=profile(root)
scratch=root/'build/writer-fixture-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
copy=files|old|{pin,PIN}
for name in copy:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
approved_writer_fixture(scratch,root);count=0;mutable=files|old|({PIN} if pin!=PIN else set())
for name in sorted(mutable):
 p=scratch/name;raw=p.read_bytes();p.write_bytes(raw+b'\n# deliberate drift\n')
 try:approved_writer_fixture(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('writer fixture accepted drift '+name)
 p.write_bytes(raw)
p=scratch/'tests/calibration_session_process_fixture.cpp';raw=p.read_bytes();p.write_bytes(historical(str(root),base,'tests/calibration_session_process_fixture.cpp'))
try:approved_writer_fixture(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('writer fixture accepted rollback')
p.write_bytes(raw);p=scratch/pin;raw=p.read_bytes()
for label in ['base','scope','predecessor','presence']:
 v=json.loads(raw)
 if label=='base':v['base_commit']='0'*40
 if label=='scope':v['files']['tools/calibration_local_api.cpp']={}
 if label=='predecessor':v['files']['CMakeLists.txt']['base_sha256']='0'*64
 if label=='presence':v['files']['CMakeLists.txt']['existed_at_base']=False
 p.write_text(json.dumps(v),encoding='utf-8')
 try:approved_writer_fixture(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('writer fixture accepted metadata '+label)
 p.write_bytes(raw)
try:predecessor(root,'CMakeLists.txt',(root/'CMakeLists.txt').read_bytes()+b'drift')
except RuntimeError:count+=1
else:raise RuntimeError('writer fixture accepted unknown projection')
try:check_names({'tools/calibration_local_api.cpp'},approved_writer_fixture(root))
except RuntimeError:count+=1
else:raise RuntimeError('writer fixture accepted production scope')
assert count==len(mutable)+7
print(f'PASS writer fixture exact scope; {count} source/pin/rollback/metadata/projection/production-scope mutants rejected; no current-byte cache')
