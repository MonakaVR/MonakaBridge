from pathlib import Path
import json,shutil,subprocess,uuid
from unittest.mock import patch
from control_foundation_scope import FILES,PIN,OLD_PINS,UPSTREAM_AGENTS,UPSTREAM_BLOB,UPSTREAM_SHA256,approved_control,historical,verify_diff_scope,digest,predecessor,predecessor_text
root=Path(__file__).resolve().parents[1];approved_control(root);verify_diff_scope(root)
scratch=root/'build/control-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
for name in approved_control(root)|OLD_PINS:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
approved_control(scratch,root);count=0
for name in ['ui/control_models.cs','ui/installation_control.cs','ui/steamvr_control_wpf.cs','tests/capability_runtime_scope.py',*sorted(OLD_PINS)]:
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
p.write_bytes(old);assert count==9
print('PASS WS01a exact control scope; 9 source/old-pin/rollback/scope mutants rejected')

additional=[]
def reject(action,label):
 try:action()
 except RuntimeError:additional.append(label)
 else:raise RuntimeError('Integration guard accepted '+label)
def mutate(name,raw,label):
 p=scratch/name;old=p.read_bytes()
 try:p.write_bytes(raw);reject(lambda:approved_control(scratch,root),label)
 finally:p.write_bytes(old)
agents=scratch/UPSTREAM_AGENTS;current=agents.read_bytes()
mutate(UPSTREAM_AGENTS,b'!'+current[1:],'AGENTS one-character drift')
agents.unlink()
try:reject(lambda:approved_control(scratch,root),'AGENTS missing')
finally:agents.write_bytes(current)
mutate(UPSTREAM_AGENTS,historical(str(root),UPSTREAM_AGENTS),'AGENTS old-commit substitution')

manifest=json.loads((scratch/PIN).read_text(encoding='utf-8'))
value=json.loads(json.dumps(manifest));value['files'][UPSTREAM_AGENTS]={}
mutate(PIN,json.dumps(value).encode(),'AGENTS historical-projection/filename exception')
guard=(scratch/'tests/control_foundation_scope.py').read_bytes()
mutate('tests/control_foundation_scope.py',guard.replace(b' approved_upstream_agents(root,repo)',b' pass # bypass upstream document validation'),'guard validation bypass drift')
old_guard=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'show','1b02d3628e11254bb0584e546c8a17713a6e6e26:tests/control_foundation_scope.py'])
mutate('tests/control_foundation_scope.py',old_guard,'pre-integration guard rollback')
value=json.loads(json.dumps(manifest));value['base_commit']='3473624a451ac172c36ea46cc5d897d84eadad43'
mutate(PIN,json.dumps(value).encode(),'fixed WS01a BASE replacement')
value=json.loads(json.dumps(manifest));value['files']['ui/control_models.cs']['existed_at_base']=False
mutate(PIN,json.dumps(value).encode(),'base predecessor existence drift')
value=json.loads(json.dumps(manifest));value['files']['ui/control_models.cs']['base_sha256']='0'*64
mutate(PIN,json.dumps(value).encode(),'historical predecessor SHA drift')

# Keep mutations in the private copy. Supply the corresponding Git change list
# at the external-command boundary; all pin/history/document checks stay real.
real_output=subprocess.check_output
for name in ['UNAPPROVED.md','docs/unapproved.md','tools/bridge_service.cpp']:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'unapproved change\n')
 def listed(args,**kwargs):
  if 'diff' in args:return (name+'\n').encode()
  if 'ls-files' in args:return b''
  return real_output(args,**kwargs)
 with patch('control_foundation_scope.subprocess.check_output',side_effect=listed):
  reject(lambda:verify_diff_scope(scratch),'unapproved changed path: '+name)
if len(additional)!=12:raise RuntimeError('Missing integration negative control')

# CRLF normalization is explicit. The new AGENTS document remains unchanged
# through both reconstruction functions; it is not an old implementation pin.
agents.write_bytes(current.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
approved_control(scratch,root)
if digest(agents.read_bytes())!=UPSTREAM_SHA256:raise RuntimeError('AGENTS LF digest mismatch')
agents.write_bytes(current)
if predecessor(scratch,UPSTREAM_AGENTS,current,root)!=current:raise RuntimeError('AGENTS projected into old implementation')
if predecessor_text(scratch,current.decode())!=current.decode():raise RuntimeError('AGENTS text projected into old implementation')
for name,entry in manifest['files'].items():
 raw=(scratch/name).read_bytes();expected=historical(str(root),name) if entry['existed_at_base'] else raw
 if digest(predecessor(scratch,name,raw,root))!=digest(expected):raise RuntimeError('Implementation predecessor mismatch: '+name)
 if predecessor_text(scratch,raw.decode())!=expected.decode():raise RuntimeError('Implementation text predecessor mismatch: '+name)
if len(FILES)!=19:raise RuntimeError('WS01a implementation allowlist changed')
print('PASS integration: 12 additional mutants rejected; exact upstream AGENTS blob '+UPSTREAM_BLOB+' / LF SHA256 '+UPSTREAM_SHA256+'; 19 implementation predecessors checked; AGENTS not projected')
for label in additional:print('REJECTED '+label)
