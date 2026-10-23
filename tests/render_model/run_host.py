"""Fresh package copies + exact held native DLL Host children. No existing owner control."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import uuid

S1='00000000-0000-4000-8000-000000000001'
MODEL='{monaka_bridge}pico_motion_tracker'
SERIAL='monaka-direct:313a7073:74'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host',type=Path,required=True)
p.add_argument('--package',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--baseline-dll',type=Path)
a=p.parse_args()
root=a.out.resolve()/('run-'+uuid.uuid4().hex);root.mkdir(parents=True)
records=[]

def fixture():
    contract=dict(id='synthetic-only',provenance='D2 explicit synthetic declaration; no physical approval',input_policy='STRICT',output_policy='STRICT',input_declaration=['position','orientation'],output_declaration=['position','orientation'])
    binding=dict(source_id='s',device_id='physical-pico',tracker_id='t',profile='explicit',input_space='native',input_revision=1,world_space='world',world_revision=1,space_approved=True,world=dict(rotation=[0,0,0,1],translation=[0,0,0]),mount=dict(rotation=[0,0,0,1],translation=[0,0,0]),runtime_contract=contract)
    config=dict(namespace='monaka.bridge',version=2,bridge_id='p',mapping_revision=1,policy='steamvr',pose_timeout_ms=500,
        profiles={'explicit':dict(convention='fixture-native',position_axes=[1,2,3],quaternion_axes=[1,2,3,4],approved=True,evidence='D2 synthetic-only approved mapping',angular_space_verified=False)},mappings=[binding])
    assignment=dict(model_id='pico_motion_tracker',device_kind='pico_motion_tracker',source_id='s',device_id='physical-pico',tracker_id='t',runtime_serial=SERIAL,input_session=S1)
    selection=dict(namespace='monaka.bridge.presentation',version=1,publisher_session=S1,config_file='bridge-config.json',config_sha256='',assignments=[assignment])
    return config,selection

def case(name,mode='generic',mutation='none',edit=None,baseline=False):
    package=root/name/'package';shutil.copytree(a.package,package)
    config,selection=fixture()
    folder=package/'resources/presentation'
    if edit:edit(config,selection,package)
    if selection is not None:
        raw=json.dumps(config,indent=2).encode();(folder/'bridge-config.json').write_bytes(raw)
        selection['config_sha256']=hashlib.sha256(raw).hexdigest()
        if name=='wrong-config-hash':selection['config_sha256']='0'*64
        if name=='default-unassigned':selection=dict(namespace='monaka.bridge.presentation',version=1,assignments=[])
        data=json.dumps(selection,indent=2)
        if name=='duplicate-key':data=data.replace('"version": 1','"version": 1, "version": 1')
        if name=='corrupt-sidecar':data='{bad-json'
        (folder/'selection.json').write_text(data,encoding='utf-8')
    if name=='missing-config':(folder/'bridge-config.json').unlink()
    if name=='missing-selection':(folder/'selection.json').unlink()
    dll=package/'bin/win64/driver_monaka_bridge.dll'
    if baseline:shutil.copyfile(a.baseline_dll,dll)
    before=hashlib.sha256(dll.read_bytes()).hexdigest()
    immutable={str(p.relative_to(package)):hashlib.sha256(p.read_bytes()).hexdigest() for p in package.rglob('*') if p.is_file()}
    command=[str(a.host.resolve()),str(dll),str(root/name/'observation'),mode,mutation]
    start=time.monotonic()
    with (root/name/'stdout.txt').open('xb') as stdout,(root/name/'stderr.txt').open('xb') as stderr:
        proc=subprocess.Popen(command,stdout=stdout,stderr=stderr)
        try:code=proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill();proc.wait();raise RuntimeError('D2 owned child timeout: '+name)
    value=json.loads((root/name/'observation/result.json').read_text())
    record=dict(name=name,pid=proc.pid,exit_code=code,seconds=time.monotonic()-start,command=command,result=value,child_exited=True)
    records.append(record);(root/'children.json').write_text(json.dumps(records,indent=2)+'\n')
    assert before==hashlib.sha256(dll.read_bytes()).hexdigest()
    assert immutable=={str(p.relative_to(package)):hashlib.sha256(p.read_bytes()).hexdigest() for p in package.rglob('*') if p.is_file()},'native child modified package/config'
    if code==77:
        (root/'D2_RESULT.json').write_text(json.dumps(dict(result='BLOCKED',reason='BLOCKED_PORT_IN_USE',records=records),indent=2));raise SystemExit(77)
    if baseline:
        assert code==1 and value['result']=='FAIL','old DLL unexpectedly implements D2'
    else:assert code==0 and value['result']=='PASS',name+': '+str(value)

case('default-unassigned')
case('explicit-pico',mode='pico')
case('mixed-pico-altra',mode='mixed')
for mutation in ['publisher','source','device','tracker','revision','session','input-session']:
    case('unknown-'+mutation,mutation=mutation)
for mutation in ['rebind','generation','input-restart','backend-restart','expiry']:
    case('revoke-'+mutation,mode='revoke',mutation=mutation)
case('wrong-config-hash')
case('missing-config')
case('missing-selection')
case('duplicate-key')
case('corrupt-sidecar')
case('path-traversal',edit=lambda c,s,p:s.update(config_file='../bridge-config.json'))
case('absolute-path',edit=lambda c,s,p:s.update(config_file='C:/forbidden.json'))
case('unknown-model',edit=lambda c,s,p:s['assignments'][0].update(model_id='other'))
case('altra-assignment',edit=lambda c,s,p:s['assignments'][0].update(device_kind='altra'))
case('wrong-runtime-serial',edit=lambda c,s,p:s['assignments'][0].update(runtime_serial=SERIAL+'1'))
case('duplicate-assignment',edit=lambda c,s,p:s['assignments'].append(copy.deepcopy(s['assignments'][0])))
case('unknown-field',edit=lambda c,s,p:s.update(unrecognized=True))
case('stale-config-generation',edit=lambda c,s,p:c.update(mapping_revision=2))
case('unapproved-profile',edit=lambda c,s,p:c['profiles']['explicit'].update(approved=False))
case('unapproved-space',edit=lambda c,s,p:c['mappings'][0].update(space_approved=False))
case('unprovisioned-input',edit=lambda c,s,p:c['mappings'][0].pop('runtime_contract'))
case('config-rebound-device',edit=lambda c,s,p:c['mappings'][0].update(device_id='ALTRA-replacement'))
case('duplicate-physical-mapping',edit=lambda c,s,p:c['mappings'].append(copy.deepcopy(c['mappings'][0])))
case('missing-model',edit=lambda c,s,p:(p/'resources/rendermodels/pico_motion_tracker/pico_motion_tracker.obj').unlink())
case('corrupt-texture',edit=lambda c,s,p:(p/'resources/rendermodels/pico_motion_tracker/pico_motion_tracker.png').write_bytes(b'bad PNG'))
case('corrupt-model-manifest',edit=lambda c,s,p:(p/'resources/rendermodels/pico_motion_tracker/assets-manifest.json').write_text('{}'))
case('wrong-driver-name',edit=lambda c,s,p:(p/'driver.vrdrivermanifest').write_text(json.dumps(dict(name='wrong',resourceOnly=False))))
if a.baseline_dll:case('old-D1-DLL-negative-control',mode='pico',baseline=True)
result=dict(result='PASS',native_cases=len(records),positive_and_safety_cases=len(records)-(1 if a.baseline_dll else 0),old_D1_negative_control=bool(a.baseline_dll),records=records,driver_sha256=hashlib.sha256((a.package/'bin/win64/driver_monaka_bridge.dll').read_bytes()).hexdigest(),
            existing_owner_controlled=False,children_remaining=0,REAL_STEAMVR_RENDER='NOT RUN',PHYSICAL_CUTOVER='PENDING')
(root/'D2_RESULT.json').write_text(json.dumps(result,indent=2)+'\n')
(a.out/'latest-run.json').write_text(json.dumps(dict(run=str(root)),indent=2)+'\n')
print(json.dumps(dict(result='PASS',native_cases=len(records),run=str(root))))
