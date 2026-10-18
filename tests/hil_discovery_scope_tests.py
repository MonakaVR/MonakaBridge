from pathlib import Path
import json,shutil,uuid
from hil_discovery_scope import FILES,PINS,PROTECTED,PIN,approved_hil,digest,hil_project
from calibration_b4_scope import approved_b4
def verify(root):
 root=Path(root);approved=approved_hil(root);scratch=root/'build/hil-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True);count=0
 for name in approved:
  p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
 def reject():
  nonlocal count
  try:approved_hil(scratch,root)
  except (RuntimeError,ValueError,KeyError):count+=1
  else:raise RuntimeError('HIL admitted mutant')
 approved_hil(scratch,root)
 for name in sorted(FILES|PINS|PROTECTED):
  p=scratch/name;raw=p.read_bytes();p.write_bytes(raw+b'\n// deliberate drift\n');reject();p.write_bytes(raw)
 pin=scratch/PIN;raw=pin.read_bytes()
 for field in ['parent','files','pins','protected']:
  v=json.loads(raw)
  if field=='parent':v[field]='0'*40
  else:v[field].pop(next(iter(v[field])))
  pin.write_text(json.dumps(v),encoding='utf-8');reject();pin.write_bytes(raw)
 for name,old,new in [('ui/discovery_models.cs','RequireOffline(snapshot,old)','/* bypass */'),('ui/discovery_models.cs','if(Text(b,"profile")!=profile)','if(false)'),('ui/discovery_models.cs','binding["mount"]=Identity()','binding["mount"]=old["mount"]'),('src/observation/discovery.cpp','const bool accepted=bridge.receive(raw,peer,now);','const bool accepted=true;'),('tests/calibration_b4_scope.py','extra=approved_hil(root,git_root)','extra=set()')]:
  p=scratch/name;source=p.read_bytes();text=source.decode();assert old in text;p.write_text(text.replace(old,new),encoding='utf-8');v=json.loads(raw);v['files'][name]['sha256']=digest(p.read_bytes());pin.write_text(json.dumps(v),encoding='utf-8');reject();p.write_bytes(source);pin.write_bytes(raw)
 # A historical view cannot grant approval to changed actual input, even after
 # a successful check on the same path (no cache of current admission).
 approved_hil(scratch,root);name='ui/discovery_models.cs';p=scratch/name;source=p.read_bytes();p.write_bytes(source+b'\n// later drift\n');assert hil_project(scratch,'unrelated',b'view',root)==b'view';reject()
 try:approved_b4(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('B4 parent admitted changed actual HIL source')
 p.write_bytes(source);approved_hil(scratch,root);print('PASS HIL exact current-source scope, 15 immutable accepted pins, protected sources, '+str(count)+' mutants; no current admission cache')
if __name__=='__main__':verify(Path(__file__).resolve().parents[1])
