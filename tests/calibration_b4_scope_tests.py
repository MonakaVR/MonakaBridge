from pathlib import Path
import json,shutil,uuid
from calibration_b4_scope import FILES,PINS,PROTECTED,PIN,approved_b4,digest,b4_project,b4_project_text,historical
from calibration_review_r1_scope import approved_r1
from hil_discovery_scope import hil_project
def verify_b4(root):
 root=Path(root);approved=approved_b4(root);scratch=root/'build/b4-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True);count=0
 for name in approved:
  p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
 approved_b4(scratch,root)
 def reject(label):
  nonlocal count
  try:approved_b4(scratch,root)
  except (RuntimeError,ValueError,KeyError):count+=1
  else:raise RuntimeError('B4 accepted '+label)
 for name in sorted(FILES|PINS|PROTECTED):
  p=scratch/name;raw=p.read_bytes();p.write_bytes(raw+b'\n// deliberate drift\n');reject(name);p.write_bytes(raw)
 pin=scratch/PIN;raw=pin.read_bytes()
 for name,old,new in [
  ('tests/calibration_review_r1_scope.py','extra=approved_b4(root,git_root)','extra=set()'),
  ('tests/calibration_review_r1_scope.py','raw=b4_project(root,name,raw,git_root,allow_older or known_parent)','raw=raw'),
  ('src/calibration/static_calibration_session.cpp','!best->valid','false'),
  ('src/calibration/static_calibration_session.cpp','usedReference_.count(best->timeNs)','false'),
  ('src/calibration/static_calibration_session.cpp','points*samples<=16384','true'),
  ('src/calibration/static_calibration_session.cpp','t<start_||t<=lastSource_','false'),
  ('src/calibration/calibration_session.cpp','PHYSICAL_CRITERIA_NOT_ADOPTED','POLICY_IGNORED'),
  ('src/calibration/calibration_session.cpp','READONLY_APPROVAL_REJECTED','READONLY_IGNORED'),
  ('src/calibration/calibration_runtime_provider.cpp',' need(configuration()->capabilityProfile().has_value(),"EXPLICIT_DECLARATION_REQUIRED");',''),
 ]:
  p=scratch/name;original=p.read_bytes();text=original.decode();assert old in text;p.write_text(text.replace(old,new),encoding='utf-8');v=json.loads(raw);v['files'][name]['sha256']=digest(p.read_bytes());pin.write_text(json.dumps(v),encoding='utf-8');reject('semantic/connection despite metadata rehash');p.write_bytes(original);pin.write_bytes(raw)
 for name,e in json.loads(raw)['files'].items():
  current=(root/name).read_bytes();assert b4_project(root,name,current)==(historical(str(root),name) if e['parent']['existed'] else hil_project(root,name,current))
  try:b4_project(root,name,current+b'foreign')
  except RuntimeError:count+=1
  else:raise RuntimeError('B4 foreign projection')
 # No-op views cannot grant admission to changed actual inputs.
 name='src/calibration/calibration_runtime_provider.cpp';p=scratch/name;original=p.read_bytes();p.write_bytes(original+b'\n// drift\n')
 assert b4_project(scratch,'unrelated-file',b'unrelated-view',root)==b'unrelated-view'
 assert b4_project_text(scratch,'unrelated-view')=='unrelated-view'
 reject('no-op projection did not approve actual drift')
 try:approved_r1(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('parent admitted actual drift after no-op view')
 p.write_bytes(original)
 approved_b4(scratch,root);print(f'PASS B4 exact scope {len(FILES)} files, {len(PINS)} immutable accepted pins, {len(PROTECTED)} protected sources, {count} mutants; actual source uncached')
if __name__=='__main__':verify_b4(Path(__file__).resolve().parents[1])
