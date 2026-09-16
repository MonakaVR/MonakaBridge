"""Real service + separate thin Direct consumer; fixed codec and actual upstream fixtures.
JSON here is a test stimulus/assertion format, never a production wire codec.
"""
import argparse,copy,json,math,os,re,select,socket,subprocess,time,uuid
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--bin',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];binary=a.bin.resolve()
run=root/'build'/('udp-'+uuid.uuid4().hex);run.mkdir(parents=True)
def listener(port=0):
 s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
 if os.name=='nt':s.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
 s.bind(('127.0.0.1',port));s.setblocking(False);return s
monaka,mirror=listener(),listener();reserved=[listener(),listener()]
ingress,direct=[s.getsockname()[1] for s in reserved]
for s in reserved:s.close()
ports=[ingress,monaka.getsockname()[1],direct,mirror.getsockname()[1]]
processes=[];received=[];mirrored=[]
def spawn(cmd):
 f=(run/('process-'+str(len(processes))+'.log')).open('w');proc=subprocess.Popen([str(x) for x in cmd],stdout=f,stderr=subprocess.STDOUT);processes.append((proc,f));return proc
def collect(seconds=.12):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  for sock in select.select([monaka,mirror],[],[],max(0,end-time.monotonic()))[0]:
   row=json.loads(sock.recv(4097));(received if sock is monaka else mirrored).append(row)
def poses():return [x for x in received if x['type']=='pose']
def send(sock,value):sock.sendto(json.dumps(value,separators=(',',':')).encode(),('127.0.0.1',ingress));collect()
config=json.loads((root/'config/bridge.example.json').read_text());config['bridge_id']='udp-regression-installation';config['policy']='both'
config['profiles']['pico-compat-v1']['convention']='fixture-pico-native'
def binding(source,device,tracker,profile,space,revision):
 return dict(source_id=source,device_id=device,tracker_id=tracker,profile=profile,input_space=space,input_revision=revision,world_space='test-world',world_revision=0,space_approved=True,world=dict(rotation=[0,0,0,1],translation=[10,20,30]),mount=dict(rotation=[0,0,0,1],translation=[0,0,0]))
config['mappings']=[binding(s,'same-device','logical-'+s,'pico-compat-v1','native',1) for s in ['pico','other']]
config['mappings'].append(binding('vive-installation-a','23:32:42:b7:82:d3','logical-vive','vive-unverified','map-a',1))
path=run/'bridge.json'
def save():
 pending=run/('config-'+uuid.uuid4().hex+'.json');pending.write_text(json.dumps(config));os.replace(pending,path)
save()
service_cmd=[binary/'monaka_bridge_service.exe',path,*ports,'--duration-ms',18000]
fixture=json.loads((root/'third_party/monaka-protocol-v2/fixtures/v2/observation.json').read_text())
# Read the actual TestPoseRoundTrip fixture values from the verified Task2 extraction.
legacy=(root/'build/upstream/task2/tests/protocol_tests.cpp').read_text()
def legacy_vector(field):return [float(x.strip()) for x in re.search(r'src\.'+field+r' = \{([^}]+)\}',legacy).group(1).split(',')]
fixture.update(source_id='pico',device_id='same-device',position=legacy_vector('positionMeters'),orientation=legacy_vector('orientationXyzw'),linear_velocity=dict(value=legacy_vector('linearVelocityMetersPerSec'),frame='space',evidence='measured'))
fixture['capabilities'].append('linear_velocity');fixture['coordinate_space']=dict(id='native',convention='fixture-pico-native',revision=1)
fixture['timestamp_ns']='1000000000000';fixture['sent_at_ns']='1000001000000'
other=copy.deepcopy(fixture);other.update(source_id='other',timestamp_ns='10',sent_at_ns='20')
s1,s2,sv=[socket.socket(socket.AF_INET,socket.SOCK_DGRAM) for _ in range(3)]
direct_log=run/'direct.jsonl';direct_log2=run/'direct-reopened.jsonl'
try:
 consumer=spawn([binary/'direct_udp_consumer.exe',direct,direct_log,8000]);service=spawn(service_cmd);collect(.25)
 assert service.poll() is None and consumer.poll() is None
 send(s1,fixture);send(s2,other)
 assert {x['tracker_id'] for x in poses()}=={'logical-pico','logical-other'}
 initial=next(x for x in poses() if x['tracker_id']=='logical-pico')
 expected=[v+t for v,t in zip(fixture['position'],[10,20,30])];assert initial['position']==expected
 q=fixture['orientation'];norm=math.sqrt(sum(v*v for v in q));expectedq=[-q[2]/norm,q[1]/norm,-q[0]/norm,q[3]/norm]
 assert all(abs(x-y)<1e-12 for x,y in zip(initial['orientation'],expectedq))
 # Duplicate and reordered observations do not receive a fresh sequence/age.
 n=len(poses());send(s1,fixture);assert len(poses())==n
 fixture['sequence']='2';send(s1,fixture);n=len(poses());fixture['sequence']='1';send(s1,fixture);assert len(poses())==n
 # Independent source restart, followed by retired-session replay.
 old=copy.deepcopy(fixture);collect(.6);fixture.update(session_id=str(uuid.uuid4()),clock_id=str(uuid.uuid4()),sequence='0');send(s1,fixture)
 other['sequence']='1';send(s2,other);n=len(poses());send(s1,old);assert len(poses())==n
 assert any(x['tracker_id']=='logical-other' and x['input']['sequence']=='1' for x in poses())
 # Partial components remain partial in MTP and Direct.
 fixture.update(sequence='1',position=None,modality='rotation_only',validity=dict(position=False,orientation=True),tracking_state='degraded',linear_velocity=None);send(s1,fixture)
 assert poses()[-1]['validity']==dict(position=False,orientation=True)
 # Actual v2 adapter output from synthetic VIVE status fixtures: mirror admission, with native profile blocked by default.
 vive=[json.loads(x) for x in (root/'tests/fixtures/vive-v2-synthetic-status-samples.jsonl').read_text().splitlines()]
 for v in vive:send(sv,v)
 assert sum(x['source_id']=='vive-installation-a' for x in mirrored)==7
 assert not any(x['tracker_id']=='logical-vive' for x in poses())
 # Explicit synthetic-only profile approval exercises downstream fixture compatibility.
 config['profiles']['vive-unverified'].update(approved=True,evidence='Synthetic Task3 fixture only; no hardware approval');config['mapping_revision']+=1;save();collect(.6)
 for v in vive:
  v=copy.deepcopy(v);v['sequence']=str(int(v['sequence'])+256);send(sv,v)
 assert any(x['tracker_id']=='logical-vive' and x['validity']['position'] for x in poses())
 # Disabled policy preserves utility mirror; wait for reconfiguration invalidations.
 config['policy']='disabled';config['mapping_revision']+=1;save();collect(.65);n=len(poses());m=len(mirrored)
 other['sequence']='2';send(s2,other);assert len(poses())==n and len(mirrored)>m
 # Duplicate loopback ingress owner is rejected without replacing the running service.
 duplicate=spawn(service_cmd);assert duplicate.wait(timeout=4)!=0 and service.poll() is None
 config['policy']='both';config['mapping_revision']+=1;save();collect(.65)
 # Stop and reopen the Direct consumer while source ingress continues.
 consumer.terminate();consumer.wait(timeout=4)
 other['sequence']='3';send(s2,other)
 consumer2=spawn([binary/'direct_udp_consumer.exe',direct,direct_log2,6000]);collect(.15)
 other['sequence']='4';send(s2,other)
 # Bridge restart creates a new MTP session; input backend session may be unchanged.
 old_session=poses()[-1]['session_id'];service.terminate();service.wait(timeout=4);collect(.6);service=spawn(service_cmd);collect(.2)
 other['sequence']='5';send(s2,other);assert poses()[-1]['session_id']!=old_session
 collect(.2)
 rows=[json.loads(x) for f in [direct_log,direct_log2] for x in f.read_text().splitlines()]
 lookup={(x['source_id'],x['tracker_id'],x['session_id'],int(x['sequence'])):x for x in poses()}
 matched=0
 for row in rows:
  mtp=lookup.get((row['source'],row['tracker'],row['session'],row['sequence']))
  if not mtp:continue
  assert row['valid']==(mtp['validity']['position'] and mtp['validity']['orientation'])
  if mtp['position'] is not None:assert row['position']==mtp['position']
  if mtp['orientation'] is not None:assert row['orientation']==mtp['orientation']
  assert -.1<=row['time_offset']<=.02
  matched+=1
 assert matched>=8,matched
 assert any(x['tracker']=='logical-pico' and not x['valid'] for x in rows)
 assert any(x['tracker']=='logical-other' and x['input_sequence']==4 for x in rows)
 assert any(x['tracker']=='logical-other' and x['input_sequence']==5 for x in rows)
 (run/'observations-and-mtp.json').write_text(json.dumps(dict(mtp=received,mirror=mirrored),indent=2))
 report=dict(result='PASS',direct_mtp_matches=matched,task2_fixture='verified extraction tests/protocol_tests.cpp TestPoseRoundTrip values',synthetic_vive_v2_samples=7,hardware='NOT RUN',cases=['separate service and Direct adapter','multi-source epochs','duplicate/reorder','source restart/retired session','explicit rotation_only','VIVE blocked and explicit synthetic profile','disabled mirror','route change','duplicate bind','consumer reopen','Bridge restart'])
 (root/'build/udp-regression-results.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
finally:
 for proc,f in processes:
  if proc.poll() is None:proc.terminate()
  proc.wait(timeout=4);f.close()
 for s in [monaka,mirror,s1,s2,sv]:s.close()
