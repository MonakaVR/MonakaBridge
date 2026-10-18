from pathlib import Path
import json,shutil,tempfile
from world_approval_scope import FILES,FROZEN,PIN,approved_world_approval,digest,world_project,historical
from responsibility_scope import approved_responsibility
from calibration_session_scope import verify_legacy_bytes
root=Path(__file__).resolve().parents[1];approved=approved_responsibility(root);count=0
with tempfile.TemporaryDirectory(dir=root/'build') as directory:
 scratch=Path(directory)
 for name in approved:
  target=scratch/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,target)
 approved_responsibility(scratch,root)
 def rejects():
  global count
  try:approved_responsibility(scratch,root)
  except RuntimeError:count+=1
  else:raise AssertionError('World invalidation drift accepted')
 for name in sorted(FILES|FROZEN|{PIN}):
  target=scratch/name;before=target.read_bytes();target.write_bytes(before+b'\n# unexpected actual drift\n');rejects();target.write_bytes(before)
 pin=scratch/PIN;receipt=pin.read_bytes()
 for name,token in [('tools/bridge_config.cpp',b'b.spaceApproved=false;'),('src/calibration/world_calibration.cpp',b'binding.spaceApproved = false;')]:
  target=scratch/name;before=target.read_bytes();target.write_bytes(before.replace(token,b''));value=json.loads(receipt);value['files'][name]['sha256']=digest(target.read_bytes());pin.write_text(json.dumps(value));rejects();target.write_bytes(before);pin.write_bytes(receipt)
 for field in ['parent','frozen']:
  value=json.loads(receipt)
  if field=='parent':value['parent']='0'*40
  else:value['frozen'][next(iter(FROZEN))]='0'*64
  pin.write_text(json.dumps(value));rejects();pin.write_bytes(receipt)
 for name in ['tools/bridge_config.cpp','tests/world_approval_gui_tests.cs']:
  value=json.loads(receipt);value['files'][name]['parent']=None if value['files'][name]['parent'] is not None else '0'*64;pin.write_text(json.dumps(value));rejects();pin.write_bytes(receipt)
 name='src/calibration/world_calibration.cpp';target=scratch/name;before=target.read_bytes();target.unlink();rejects();target.write_bytes(before)
 # A permitted historical view never admits a changed actual native source.
 target.write_bytes(before+b'\n// actual native drift\n');view=historical(root,name);assert world_project(scratch,name,view,root)==view;rejects()
 try:verify_legacy_bytes(scratch)
 except RuntimeError:count+=1
 else:raise AssertionError('B2 legacy view accepted changed actual native source')
 target.write_bytes(before)
 try:world_project(scratch,name,b'foreign arbitrary source',root)
 except RuntimeError:count+=1
 else:raise AssertionError('Foreign projection accepted')
print('PASS World approval exact scope:',count,'negative cases; metadata rehash cannot remove native invalidation; prior pins unchanged')
