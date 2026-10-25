from pathlib import Path
import hashlib,json,shutil,uuid
from integration_scope import FILES,PIN,PARENTS,PIN_OWNERS,OVERLAP,PROTECTED_PARENTS,approved_integration,historical,project,digest,verify_diff_scope
root=Path(__file__).resolve().parents[1];approved=approved_integration(root);verify_diff_scope(root)
scratch=root/'build/j0-composition-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
for name in approved:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
approved_integration(scratch,root);count=0
def reject(label):
 global count
 try:approved_integration(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('J0 accepted '+label)
for name in sorted(FILES|set(PIN_OWNERS)):
 p=scratch/name;raw=p.read_bytes();p.write_bytes(raw+b'\n# deliberate drift\n');reject('current/pin drift '+name);p.write_bytes(raw)
pin=scratch/PIN;raw=pin.read_bytes()
for label in ['base','native-parent','control-parent','scope','overlap','native-predecessor','control-predecessor','native-presence','control-presence','pin-owner','pin-hash']:
 v=json.loads(raw)
 if label=='base':v['common_base']='0'*40
 if label.endswith('-parent'):v['parents'][label.split('-')[0]]='0'*40
 if label=='scope':v['files']['tools/steamvr_calibrator.cpp']={}
 if label=='overlap':v['overlap']=OVERLAP[:2]
 if label.endswith('-predecessor'):v['files']['tests/capability_runtime_scope.py']['parents'][label.split('-')[0]]['sha256']='0'*64
 if label.endswith('-presence'):v['files']['tests/capability_runtime_scope.py']['parents'][label.split('-')[0]]['existed']=False
 if label=='pin-owner':v['pins']['dependencies/native-capture-baseline.json']={'control':'0'*64}
 if label=='pin-hash':v['pins']['dependencies/native-capture-baseline.json']['native']='0'*64
 pin.write_text(json.dumps(v),encoding='utf-8');reject('metadata '+label);pin.write_bytes(raw)
for name in OVERLAP:
 current=(root/name).read_bytes()
 for view in PARENTS:
  expected=historical(str(root),view,name)
  assert project(root,view,name,current)==expected
  p=scratch/name;p.write_bytes(expected);reject('overlap rollback '+view+':'+name);p.write_bytes(current)
 try:project(root,'native',name,current+b'drift')
 except RuntimeError:count+=1
 else:raise RuntimeError('J0 accepted unknown composite projection')
 if historical(str(root),'native',name)!=historical(str(root),'control',name):
  try:project(root,'native',name,historical(str(root),'control',name))
  except RuntimeError:count+=1
  else:raise RuntimeError('J0 accepted foreign parent as native')
# Mutants also update their own declared current hash. The independent AST
# connection check must still catch a dropped branch or wrong explicit view.
name='tests/capability_runtime_scope.py';p=scratch/name;source=p.read_bytes()
for lost in ['native','control']:
 text=source.decode();old='approved_measurement(root,git_root)|approved_control(root,git_root)';new='approved_control(root,git_root)' if lost=='native' else 'approved_measurement(root,git_root)';assert old in text
 p.write_text(text.replace(old,new),encoding='utf-8');v=json.loads(raw);v['files'][name]['sha256']=digest(p.read_bytes());pin.write_text(json.dumps(v),encoding='utf-8');reject('lost '+lost+' delegation despite hash rewrite');p.write_bytes(source);pin.write_bytes(raw)
for name,own,other in [('tests/native_capture_scope.py','native','control'),('tests/control_foundation_scope.py','control','native')]:
 p=scratch/name;source=p.read_bytes();text=source.decode().replace("integration_project(root,'"+own+"'","integration_project(root,'"+other+"'")
 p.write_text(text,encoding='utf-8');v=json.loads(raw);v['files'][name]['sha256']=digest(p.read_bytes());pin.write_text(json.dumps(v),encoding='utf-8');reject('wrong explicit '+own+' view despite hash rewrite');p.write_bytes(source);pin.write_bytes(raw)
name='src/routing/bridge.cpp';p=scratch/name;source=p.read_bytes();p.write_bytes(source+b'\n// deliberate product drift\n');v=json.loads(raw);v['files'][name]['sha256']=digest(p.read_bytes());pin.write_text(json.dumps(v),encoding='utf-8');reject('protected product drift despite hash rewrite');p.write_bytes(source);pin.write_bytes(raw)
# A missing scratch input cannot produce an apparent mutant PASS.
p=scratch/'ui/control_models.cs';source=p.read_bytes();p.unlink();reject('missing required actual source');p.write_bytes(source)
approved_integration(scratch,root)
assert count==len(FILES|set(PIN_OWNERS))+11+6+3+3+4+1+1
print(f'PASS J0 {len(PROTECTED_PARENTS)} actual product sources equal their fixed parent; actual composite; {count} source/11 immutable pins/parent/overlap/delegation/view/missing-input mutants rejected; two explicit parent projections; no current-byte cache')
