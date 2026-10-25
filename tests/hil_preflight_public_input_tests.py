"""Synthetic public Health and mocked OS boundary; no hardware or live inventory."""
from pathlib import Path
import copy, hashlib, importlib.util, json, tempfile
from types import SimpleNamespace
from unittest.mock import patch

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('preflight',root/'scripts/hil_preflight.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
now=100000
expected={'input_readiness':dict(source='opaque-source-A',device='opaque-device-42',tracker='logical-1',input_space='space-9',input_revision=112,mapping_revision=54)}
config=dict(namespace='monaka.bridge',version=2,bridge_id='bridge-installation',mapping_revision=54,pose_timeout_ms=500,
            profiles={'profile-1':{'convention':'declared-space-convention'}},
            mappings=[dict(source_id='opaque-source-A',device_id='opaque-device-42',tracker_id='logical-1',input_space='space-9',input_revision=112,profile='profile-1')])
row=dict(publisher_id='bridge-installation',source='opaque-source-A',device='opaque-device-42',input_session=91,input_clock='monotonic',
         space='space-9',convention='declared-space-convention',revision=112,fresh=True,tracker='logical-1',collision=False,
         tracking_state='tracked',position_valid=True,orientation_valid=True,has_pose=True,absent=False,pose_age_ms=10.0,source_receive_age_ms=5.0,
         pose_sequence=7,state_sequence=6,pose_stage='native_observation',is_final_output=False)
health=dict(publisher_id='bridge-installation',publisher_session=34,mapping_revision=54,snapshot_unix_ms=now-20,
            pose_timeout_ms=500,pose_stage='native_observation',is_final_output=False,devices=[row])
count=0
def check(h=health,e=expected,c=config,valid=True):
    global count
    result=m.evaluate_input_readiness(h,e,c,now)
    assert bool(result)!=valid,result
    count+=1
check()
c=copy.deepcopy(config);del c['pose_timeout_ms'];check(c=c)
h=copy.deepcopy(health);h['pose_timeout_ms']=499;check(h,c=c,valid=False)
for invalid in (None,True,500.0,0,10001):
    c=copy.deepcopy(config);c['pose_timeout_ms']=invalid;check(c=c,valid=False)
for timeout in (1,10000):
    c=copy.deepcopy(config);c['pose_timeout_ms']=timeout
    h=copy.deepcopy(health);h.update(snapshot_unix_ms=now,pose_timeout_ms=timeout)
    h['devices'][0].update(pose_age_ms=0,source_receive_age_ms=0);check(h,c=c)
for field in ('fresh','has_pose','position_valid','orientation_valid','collision','absent'):
    for bad in (not row[field],None,1 if row[field] else 0,'true'):
        h=copy.deepcopy(health);h['devices'][0][field]=bad;check(h,valid=False)
for field,value in [('tracking_state','lost'),('tracking_state','stale'),('space','other'),('revision',113),('revision',True),
                    ('tracker','other'),('convention','unknown'),('publisher_id','other'),('pose_stage','final'),('is_final_output',True),
                    ('pose_age_ms',480),('source_receive_age_ms',480),('pose_age_ms',-1),('pose_age_ms',float('nan')),
                    ('source_receive_age_ms',float('inf')),('pose_age_ms',True)]:
    h=copy.deepcopy(health);h['devices'][0][field]=value;check(h,valid=False)
for field,value in [('snapshot_unix_ms',now+1),('snapshot_unix_ms',now-2001),('snapshot_unix_ms',True),('snapshot_unix_ms',None),
                    ('mapping_revision',55),('mapping_revision',True),('publisher_id','other'),('pose_timeout_ms',501),
                    ('pose_stage','final'),('is_final_output',0),('devices',[]),('devices',[row,row]),('devices',{}),('devices',[None])]:
    h=copy.deepcopy(health);h[field]=value;check(h,valid=False)
for field in row:
    if field in ('input_session','input_clock','pose_sequence','state_sequence'):continue
    h=copy.deepcopy(health);del h['devices'][0][field];check(h,valid=False)
for value in (None,[],{},'bad'):check(value,valid=False)
for field,value in [('mapping_revision',55),('bridge_id','other'),('pose_timeout_ms',0),('mappings',[]),('mappings',config['mappings']*2)]:
    c=copy.deepcopy(config);c[field]=value;check(c=c,valid=False)
for field,value in [('input_revision',113),('input_space','other'),('tracker_id','other'),('profile','missing')]:
    c=copy.deepcopy(config);c['mappings'][0][field]=value;check(c=c,valid=False)
for key in expected['input_readiness']:
    e=copy.deepcopy(expected);del e['input_readiness'][key];check(e=e,valid=False)
# Opaque names have no classification: consistently replacing them preserves PASS.
for source,device in [('unrecognized-provider','unit-Z'),('α/opaque','β:opaque')]:
    h=copy.deepcopy(health);e=copy.deepcopy(expected);c=copy.deepcopy(config)
    h['devices'][0].update(source=source,device=device)
    e['input_readiness'].update(source=source,device=device)
    c['mappings'][0].update(source_id=source,device_id=device)
    check(h,e,c)

with tempfile.TemporaryDirectory() as folder:
    base=Path(folder);cfg=base/'config.json';hp=base/'health.json'
    cfg.write_text(json.dumps(config));hp.write_text(json.dumps(health))
    e={**expected,'config_path':str(cfg),'config_sha256':m.digest(cfg),'health_path':str(hp)}
    reasons,diagnostics=m.input_readiness(e,now);assert not reasons
    assert diagnostics['selected'][0]['input_session']==91
    for raw in ('{','{"devices":[],"devices":[]}',json.dumps(None)):
        hp.write_text(raw)
        try:reasons,_=m.input_readiness(e,now)
        except (ValueError,TypeError):pass
        else:assert reasons
    hp.unlink()
    try:m.input_readiness(e,now)
    except OSError:pass
    else:raise AssertionError('Missing Health accepted')
    hp.write_text(json.dumps(health));cfg.write_text(json.dumps({**config,'mapping_revision':55}))
    try:m.input_readiness(e,now)
    except ValueError as error:assert str(error)=='INPUT_CONFIG_HASH_MISMATCH'
    else:raise AssertionError('Changed config accepted')

    # Exercise the production inventory boundary, including the actual query text.
    app=base/'app';(app/'openvr').mkdir(parents=True)
    runtime=base/'runtime';(runtime/'bin/win64').mkdir(parents=True);(runtime/'bin/win64/vrserver.exe').write_bytes(b'runtime')
    (app/'openvr/openvrpaths.vrpath').write_text(json.dumps({'runtime':[str(runtime)],'external_drivers':[]}))
    package=base/'package';(package/'provenance/openvr/bin/win64').mkdir(parents=True)
    (package/'helper.exe').write_bytes(b'helper');(package/'openvr_api.dll').write_bytes(b'sdk')
    (package/'provenance/openvr/bin/win64/openvr_api.dll').write_bytes(b'sdk')
    (package/'provenance/calibration-openvr-sdk.json').write_text(json.dumps({'files':{'bin/win64/openvr_api.dll':m.digest(package/'openvr_api.dll')}}))
    service=package/'monaka_bridge_service.exe';service.write_bytes(b'bridge')
    e.update(helper_relative='helper.exe',mirror_port=12345)
    profile={'USERPROFILE':str(base),'LOCALAPPDATA':str(app),'TEMP':str(base),'HOME':''}
    snapshots=[]
    for private in ({'name':'implementation-a.exe','path':'unreadable/a','sha256':'a','vendor':'A'},
                    {'name':'implementation-b.exe','path':'unreadable/b','sha256':'b','vendor':'B'},None):
        calls=[]
        def run(args,**kwargs):
            calls.append(args)
            if args[0]=='whoami.exe':return SimpleNamespace(stdout='"operator","sid"',returncode=0)
            if args[0]==str(package/'helper.exe'):
                return SimpleNamespace(stdout=json.dumps(dict(available=True,runtime_path=str(runtime),**profile)),returncode=0)
            assert args[0]=='powershell.exe'
            query=args[-1]
            assert 'Get-CimInstance Win32_Process -Filter "Name = \'monaka_bridge_service.exe\'"' in query
            assert 'Where-Object' not in query
            return SimpleNamespace(stdout=json.dumps({'endpoints':[],'bridge_services':[{'ExecutablePath':str(service)}],
                                                       'unavailable_external_inventory':private}),returncode=0)
        with patch.dict(m.os.environ,profile),patch.object(m.subprocess,'run',side_effect=run),patch.object(m.subprocess,'CREATE_NO_WINDOW',0,create=True):
            snapshots.append(m.inventory(e,package))
        assert len(calls)==3
        check()
    assert snapshots[0]==snapshots[1]==snapshots[2]
    assert m.evaluate_environment(snapshots[0],snapshots[0])==[]
    with patch.object(m.subprocess,'run',side_effect=AssertionError('Legacy schema must fail before OS access')):
        try:m.inventory({**e,'services':[]},package)
        except ValueError as error:assert str(error)=='LEGACY_SERVICES_SCHEMA_UNSUPPORTED'
        else:raise AssertionError('Legacy service expectation accepted')

assert not any(token in (root/'scripts/hil_preflight.py').read_text().lower() for token in ('pico_ot_bridge_service','picomotiontrackerbridge'))
print('PASS',count,'public readiness cases; Bridge-only inventory; renamed/replaced/unavailable external implementation equivalence; missing/malformed Health; config SHA refusal')
