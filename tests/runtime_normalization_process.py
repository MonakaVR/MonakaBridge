"""Actual service executable replay; JSON is stimulus, never a wire implementation."""
from pathlib import Path
import argparse,copy,json,subprocess,tempfile

SESSION='11111111-1111-1111-1111-111111111111'
DECL=['position','orientation','linear_velocity','angular_velocity']
def configuration():
 return dict(namespace='monaka.bridge',version=2,bridge_id='synthetic-bridge',mapping_revision=1,policy='monaka',pose_timeout_ms=500,
  profiles={'fixture':dict(convention='pico-output-a-v1',position_axes=[1,2,3],quaternion_axes=[1,2,3,4],approved=True,evidence='Explicit synthetic identity transform only',angular_space_verified=False)},
  mappings=[dict(source_id='backend-A',device_id='device-01',tracker_id='tracker-A',profile='fixture',input_space='pico-offline-map',input_revision=7,world_space='room-A',world_revision=0,space_approved=True,
   world=dict(rotation=[0,0,0,1],translation=[0,0,0]),mount=dict(rotation=[0,0,0,1],translation=[0,0,0]),runtime_contract=dict(id='pico-explicit-pqlv-to-bridge-pqlvav',provenance='Accepted PICO Observation adapter explicitly advertises position/orientation/linear_velocity; accepted Bridge calibration supports derived linear/angular velocity; P0/P1 fixture contract. Synthetic mapping, no physical approval.',input_policy='STRICT',output_policy='LEGACY_DYNAMIC',input_declaration=DECL[:3],output_declaration=DECL))])

def replay(exe,config,rows,directory,name,mtp=False):
 directory.mkdir(parents=True,exist_ok=True)
 cfg=directory/(name+'.config.json');inp=directory/(name+'.input.jsonl');out=directory/(name+'.output.jsonl')
 cfg.write_text(json.dumps(config),encoding='utf-8');inp.write_text(''.join(json.dumps(x)+'\n' for x in rows),encoding='utf-8')
 cmd=[str(exe),'--replay',str(cfg),str(inp),str(out),SESSION]+(['--mtp'] if mtp else [])
 result=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
 (directory/(name+'.process.json')).write_text(json.dumps(dict(command=cmd,exit_code=result.returncode,stdout=result.stdout,stderr=result.stderr),indent=2),encoding='utf-8')
 if result.returncode:raise RuntimeError(result.stderr)
 return [json.loads(x) for x in out.read_text().splitlines()]

def equality(rows):
 count=0
 for row in rows:
  for pair in row['normalization_pairs']:
   before,after=pair['before'],pair['after']
   if {k:v for k,v in before.items() if k!='capabilities'}!={k:v for k,v in after.items() if k!='capabilities'}:raise AssertionError('Non-capability mutation')
   count+=1
 return count

def main(exe,directory):
 root=Path(__file__).resolve().parents[1];fixtures=root/'third_party/mbip-p1/tests/fixtures/p1'
 old=[json.loads((fixtures/(name+'.json')).read_text()) for name in ['legacy-bridge-first-pose','legacy-bridge-second-derived-pose','legacy-bridge-state']]
 rows=[dict(envelope=x,received_at_ns=100000000+i*20000000) for i,x in enumerate(old)]
 result=replay(exe,configuration(),rows,directory,'old-three',True)
 assert all(x['accepted'] for x in result) and len(result)==3
 wires=[r['emitted'][0] for r in result];assert all(x['capabilities']==DECL for x in wires)
 pairs=equality(result)
 negatives={}
 for field,value in [('source_id','foreign'),('tracker_id','foreign'),('publisher_id','foreign'),('session_id','33333333-3333-3333-3333-333333333333'),('capabilities',DECL+['linear_acceleration'])]:
  changed=copy.deepcopy(old[0]);changed[field]=value
  r=replay(exe,configuration(),[dict(envelope=changed,received_at_ns=100000000)],directory,'reject-'+field,True)
  assert not r[0]['accepted'] and not r[0]['emitted'];negatives[field]=r[0].get('reject_reason')
 config=configuration();del config['mappings'][0]['runtime_contract']['output_policy']
 strict=replay(exe,config,rows[:1],directory,'strict-default',True);assert not strict[0]['accepted']
 summary=dict(result='PASS',runtime_inputs=3,runtime_outputs=3,non_capability_pairs=pairs,non_capability_mutations=0,explicit_declaration=DECL,fail_closed=negatives,default_policy='STRICT',core_capability_conflict_counter='NOT PRESENT in accepted Core; reference probe measured separately')
 (directory/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8');print(json.dumps(summary));return summary

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--service',type=Path,required=True);p.add_argument('--work',type=Path);a=p.parse_args()
 if a.work:main(a.service,a.work)
 else:
  with tempfile.TemporaryDirectory(prefix='p2r-runtime-') as tmp:main(a.service,Path(tmp))
