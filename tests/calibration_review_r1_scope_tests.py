from pathlib import Path
import json,shutil,uuid
from calibration_review_r1_scope import FILES,PINS,PROTECTED,PIN,approved_r1,digest,r1_project,historical
from calibration_b4_scope import b4_project
def verify_r1(root):
 root=Path(root);approved=approved_r1(root);scratch=root/'build/r1-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True);count=0
 for name in approved:
  p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
 approved_r1(scratch,root)
 def reject(label):
  nonlocal count
  try:approved_r1(scratch,root)
  except RuntimeError:count+=1
  else:raise RuntimeError('R1 accepted '+label)
 for name in sorted(FILES|PINS|PROTECTED):
  p=scratch/name;raw=p.read_bytes();p.write_bytes(raw+b'\n// deliberate drift\n');reject(name);p.write_bytes(raw)
 pin=scratch/PIN;raw=pin.read_bytes()
 for name,old,new in [
  ('tests/calibration_gui_scope.py','extra=approved_r1(root,git_root)','extra=set()'),
  ('tests/calibration_gui_scope.py','return FILES|PINS|PROTECTED|{PIN}|extra','return FILES|PINS|PROTECTED|{PIN}'),
  ('tests/calibration_gui_scope.py','raw=r1_project(root,name,raw,git_root,allow_older)','raw=raw'),
  ('src/calibration/calibration_runtime_provider.cpp',' need(configuration()->capabilityProfile().has_value(),"EXPLICIT_DECLARATION_REQUIRED");',''),
  ('src/calibration/calibration_runtime_provider.cpp',' need(configuration()->capabilityProfile().has_value(),"EXPLICIT_DECLARATION_REQUIRED");',' api_->initialize(vr::VRApplication_Background);need(configuration()->capabilityProfile().has_value(),"EXPLICIT_DECLARATION_REQUIRED");'),
 ]:
  p=scratch/name;original=p.read_bytes();text=original.decode();assert old in text;p.write_text(text.replace(old,new),encoding='utf-8');v=json.loads(raw);v['files'][name]['sha256']=digest(p.read_bytes());pin.write_text(json.dumps(v),encoding='utf-8');reject('critical semantic/connection despite hash redeclaration');p.write_bytes(original);pin.write_bytes(raw)
 name='src/calibration/calibration_runtime_provider.cpp';p=scratch/name;original=p.read_bytes();text=original.decode().replace(' need(o.mirrorAddress()', ' need(o.capabilityProfile().has_value(),"EXPLICIT_DECLARATION_REQUIRED");\n need(o.mirrorAddress()');p.write_text(text,encoding='utf-8');v=json.loads(raw);v['files'][name]['sha256']=digest(p.read_bytes());pin.write_text(json.dumps(v),encoding='utf-8');reject('declaration restored at wrong configure boundary despite hash rewrite');p.write_bytes(original);pin.write_bytes(raw)
 for name,e in json.loads(raw)['files'].items():
  current=b4_project(root,name,(root/name).read_bytes());assert r1_project(root,name,current)==(historical(str(root),name) if e['parent']['existed'] else current)
  try:r1_project(root,name,current+b'foreign')
  except RuntimeError:count+=1
  else:raise RuntimeError('R1 foreign projection')
 approved_r1(scratch,root);print(f'PASS R1 exact scope {len(FILES)} files, {len(PINS)} immutable accepted pins, {len(PROTECTED)} protected sources, {count} drift/guard-order/connection/projection mutants; actual current bytes uncached')
if __name__=='__main__':verify_r1(Path(__file__).resolve().parents[1])
