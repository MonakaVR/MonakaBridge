from pathlib import Path
import hashlib,json,shutil,uuid
from calibration_gui_scope import FILES,PINS,PROTECTED,PIN,PARENT,approved_gui,historical,gui_project,digest
root=Path(__file__).resolve().parents[1];approved=approved_gui(root)
scratch=root/'build/gui-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
for name in approved:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
approved_gui(scratch,root);count=0
def reject(label):
 global count
 try:approved_gui(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('GUI accepted '+label)
for name in sorted(FILES|PINS|PROTECTED):
 p=scratch/name;raw=p.read_bytes();p.write_bytes(raw+b'\n# deliberate drift\n');reject(name);p.write_bytes(raw)
pin=scratch/PIN;raw=pin.read_bytes()
for label in ['parent','extra-file','missing-file','pin-hash','predecessor-hash','predecessor-presence','protected-hash']:
 value=json.loads(raw)
 if label=='parent':value['parent']='0'*40
 if label=='extra-file':value['files']['foreign.py']={}
 if label=='missing-file':value['files'].pop('ui/calibration_process_client.cs')
 if label=='pin-hash':value['pins']['dependencies/native-control-integration-baseline.json']='0'*64
 if label=='predecessor-hash':value['files']['ui/installation_control.cs']['parent']['sha256']='0'*64
 if label=='predecessor-presence':value['files']['ui/calibration_panel.cs']['parent']={'existed':True,'sha256':'0'*64}
 if label=='protected-hash':value['protected']['tools/steamvr_calibrator.cpp']='0'*64
 pin.write_text(json.dumps(value),encoding='utf-8');reject(label);pin.write_bytes(raw)
name='tests/integration_scope.py';p=scratch/name;source=p.read_bytes()
for old,new in [
 ('from calibration_gui_scope import approved_gui,gui_project,gui_project_text','from calibration_gui_scope import gui_project,gui_project_text'),
 ('extra=approved_gui(root,git_root)','extra=set()'),
 ('return FILES|set(PIN_OWNERS)|{PIN}|extra','return FILES|set(PIN_OWNERS)|{PIN}'),
 ('gui_project(root,name,(Path(root)/name).read_bytes(),git_root)','(Path(root)/name).read_bytes()'),
 ('raw=gui_project(root,name,raw,git_root,allow_older)','raw=raw'),
 ('text=gui_project_text(root,text)','text=text'),
]:
 text=source.decode();assert old in text;p.write_text(text.replace(old,new),encoding='utf-8');value=json.loads(raw);value['files'][name]['sha256']=digest(p.read_bytes());pin.write_text(json.dumps(value),encoding='utf-8');reject('connection despite metadata rewrite: '+old);p.write_bytes(source);pin.write_bytes(raw)
for name in sorted(PROTECTED):
 p=scratch/name;source=p.read_bytes();p.write_bytes(source+b'\n// deliberate protected drift\n');value=json.loads(raw);value['protected'][name]=digest(p.read_bytes());pin.write_text(json.dumps(value),encoding='utf-8');reject('protected hash redeclaration '+name);p.write_bytes(source);pin.write_bytes(raw)
for name in sorted(PINS):
 p=scratch/name;source=p.read_bytes();p.write_bytes(source+b'\n');value=json.loads(raw);value['pins'][name]=hashlib.sha256(p.read_bytes()).hexdigest();pin.write_text(json.dumps(value),encoding='utf-8');reject('old pin redeclaration '+name);p.write_bytes(source);pin.write_bytes(raw)
for name,e in json.loads(raw)['files'].items():
 current=(root/name).read_bytes();assert gui_project(root,name,current)==(historical(str(root),name) if e['parent']['existed'] else current)
 try:gui_project(root,name,current+b'foreign')
 except RuntimeError:count+=1
 else:raise RuntimeError('GUI foreign projection '+name)
p=scratch/'ui/calibration_panel.cs';source=p.read_bytes();p.unlink();reject('missing real input');p.write_bytes(source)
approved_gui(scratch,root)
print(f'PASS GUI exact actual scope {len(FILES)} files; {len(PINS)} raw immutable pins; {len(PROTECTED)} protected sources unchanged from {PARENT}; {count} drift/metadata/connection/redeclaration/projection/missing-input mutants rejected')
