from pathlib import Path
import json,shutil,uuid
from provider_options_scope import FILES,PIN,OLD_PINS,approved_provider_options,historical,verify_diff_scope,predecessor
root=Path(__file__).resolve().parents[1];approved_provider_options(root);verify_diff_scope(root)
scratch=root/'build/n0a-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
for name in FILES|OLD_PINS|{PIN}:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
approved_provider_options(scratch,root);count=0
for name in sorted(FILES|OLD_PINS):
 p=scratch/name;old=p.read_bytes();p.write_bytes(old+b'\n# deliberate drift\n')
 try:approved_provider_options(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('N0a accepted drift after prior approval: '+name)
 p.write_bytes(old)
p=scratch/'src/calibration/calibration_session.cpp';old=p.read_bytes();p.write_bytes(historical(str(root),'src/calibration/calibration_session.cpp'))
try:approved_provider_options(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('N0a accepted rollback')
p.write_bytes(old);p=scratch/PIN;old=p.read_bytes()
for change in ['scope','base','predecessor','presence']:
 v=json.loads(old)
 if change=='scope':v['files']['tools/bridge_service.cpp']={}
 if change=='base':v['base_commit']='0'*40
 if change=='predecessor':v['files']['CMakeLists.txt']['base_sha256']='0'*64
 if change=='presence':v['files']['CMakeLists.txt']['existed_at_base']=False
 p.write_text(json.dumps(v),encoding='utf-8')
 try:approved_provider_options(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('N0a accepted metadata '+change)
 p.write_bytes(old)
name='src/calibration/calibration_session.cpp';raw=(root/name).read_bytes()
assert predecessor(root,name,raw)==historical(str(root),name)
try:predecessor(root,name,raw+b'changed')
except RuntimeError:count+=1
else:raise RuntimeError('N0a projection accepted unknown bytes')
assert count==len(FILES|OLD_PINS)+6
print(f'PASS N0a exact scope; {count} current-source/old-pin/rollback/metadata/projection mutants rejected; no mutable-source approval cache')
