from pathlib import Path
import json,shutil,uuid
from writer_fixture_scope import PIN,profile,approved_writer_fixture,verify_diff_scope,historical,predecessor,check_names,digest
root=Path(__file__).resolve().parents[1];approved=approved_writer_fixture(root);verify_diff_scope(root);base,pin,files,old=profile(root)
scratch=root/'build/writer-fixture-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
copy=approved|old|{pin,PIN}
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
# The writer layer still cannot grant production changes. The composed set
# also contains separately reviewed HIL production hooks, whose actual bytes
# must be checked through their own leaf rather than a filename-only refusal.
try:check_names({'tools/bridge_service.cpp'},files)
except RuntimeError:count+=1
else:raise RuntimeError('writer fixture accepted production scope')
p=scratch/'tools/bridge_service.cpp';original=p.read_bytes();p.write_bytes(original+b'\n// unreviewed production drift\n')
try:approved_writer_fixture(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('composed writer fixture accepted actual production drift')
leaf=scratch/'dependencies/hil-discovery-baseline.json';leaf_raw=leaf.read_bytes();value=json.loads(leaf_raw);value['files']['tools/bridge_service.cpp']['sha256']=digest(p.read_bytes());leaf.write_text(json.dumps(value),encoding='utf-8')
try:approved_writer_fixture(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('composed writer fixture accepted production metadata rehash')
p.write_bytes(original);leaf.write_bytes(leaf_raw);approved_writer_fixture(scratch,root)
assert count==len(mutable)+9
print(f'PASS writer fixture exact scope; {count} source/pin/rollback/metadata/projection/production-scope mutants rejected; no current-byte cache')
