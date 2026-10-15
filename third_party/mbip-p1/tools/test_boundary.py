"""Bounded executable old producer -> explicit MBIP normalization -> frozen Core comparison."""
import copy,json,os,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
F=R/'tests/fixtures/p1';B=R/'build/boundary-tests';B.mkdir(parents=True,exist_ok=True)
JAVA=pathlib.Path(os.environ.get('JAVA_HOME','C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot'))/'bin/java.exe'
cp=os.pathsep.join(str(p) for p in [R/'jvm/build/classes/kotlin/test',R/'jvm/build/libs/monaka-bridge-capabilities.jar',R/'third_party/mvip/jvm/libs/monaka-input-protocol.jar',R/'third_party/mvip/jvm/libs/gson-2.11.0.jar',R/'third_party/mvip/jvm/libs/kotlin-stdlib-2.3.10.jar'])
commands=[[str(R/'build/native/Release/mbip_capabilities_runner.exe')],[str(JAVA),'-Dfile.encoding=UTF-8','-cp',cp,'dev.monaka.bridgeinput.NormalizerRunner']]
encoders=[[str(R/'build/native/Release/mvip_boundary_encoder.exe')],[str(JAVA),'-Dfile.encoding=UTF-8','-cp',cp,'dev.monaka.protocol.v2.Runner']]
tokens=json.loads((R/'third_party/mvip/schema/capabilities.json').read_text())['tokens']
count=0
serialized_directions=0
def read(name):return json.loads((F/(name+'.json')).read_text())
def save(name,data):
 p=B/(name+'.json');p.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8');return p
def binding(name):return read(name+'.binding')
def invoke(command,*args):return subprocess.check_output(command+list(map(str,args)),encoding='utf-8',timeout=30).strip()
def golden(actual,states):
 for t in tokens:
  expected=states.get(t,'UNSUPPORTED');c=actual['channels'][t]
  assert c['state']==expected,(t,c,expected)
  assert c['valid']==(expected=='VALID'),(t,c,expected)
  assert c['supported']==(expected!='UNSUPPORTED'),(t,c,expected)
def check(name,contract,mode='STRICT',states=None,error=None,message=None):
 global count,serialized_directions
 msg=message if message is not None else read(name)
 bp=save('binding-'+name,contract);mp=save('message-'+name,msg);outputs=[]
 for i,command in enumerate(commands):
  serialized=invoke(encoders[1-i],mp)
  # Malformed/unknown inputs still exercise MBIP's own ingress failure path.
  if not serialized.startswith('ERROR:'):
   ingress=save('serialized-'+str(i)+'-'+name,json.loads(serialized));serialized_directions+=1
  else:ingress=mp
  out=invoke(command,bp,ingress,mode);count+=1
  if error:assert out=='ERROR:'+error,(name,out,error)
  else:
   assert not out.startswith('ERROR:'),(name,out)
   value=json.loads(out);golden(value,states)
   for key,expected in msg.items():
    if key!='capabilities':assert value['wire'][key]==expected,('rewritten field',name,key)
   assert set(value['wire']['capabilities'])==set(contract['declaration'])
   outputs.append(value)
 if outputs:assert outputs[0]==outputs[1],('native/JVM mapping mismatch',name)
 return outputs
obs=binding('observation-contract');legacy=binding('legacy-bridge-contract')
valid=dict(position='VALID',orientation='VALID',linear_velocity='VALID')
lost=dict(position='INVALID',orientation='INVALID',linear_velocity='ABSENT')
for name,states in [('pico-like-supported-valid',valid),('pico-like-position-loss',lost),('pico-like-full-loss-stale',lost),('supported-optional-absent',dict(position='VALID',orientation='VALID',linear_velocity='ABSENT')),('set-order-reversed',valid),('admission-candidate',lost),('admission-admitted',valid)]:check(name,obs,states=states)
for name,error in [('unknown-capability','UnsupportedValue'),('duplicate-capability','InconsistentValidity'),('unsupported-derivative','InconsistentValidity'),('unsupported-valid-position','InconsistentValidity')]:check(name,obs,error=error)
for name,decl,states in [('unsupported-position-diagnostic',['orientation'],dict(orientation='VALID')),('empty-no-support',[],{}),('synthetic-same-tracker-position-loss',['position','orientation','angular_velocity'],dict(position='INVALID',orientation='VALID',angular_velocity='VALID')),('metadata-not-pose-validity',tokens,{**{t:'ABSENT' for t in tokens[:5]},'battery_fraction':'VALID','charging':'VALID'})]:
 config=copy.deepcopy(obs);config['declaration']=decl;check(name,config,states=states)
diagnostics=copy.deepcopy(obs);diagnostics['declaration']=tokens[:5]
check('loss-retained-derivative-diagnostics',diagnostics,states={t:'INVALID' for t in tokens[:5]})

# Regression: preserving a capability MUST NOT make an invalid sample valid.
candidate=check('candidate-validity-regression',obs,states=lost,message=read('admission-candidate'))[0]
mutant=copy.deepcopy(candidate);mutant['channels']['position']['valid']=mutant['channels']['position']['supported']
try:golden(mutant,lost)
except AssertionError:pass
else:raise AssertionError('Golden oracle failed to detect capability=>valid consumer mutant')

# Regression: deleting a capability on transient invalidity MUST fail STRICT.
bad=read('admission-candidate');bad['capabilities'].remove('position')
check('producer-drop-on-invalid',obs,error='CapabilitiesChanged',message=bad)
check('explicit-legacy-restores-declaration',obs,'LEGACY_DYNAMIC',states=lost,message=bad)
unbound=copy.deepcopy(obs);unbound['provenance']='';check('unbound-legacy',unbound,'LEGACY_DYNAMIC',error='MissingDeclaration',message=read('pico-like-supported-valid'))
small=copy.deepcopy(obs);small['declaration']=['position','orientation'];check('unprovisioned-channel',small,'LEGACY_DYNAMIC',error='UndeclaredCapability',message=read('pico-like-supported-valid'))

# Caller-admitted new session, not automatic session takeover; no space rotation.
check('session-restart',obs,error='SessionMismatch')
rotated=copy.deepcopy(obs);rotated['session']=read('session-restart')['session_id'];check('session-restart',rotated,states=valid)
changed=read('session-restart');changed.update(capabilities=['position','orientation'],linear_velocity=None)
newdecl=copy.deepcopy(rotated);newdecl['declaration']=['position','orientation'];check('changed-support-new-session',newdecl,states=dict(position='VALID',orientation='VALID'),message=changed)
check('same-device-other-source',obs,error='IdentityMismatch')
check('same-source-tracker-other-publisher',legacy,'LEGACY_DYNAMIC',error='IdentityMismatch')
other=read('legacy-bridge-first-pose');other['tracker_id']='another-tracker';check('foreign-tracker',legacy,'LEGACY_DYNAMIC',error='IdentityMismatch',message=other)

names=['pico-like-supported-valid','admission-candidate','admission-admitted']
for field in ['source_id','device_id','session_id','clock_id','coordinate_space','capabilities']:
 assert all(read(n)[field]==read(names[0])[field] for n in names),field
assert [read(n)['validity']['position'] for n in names]==[True,False,True]
assert [int(read(n)['sequence']) for n in names]==[1,2,3]
assert [int(read(n)['timestamp_ns']) for n in names]==[100,130,160]
assert all('CANDIDATE' not in json.dumps(read(n)) and 'raw_valid' not in read(n) for n in names)

# Same physical/logical identity, multi-channel support; protocol-only fixture.
same=binding('observation-contract');same['declaration']=['position','orientation','angular_velocity']
loss=read('synthetic-same-tracker-position-loss');loss.update(sequence='2',timestamp_ns='130',sent_at_ns='150')
full=copy.deepcopy(loss);full.update(sequence='1',timestamp_ns='100',sent_at_ns='120',validity=dict(position=True,orientation=True),modality='full',tracking_state='tracked')
check('synthetic-same-full',same,states=dict(position='VALID',orientation='VALID',angular_velocity='VALID'),message=full)
check('synthetic-same-loss',same,states=dict(position='INVALID',orientation='VALID',angular_velocity='VALID'),message=loss)
for key in ['source_id','device_id','session_id','clock_id','coordinate_space','capabilities']:assert full[key]==loss[key]

# Exact frozen Core capability block, not a production runtime/receiver replacement.
sequence=['legacy-bridge-first-pose','legacy-bridge-second-derived-pose','legacy-bridge-state']
before=invoke([str(JAVA),'-cp',cp,'dev.monaka.tracking.FrozenCoreProbe'],*[F/(n+'.json') for n in sequence])
assert json.loads(before)['conflict'] is True,'P0 mismatch must reproduce BEFORE adaptation'
after_paths=[[],[]]
for name,states in zip(sequence,[{**valid,'angular_velocity':'ABSENT'},{**valid,'angular_velocity':'VALID'},dict(position='ABSENT',orientation='ABSENT',linear_velocity='ABSENT',angular_velocity='ABSENT')]):
 check(name,legacy,error='CapabilitiesChanged' if name!='legacy-bridge-second-derived-pose' else None,states=states)
 outputs=check(name,legacy,'LEGACY_DYNAMIC',states=states)
 for i,out in enumerate(outputs):after_paths[i].append(save('normalized-'+str(i)+'-'+name,out['wire']))
after=[]
for paths in after_paths:
 result=json.loads(invoke([str(JAVA),'-cp',cp,'dev.monaka.tracking.FrozenCoreProbe'],*paths));assert result['conflict'] is False and result['conflicts']==0;after.append(result)

# Wire2.2 Common World is decoded normally by MVIP, but explicitly out of scope for capability projection.
for row in json.loads((R/'tests/fixtures/v2.2/index.json').read_text()):
 message=json.loads((R/'tests/fixtures/v2.2'/row['file']).read_text()) if row['error'] is None else None
 if message is not None:check('wire22-'+pathlib.Path(row['file']).stem,legacy,error='UnsupportedMessage',message=message)

summary=dict(status='PASS',native_jvm_boundary_probes=count,cross_repo_serialized_directions=serialized_directions,negative_mutants_detected=2,p0_before=json.loads(before),p1_after=after,original_capability_mismatch='RESOLVED',scope='explicit P1 adapter + actual frozen Core capability block; no production receiver/HIL migration',mvip_source_commit=json.loads((R/'dependencies/mvip.lock.json').read_text())['source_commit'],mbip_source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=R).decode().strip())
(R/'build/test-results.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8');print(json.dumps(summary))
