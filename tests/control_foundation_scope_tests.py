from pathlib import Path
import json,shutil,uuid
from control_foundation_scope import FILES,PIN,OLD_PINS,approved_control,historical,verify_diff_scope
root=Path(__file__).resolve().parents[1];approved_control(root);verify_diff_scope(root)
scratch=root/'build/control-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
for name in FILES|OLD_PINS|{PIN}:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
approved_control(scratch,root);count=0
for name in ['ui/installation_control.cs','ui/steamvr_control_wpf.cs','tests/capability_runtime_scope.py',*sorted(OLD_PINS)]:
 p=scratch/name;old=p.read_bytes();p.write_bytes(old+b'\n# deliberate drift\n')
 try:approved_control(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('Unreviewed drift accepted: '+name)
 p.write_bytes(old)
p=scratch/'ui/steamvr_control_wpf.cs';old=p.read_bytes();p.write_bytes(historical(str(root),'ui/steamvr_control_wpf.cs'))
try:approved_control(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('Predecessor rollback accepted')
p.write_bytes(old)
p=scratch/PIN;old=p.read_bytes();value=json.loads(old);value['files']['tools/bridge_service.cpp']={};p.write_text(json.dumps(value))
try:approved_control(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('Scope broadening accepted')
p.write_bytes(old);assert count==8
print('PASS WS01a exact control scope; 8 source/old-pin/rollback/scope mutants rejected')
