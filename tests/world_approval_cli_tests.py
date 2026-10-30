"""Actual packaged CLI, synthetic files only; no sockets/SDK/live settings."""
from pathlib import Path
import argparse,json,subprocess,uuid,copy
p=argparse.ArgumentParser();p.add_argument('helper');p.add_argument('fixture');p.add_argument('out');p.add_argument('--baseline-only',action='store_true');p.add_argument('--calibrator');a=p.parse_args()
root=Path(a.out)/str(uuid.uuid4());root.mkdir(parents=True);seed=json.loads(Path(a.fixture).read_bytes());cases=[]
def run(*args):return subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=20)
def fixture(label,value=None):
 directory=root/label;directory.mkdir();target=directory/'config.json';target.write_text(json.dumps(seed if value is None else value),encoding='utf-8');return target
def record(label):cases.append(label);print('PASS '+label,flush=True)
def unchanged_except_group(before,after,translation):
 changed=any(row['world']['translation']!=translation for row in before['mappings'] if (row['source_id'],row['input_space'],row['input_revision'])==('pico','native',1))
 assert after['mapping_revision']==before['mapping_revision']+int(changed)
 expected=copy.deepcopy(before);expected['mapping_revision']+=int(changed)
 for row in expected['mappings']:
  if (row['source_id'],row['input_space'],row['input_revision'])==('pico','native',1):
   row['world']['translation']=translation
 canonical=lambda value:dict(value,mappings=sorted(value['mappings'],key=lambda row:(row['source_id'],row['device_id'])))
 assert canonical(after)==canonical(expected), 'World candidate differs from exact target group'
target=fixture('changed-four');before=json.loads(target.read_bytes());result=run(a.helper,target,'align','logical-pico-device-1',2,3,4);assert result.returncode==0,result.stderr
after=json.loads(target.read_bytes())
target_flags=lambda value:[r['space_approved'] for r in value['mappings'] if (r['source_id'],r['input_space'],r['input_revision'])==('pico','native',1)]
print(json.dumps(dict(before_approved=target_flags(before),after_approved=target_flags(after),before_revision=before['mapping_revision'],after_revision=after['mapping_revision'])),flush=True)
unchanged_except_group(before,after,[2,3,4]);assert list(target.parent.glob('*.backup-txn-*'));record('align changes intended World group without permission invalidation, exact non-target/profile/Mount/contract/extensions, one revision and backup')
if a.baseline_only:raise SystemExit(0)
target=fixture('same-world');before=json.loads(target.read_bytes());assert run(a.helper,target,'align','logical-pico-device-1',1,0,0).returncode==0;unchanged_except_group(before,json.loads(target.read_bytes()),[1,0,0]);record('same World explicit align preserves revision and geometry')
document=copy.deepcopy(seed);document['mappings'][4]['tracker_id']='logical-pico-device-1'
target=fixture('exact-binding-same-names',document);row=document['mappings'][0]
result=run(a.helper,target,'align-binding',row['source_id'],row['device_id'],2,3,4);assert result.returncode==0,result.stderr
unchanged_except_group(document,json.loads(target.read_bytes()),[2,3,4]);before=target.read_bytes()
assert run(a.helper,target,'align-binding',row['source_id'],row['device_id'],2,3,4).returncode==0 and target.read_bytes()==before
assert run(a.helper,target,'align-binding','missing',row['device_id'],9,3,4).returncode!=0 and target.read_bytes()==before
record('exact source/device alignment isolates same-name peers, same-W byte no-op and absent-key rejection')
for label,args,modify in [('missing',['missing','2','3','4'],None),('ambiguous',['logical-pico-device-1','2','3','4'],'ambiguous'),('nan',['logical-pico-device-1','nan','3','4'],None),('inf',['logical-pico-device-1','2','inf','4'],None),('overflow',['logical-pico-device-1','2','3','4'],'overflow')]:
 document=copy.deepcopy(seed)
 if modify=='ambiguous':document['mappings'][4]['tracker_id']='logical-pico-device-1'
 if modify=='overflow':document['mapping_revision']=4294967295
 target=fixture(label,document);before=target.read_bytes();result=run(a.helper,target,'align',*args);assert result.returncode!=0,(label,result.stdout);assert target.read_bytes()==before;assert not list(target.parent.glob('*.backup-txn-*'));record(label+' rejects without partial file/approval mutation')
target=fixture('stale-cas');expected=target.with_name('expected.json');expected.write_bytes(target.read_bytes());candidate=target.with_name('candidate.json');candidate.write_bytes(target.read_bytes())
assert run(a.helper,candidate,'align','logical-pico-device-1',2,3,4).returncode==0
assert run(a.helper,target,'policy','steamvr').returncode==0;before=target.read_bytes();result=run(a.helper,target,'commit',expected,candidate);assert result.returncode!=0;assert target.read_bytes()==before;record('stale exact-byte CAS cannot overwrite concurrent change with prepared World candidate')
target=fixture('successful-cas');expected=target.with_name('expected.json');expected.write_bytes(target.read_bytes());candidate=target.with_name('candidate.json');candidate.write_bytes(target.read_bytes());assert run(a.helper,candidate,'align','logical-pico-device-1',2,3,4).returncode==0
assert run(a.helper,target,'commit',expected,candidate).returncode==0;assert target.read_bytes()==candidate.read_bytes();unchanged_except_group(seed,json.loads(target.read_bytes()),[2,3,4]);record('native CAS commits exact World candidate with one revision')
if a.calibrator:
 for label,same in [('clear-changed',False),('clear-same',True)]:
  document=copy.deepcopy(seed)
  if same:
   for row in document['mappings'][:4]:row['world']['translation']=[0,0,0]
  target=fixture(label,document);result=run(a.calibrator,'--config',target,'--source','pico','--device','device-1','--tracker','logical-pico-device-1','--input-space','native','--input-revision','1','--clear','--apply')
  assert result.returncode==0,(result.stdout,result.stderr)
  unchanged_except_group(document,json.loads(target.read_bytes()),[0,0,0]);record(label+' actual CLI path before any VR_Init preserves unrelated geometry and permission metadata')
(root/'summary.json').write_text(json.dumps(dict(result='PASS',cases=len(cases),checks=cases,actual_sdk='NOT_RUN',physical_hil='NOT_RUN'),indent=2)+'\n')
print('PASS world approval CLI '+str(len(cases))+' cases; fixture files only',flush=True)
