from pathlib import Path
import copy, importlib.util
spec=importlib.util.spec_from_file_location('preflight',Path(__file__).resolve().parents[1]/'scripts/hil_preflight.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
normal={'USERPROFILE':'C:/Users/operator','LOCALAPPDATA':'C:/Users/operator/AppData/Local','TEMP':'C:/Users/operator/AppData/Local/Temp','HOME':'',
 'vrpaths_sha256':'registry','runtime_path':'C:/Steam/SteamVR','runtime_executable':'C:/Steam/SteamVR/bin/win64/vrserver.exe','runtime_exists':True,
 'helper_path':'C:/reviewed/helper.exe','helper_sha256':'helper','config_path':'C:/reviewed/config.json','config_sha256':'config','sdk_matches':True,
 'helper_runtime_path':'C:/Steam/SteamVR','drivers':[{'path':'C:/driver','exists':True,'manifest_sha256':'driver'}],'mirror_port_in_use':False,
 'bridge_services':[{'path':'C:/reviewed/monaka_bridge_service.exe','sha256':'service'}]}
normal['helper_environment']={k:normal[k] for k in ('USERPROFILE','LOCALAPPDATA','TEMP','HOME')}
assert module.evaluate_environment(normal,normal)==[]
assert module.normalized(module.runtime_path({'runtime':['C:/Steam/SteamVR','C:/Steam/SteamVR/']}))==module.normalized('C:/Steam/SteamVR')
if module.os.name=='nt':assert module.normalized(module.runtime_path({'runtime':['C:/Steam/SteamVR','c:/steam/steamvr']}))==module.normalized('C:/Steam/SteamVR')
for runtimes in ([],['C:/SteamVR','D:/OtherSteamVR'],[None],['']):
    try:module.runtime_path({'runtime':runtimes})
    except ValueError:pass
    else:raise AssertionError('Ambiguous/invalid registry accepted')
cases=[('TEMP','C:/isolated'),('HOME','C:/isolated'),('LOCALAPPDATA','C:/isolated'),('USERPROFILE','C:/isolated'),('vrpaths_sha256','other'),('runtime_exists',False),('sdk_matches',False),('helper_path','C:/copy/helper.exe'),('helper_sha256','modified'),('helper_runtime_path','C:/other'),('mirror_port_in_use',True),('config_sha256','changed'),('bridge_services',[]),('bridge_services',[{'path':'C:/wrong/monaka_bridge_service.exe','sha256':'service'}]),('bridge_services',[{'path':'C:/reviewed/monaka_bridge_service.exe','sha256':'wrong'}]),('drivers',[{'path':'C:/stale','exists':False}])]
for field,value in cases:
    actual=copy.deepcopy(normal);actual[field]=value
    assert module.evaluate_environment(actual,normal),(field,value)
for services in (None,[],[{}],[{'path':'only-path'}]):
    expected=copy.deepcopy(normal);expected['bridge_services']=services
    assert module.evaluate_environment(normal,expected)
expected=copy.deepcopy(normal);expected['services']=[]
assert 'LEGACY_SERVICES_SCHEMA_UNSUPPORTED' in module.evaluate_environment(normal,expected)
print('PASS equivalent Windows runtime registrations, invalid/distinct runtime refusal, normal profile/current expected environment and',len(cases),'read-only environment refusals; synthetic snapshots, no runtime started')
