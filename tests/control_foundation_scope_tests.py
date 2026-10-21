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

# Legacy guard acceptance has its own fixed source pins. These controls use
# private file copies, real immutable Git documents, and explicitly named mocks.
import textwrap
from types import SimpleNamespace
import control_foundation_scope as control
from p2r_runtime_scope import approved_runtime
from receiver_scope import verify as verify_receiver

for name in approved_runtime(root):
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
legacy_rejected=[];executed=[]
def reject_legacy(action,label):
 try:action()
 except (RuntimeError,AssertionError):legacy_rejected.append(label)
 else:raise RuntimeError('Legacy integration accepted '+label)
def mutate_legacy(name,raw,label,check=None):
 p=scratch/name;before=p.read_bytes()
 try:p.write_bytes(raw);reject_legacy(check or (lambda:approved_control(scratch,root)),label)
 finally:p.write_bytes(before)
def compare_guard(name,old,current,member='AGENTS.md'):
 # Execute the exact current comparison block, without rewriting its predicate.
 # Only the root location is a scratch fixture; archive bytes come from real Git.
 text=(root/name).read_text();start='            # BEGIN WS01A EXACT AGENTS COMPARISON\n';end='            # END WS01A EXACT AGENTS COMPARISON\n'
 block=text[text.index(start)+len(start):text.index(end)]
 def checked(r,b,o,c):
  executed.append((name,b));return control.verify_archive_agents(r,b,o,c,root)
 exec(compile(textwrap.dedent(block),name,'exec'),dict(root=scratch,BASE=control.LEGACY_GUARDS[name]['archive_base'],member=SimpleNamespace(name=member),old=old,current=current,verify_archive_agents=checked))

for name,e in control.LEGACY_GUARDS.items():
 raw=(scratch/name).read_bytes();original=control.historical(str(root),name)
 restored=control.legacy_guard_source(scratch,name,raw,root)
 if restored!=original:raise RuntimeError('Legacy guard original bytes not preserved: '+name)
 if control.predecessor(scratch,name,raw,root)!=original:raise RuntimeError('Legacy byte predecessor mismatch')
 if control.predecessor_text(root,raw.decode())!=original.decode():raise RuntimeError('Legacy text predecessor mismatch')
 old=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'show',e['archive_base']+':AGENTS.md'])
 compare_guard(name,old,current)
 mutate_legacy(name,b'!'+raw[1:],name+' source drift')
 mutate_legacy(name,original,name+' rollback')
 mutate_legacy(name,raw.replace(b'verify_archive_agents(root,BASE,old,current)',b'pass # document check bypass'),name+' archive document check bypass')
 mutate_legacy(name,raw.replace(b'assert current==old',b'assert True'),name+' frozen assertion relaxation')
 mutate_legacy(name,raw.replace(b'# END WS01A EXACT AGENTS COMPARISON',b'# END UNAPPROVED COMPARISON'),name+' marker drift')
 real_history=control.historical
 def changed_history(repo,path):
  value=real_history(repo,path)
  return value+b'\n# deliberate predecessor drift\n' if path==name else value
 with patch('control_foundation_scope.historical',side_effect=changed_history):
  reject_legacy(lambda:approved_control(scratch,root),name+' predecessor drift (Git-read boundary mock)')
 reject_legacy(lambda:compare_guard(name,b'!'+old[1:],current),name+' archive AGENTS byte drift')
 reject_legacy(lambda:compare_guard(name,current,current),name+' current document substituted into old archive')
 reject_legacy(lambda:compare_guard(name,b'frozen test\n',b'changed test\n','tests/frozen-test.py'),name+' other historical test source drift')

first=next(iter(control.LEGACY_GUARDS));base=control.LEGACY_GUARDS[first]['archive_base']
old=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'show',base+':AGENTS.md'])
mutate_legacy('AGENTS.md',current.replace(b'generic',b'GENERIC',1),'unapproved current document version')
guard=(scratch/'tests/control_foundation_scope.py').read_bytes()
mutate_legacy('tests/control_foundation_scope.py',guard.replace(control.UPSTREAM_SHA256.encode(),b'0'*64),'current document signature pin drift')
reject_legacy(lambda:compare_guard(first,old.replace(b'\n',b'\r\n'),current),'archive CRLF byte drift is not normalized')
crlf=current.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
mutate_legacy('AGENTS.md',crlf,'raw archive comparison rejects worktree CRLF at exact-byte boundary',lambda:control.verify_archive_agents(scratch,base,old,crlf,root))
reject_legacy(lambda:control.verify_archive_agents(scratch,control.BASE,old,current,root),'unapproved archive commit')
previous=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'show','7e49046af07a9e38bd81eeca5462c618c577f302:tests/control_foundation_scope.py'])
mutate_legacy('tests/control_foundation_scope.py',previous,'control guard pre-legacy rollback')
mutate_legacy('tests/control_foundation_scope.py',guard.replace(b"return FILES|{PIN,UPSTREAM_AGENTS}|set(LEGACY_GUARDS)",b"return set((Path(root)).rglob('*'))"),'control guard self-exception broadening')
real_output=subprocess.check_output
for names in [['tests/unapproved_guard.py'],['docs/extra.md','src/extra.cpp']]:
 def changed_paths(args,**kwargs):
  if 'diff' in args:return ('\n'.join(names)+'\n').encode()
  if 'ls-files' in args:return b''
  return real_output(args,**kwargs)
 with patch('control_foundation_scope.subprocess.check_output',side_effect=changed_paths):
  reject_legacy(lambda:verify_diff_scope(scratch),'unapproved Git changed-path boundary '+','.join(names))
for name in ['src/mapping/config_transaction.cpp','tools/bridge_service.cpp']:
 mutate_legacy(name,(scratch/name).read_bytes()+b'\n// deliberate native drift\n',name+' native drift',lambda:approved_runtime(scratch))

# Full receiver guard, real Git archive; only one worktree file-read is mocked.
real_read=Path.read_bytes;target=root/'tests/bridge_tests.cpp'
def changed_read(path):
 raw=real_read(path)
 return raw+b'\n// historical test drift\n' if path==target else raw
with patch.object(Path,'read_bytes',changed_read):
 reject_legacy(lambda:verify_receiver(root),'full receiver historical test read-boundary mutant')
entry=control.LEGACY_GUARDS[first]
with patch.dict(entry,reason='unreviewed reason'):
 reject_legacy(lambda:approved_control(scratch,root),'legacy approval reason drift')
with patch('control_foundation_scope.ARCHIVE_AGENTS_BLOB','0'*40):
 reject_legacy(lambda:compare_guard(first,old,current),'historical AGENTS signature pin drift')
if len(legacy_rejected)!=32:raise RuntimeError('Missing legacy negative control: '+str(len(legacy_rejected)))
if {name for name,base in executed}!=set(control.LEGACY_GUARDS):raise RuntimeError('Current guard document branches not executed')
if len(FILES)!=19:raise RuntimeError('Implementation allowlist broadened by legacy guards')
print('PASS legacy integration: 32 additional negative controls; both current comparison branches executed; two marker reconstructions byte-exact; 19 implementation paths unchanged')
for label in legacy_rejected:print('REJECTED '+label)
