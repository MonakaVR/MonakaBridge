"""Read-only HIL readiness check. Never starts SteamVR, binds a port or writes config."""
from pathlib import Path
import argparse, csv, datetime, hashlib, json, math, os, subprocess, sys

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def load(path):
    def unique(pairs):
        out={}
        for k,v in pairs:
            if k in out: raise ValueError('duplicate JSON key: '+k)
            out[k]=v
        return out
    return json.loads(Path(path).read_text(encoding='utf-8-sig'),object_pairs_hook=unique)

def inside(root,relative):
    path=(root/relative).resolve()
    if not path.is_relative_to(root.resolve()):raise ValueError('path escapes package: '+relative)
    return path

def normalized(value):return os.path.normcase(os.path.abspath(value)) if value else ''

def runtime_path(registry):
    runtimes=registry.get('runtime',[])
    if not isinstance(runtimes,list) or not runtimes or any(not isinstance(p,str) or not p for p in runtimes):raise ValueError('OPENVR_RUNTIME_PATH_INVALID')
    # OpenVR can retain spelling/case variants of the same Windows path.
    # Distinct locations still require operator review; never choose one silently.
    if len({normalized(p) for p in runtimes})!=1:raise ValueError('OPENVR_RUNTIME_PATH_AMBIGUOUS')
    return str(Path(runtimes[0]).resolve())

def evaluate_environment(actual,expected):
    reasons=[]
    if 'services' in expected:reasons.append('LEGACY_SERVICES_SCHEMA_UNSUPPORTED')
    for key in ('USERPROFILE','LOCALAPPDATA','TEMP','HOME'):
        if normalized(actual.get(key,''))!=normalized(expected.get(key,'')):reasons.append('ENVIRONMENT_'+key+'_MISMATCH')
    for key in ('vrpaths_sha256','runtime_path','runtime_executable','helper_path','helper_sha256','config_path','config_sha256'):
        if actual.get(key)!=expected.get(key):reasons.append(key.upper()+'_MISMATCH')
    if not actual.get('runtime_exists'):reasons.append('OPENVR_RUNTIME_MISSING')
    if not actual.get('sdk_matches'):reasons.append('OPENVR_SDK_IDENTITY_MISMATCH')
    if actual.get('helper_runtime_path')!=actual.get('runtime_path'):reasons.append('HELPER_RUNTIME_PATH_MISMATCH')
    if actual.get('helper_environment')!={k:actual.get(k,'') for k in ('USERPROFILE','LOCALAPPDATA','TEMP','HOME')}:reasons.append('HELPER_PROFILE_MISMATCH')
    if actual.get('drivers')!=expected.get('drivers'):reasons.append('DRIVER_REGISTRATION_MISMATCH')
    if any(not d.get('exists',False) for d in actual.get('drivers',[])):reasons.append('STALE_DRIVER_REGISTRATION')
    if actual.get('mirror_port_in_use'):reasons.append('MIRROR_PORT_IN_USE')
    services=expected.get('bridge_services')
    if not isinstance(services,list) or len(services)!=1 or not services[0].get('path') or not services[0].get('sha256'):
        reasons.append('BRIDGE_SERVICE_EXPECTATION_REQUIRED')
    elif actual.get('bridge_services')!=services:reasons.append('CURRENT_BRIDGE_SERVICE_BUILD_MISMATCH')
    return reasons


def evaluate_input_readiness(health,expected,config,now_ms):
    """Observation-derived diagnostics only; no input implementation inspection."""
    def require(ok,reason):
        if not ok:raise ValueError(reason)
    def integer(value):return type(value) is int and 0<=value<=9223372036854775807
    def number(value):return type(value) in (int,float) and math.isfinite(value) and value>=0
    def identity(value):return isinstance(value,str) and bool(value) and len(value)<=4096
    try:
        target=expected['input_readiness']
        require(isinstance(target,dict),'INPUT_EXPECTATION_INVALID')
        for key in ('source','device','tracker','input_space'):
            require(identity(target[key]),'INPUT_EXPECTATION_INVALID:'+key)
        for key in ('input_revision','mapping_revision'):
            require(integer(target[key]),'INPUT_EXPECTATION_INVALID:'+key)
        require(isinstance(config,dict) and config['namespace']=='monaka.bridge' and config['version']==2,'INPUT_CONFIG_INVALID')
        require(identity(config['bridge_id']) and integer(config['mapping_revision']),'INPUT_CONFIG_INVALID')
        timeout=config['pose_timeout_ms']
        require(number(timeout) and timeout>0,'INPUT_CONFIG_TIMEOUT_INVALID')
        require(type(config['mappings']) is list,'INPUT_CONFIG_INVALID')
        bindings=[b for b in config['mappings'] if b['source_id']==target['source'] and b['device_id']==target['device']]
        require(len(bindings)==1,'INPUT_CONFIG_MAPPING_NOT_UNIQUE')
        binding=bindings[0]
        require(integer(binding['input_revision']) and binding['input_revision']==target['input_revision'] and
                binding['input_space']==target['input_space'] and binding['tracker_id']==target['tracker'] and
                config['mapping_revision']==target['mapping_revision'],'INPUT_EXPECTATION_CONFIG_MISMATCH')
        convention=config['profiles'][binding['profile']]['convention']
        require(identity(convention),'INPUT_CONFIG_CONVENTION_INVALID')
        require(isinstance(health,dict),'HEALTH_MISSING_OR_MALFORMED')
        require(integer(now_ms) and integer(health['snapshot_unix_ms']),'HEALTH_TIME_INVALID')
        age=now_ms-health['snapshot_unix_ms']
        require(0<=age<=2000,'HEALTH_STALE_OR_FUTURE')
        require(integer(health['mapping_revision']) and health['publisher_id']==config['bridge_id'] and
                health['mapping_revision']==target['mapping_revision'] and
                number(health['pose_timeout_ms']) and health['pose_timeout_ms']==timeout,'HEALTH_CONFIG_MISMATCH')
        require(health['pose_stage']=='native_observation' and health['is_final_output'] is False,'HEALTH_STAGE_INVALID')
        require(type(health['devices']) is list and all(isinstance(row,dict) for row in health['devices']),'HEALTH_DEVICES_INVALID')
        rows=[row for row in health['devices'] if row['source']==target['source'] and row['device']==target['device']]
        require(len(rows)==1,'INPUT_NOT_UNIQUE')
        row=rows[0]
        require(row['publisher_id']==config['bridge_id'] and row['pose_stage']=='native_observation' and row['is_final_output'] is False,'INPUT_HEALTH_IDENTITY_MISMATCH')
        for key in ('fresh','has_pose','position_valid','orientation_valid'):
            require(row[key] is True,'INPUT_INVALID:'+key)
        for key in ('collision','absent'):
            require(row[key] is False,'INPUT_INVALID:'+key)
        require(row['tracking_state']=='tracked','INPUT_NOT_TRACKED')
        require(integer(row['revision']) and row['space']==target['input_space'] and row['revision']==target['input_revision'] and
                row['tracker']==target['tracker'] and row['convention']==convention,'INPUT_SPACE_OR_MAPPING_MISMATCH')
        for key in ('pose_age_ms','source_receive_age_ms'):
            require(number(row[key]) and row[key]+age<timeout,'INPUT_EXPIRED:'+key)
    except (KeyError,TypeError,ValueError,OverflowError) as exc:
        return [str(exc)]
    return []


def input_readiness(expected,now_ms):
    """Read explicitly selected Bridge-owned files, without probing input software."""
    def bounded(path):
        path=Path(path)
        if not path.is_absolute():raise ValueError('BRIDGE_DIAGNOSTIC_PATH_MUST_BE_ABSOLUTE')
        if path.stat().st_size>2*1024*1024:raise ValueError('BRIDGE_DIAGNOSTIC_TOO_LARGE')
        return load(path)
    config_path=Path(expected['config_path'])
    before=digest(config_path)
    if before!=expected['config_sha256']:raise ValueError('INPUT_CONFIG_HASH_MISMATCH')
    config=bounded(config_path);health=bounded(expected['health_path'])
    if digest(config_path)!=before:raise ValueError('INPUT_CONFIG_CHANGED_DURING_READ')
    reasons=evaluate_input_readiness(health,expected,config,now_ms)
    keys=('publisher_id','publisher_session','mapping_revision','snapshot_unix_ms','pose_timeout_ms','pose_stage','is_final_output')
    diagnostics={k:health.get(k) for k in keys} if isinstance(health,dict) else {}
    row_keys=('source','device','input_session','input_clock','space','convention','revision','fresh','tracker','collision',
              'tracking_state','position_valid','orientation_valid','has_pose','absent','pose_age_ms','source_receive_age_ms','pose_sequence','state_sequence')
    if isinstance(health,dict) and isinstance(health.get('devices'),list):
        target=expected.get('input_readiness',{})
        diagnostics['selected']=[{k:row.get(k) for k in row_keys} for row in health['devices'] if isinstance(row,dict) and
                                 row.get('source')==target.get('source') and row.get('device')==target.get('device')]
    return reasons,diagnostics

def inventory(expected,package):
    if 'services' in expected:raise ValueError('LEGACY_SERVICES_SCHEMA_UNSUPPORTED')
    actual={k:os.environ.get(k,'') for k in ('USERPROFILE','LOCALAPPDATA','TEMP','HOME')}
    identity=subprocess.run(['whoami.exe','/user','/fo','csv','/nh'],capture_output=True,text=True,timeout=10,creationflags=subprocess.CREATE_NO_WINDOW,check=True)
    actual['windows_identity']=next(csv.reader(identity.stdout.strip().splitlines()))
    paths=Path(actual['LOCALAPPDATA'])/'openvr/openvrpaths.vrpath'
    # OpenVR's registry filename is openvrpaths.vrpath; no registry setter/API is used.
    registry=load(paths);actual['vrpaths_sha256']=digest(paths)
    actual['runtime_path']=runtime_path(registry)
    executable=Path(actual['runtime_path'])/'bin/win64/vrserver.exe'
    actual['runtime_executable']=str(executable);actual['runtime_exists']=executable.is_file()
    actual['drivers']=[{'path':str(Path(p).resolve()),'exists':(Path(p)/'driver.vrdrivermanifest').is_file(),
        'manifest_sha256':digest(Path(p)/'driver.vrdrivermanifest') if (Path(p)/'driver.vrdrivermanifest').is_file() else None,
        'dlls_sha256':{x.name:digest(x) for x in sorted((Path(p)/'bin/win64').glob('*.dll'))},
        'render_manifests_sha256':{str(x.relative_to(p)):digest(x) for x in sorted((Path(p)/'resources').rglob('assets-manifest.json'))}} for p in registry.get('external_drivers',[])]
    helper=inside(package,expected['helper_relative']);config=Path(expected['config_path'])
    actual.update(helper_path=str(helper),helper_sha256=digest(helper),config_path=str(config.resolve()),config_sha256=digest(config))
    query=subprocess.run([str(helper),'--hil-runtime-path'],capture_output=True,text=True,timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0),check=True)
    visible=json.loads(query.stdout);actual['helper_runtime_path']=str(Path(visible['runtime_path']).resolve()) if visible.get('available') else None
    actual['helper_environment']={k:visible.get(k,'') for k in ('USERPROFILE','LOCALAPPDATA','TEMP','HOME')}
    sdk=load(inside(package,'provenance/calibration-openvr-sdk.json'))
    actual['sdk_matches']=all(digest(inside(package,'provenance/openvr/'+p))==h for p,h in sdk['files'].items()) and digest(helper.with_name('openvr_api.dll'))==sdk['files']['bin/win64/openvr_api.dll']
    # Only query endpoint/process identity. No socket construction, Start-Service or stop/kill.
    script="""[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$ErrorActionPreference='Stop'
$endpoints=@(Get-NetUDPEndpoint | Select-Object LocalAddress,LocalPort,OwningProcess)
$bridge_services=@(Get-CimInstance Win32_Process -Filter "Name = 'monaka_bridge_service.exe'" | Select-Object ExecutablePath)
@{endpoints=$endpoints;bridge_services=$bridge_services} | ConvertTo-Json -Depth 5 -Compress
"""
    query=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',script],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    if query.returncode:raise ValueError('WINDOWS_INVENTORY_QUERY_FAILED:'+query.stderr.strip()[:1000])
    state=json.loads(query.stdout);actual['mirror_port_in_use']=any(x['LocalPort']==expected['mirror_port'] for x in state['endpoints'])
    actual['bridge_services']=sorted([{'path':x.get('ExecutablePath'),'sha256':digest(x['ExecutablePath']) if x.get('ExecutablePath') else None} for x in state['bridge_services']],key=lambda x:x.get('path') or '')
    return actual

def time_contract(document,now_ms,semantic,evidence=False):
    """Validate declared time constraints only; never choose a duration."""
    def integer(v):
        if type(v) is not int or not 0<=v<=9223372036854775807:raise ValueError('APPROVAL_INTEGER_INVALID')
        return v
    if semantic:
        v=document.get('lifetime')
        if not isinstance(v,dict):raise ValueError('APPROVAL_LIFETIME_REQUIRED')
        if v.get('mode')!='semantic':raise ValueError('APPROVAL_LIFETIME_MODE_INVALID')
        if set(v)-{'mode','not_before_utc_ms','not_after_utc_ms'}:raise ValueError('APPROVAL_LIFETIME_FIELD_INVALID')
        if set(document)&{'expires_at_utc_ms','valid_from_utc_ms','valid_until_utc_ms'}:raise ValueError('APPROVAL_MIXED_LIFETIME_SCHEMA')
        start=v.get('not_before_utc_ms');end=v.get('not_after_utc_ms')
        if start is not None:start=integer(start)
        if end is not None:end=integer(end)
        if start is not None and end is not None and start>=end:raise ValueError('APPROVAL_TIME_POLICY_RANGE')
        if start is not None and now_ms<start:raise ValueError('APPROVAL_TIME_POLICY_NOT_YET_VALID')
        if end is not None and now_ms>=end:raise ValueError('APPROVAL_TIME_POLICY_EXPIRED')
    elif evidence:end=integer(document['expires_at_utc_ms'])
    else:
        start=integer(document['valid_from_utc_ms']);end=integer(document['valid_until_utc_ms'])
        if not start<end or not start<=now_ms<end:raise ValueError('APPROVAL_POLICY_EXPIRED_OR_NOT_YET_VALID')
    if evidence:
        measured=integer(document['measured_at_utc_ms'])
        if measured>now_ms or (end is not None and (measured>=end or now_ms>=end)):raise ValueError('APPROVAL_EVIDENCE_EXPIRED_OR_FUTURE')
    return end

def authority_package_checks(root,now_ms):
    """Static artifact readiness. Live continuity/freshness is Native-only at approval."""
    failures=[];adoption=load(root/'adoption.json')
    if adoption.get('schema')!='monaka.output-adoption.v1':return ['ADOPTION_SCHEMA_UNSUPPORTED']
    if adoption.get('enabled') is not True:return ['PHYSICAL_POLICY_NOT_ADOPTED']
    def pinned(pin):
        path=inside(root,pin['file'])
        if digest(path)!=pin['sha256']:raise ValueError('APPROVAL_HASH_MISMATCH')
        return load(path)
    def number(v):
        if type(v) not in (int,float) or not math.isfinite(v) or v<0:raise ValueError('APPROVAL_NUMBER_INVALID')
        return v
    try:
        policy=pinned(adoption['policy']);semantic=policy.get('schema')=='monaka.physical-criteria.v2'
        if not semantic and policy.get('schema')!='monaka.physical-criteria.v1':raise ValueError('APPROVAL_POLICY_INVALID_OR_DISABLED')
        if policy.get('enabled') is not True:raise ValueError('APPROVAL_POLICY_INVALID_OR_DISABLED')
        time_contract(policy,now_ms,semantic);limits=policy['limits']
        for k in ('min_pairs','min_inliers','min_inlier_ratio','rms_position','rms_orientation','max_position','max_orientation'):number(limits[k])
        if type(limits['min_pairs']) is not int or type(limits['min_inliers']) is not int or limits['min_pairs']<1 or limits['min_inliers']<1 or limits['min_inlier_ratio']>1:raise ValueError('APPROVAL_LIMITS_INVALID')
        if semantic:
            if policy.get('all_pair_max_mode')!='diagnostic':raise ValueError('APPROVAL_ALL_PAIR_MODE_INVALID')
            if set(limits)&{'all_max_position','all_max_orientation'}:raise ValueError('APPROVAL_DIAGNOSTIC_LIMIT_CONFLICT')
        else:
            for k in ('all_max_position','all_max_orientation'):number(limits[k])
        if policy['measurement_mode']!='trajectory' or any(not isinstance(policy['scope'][k],str) or not policy['scope'][k] for k in ('source','input_space','profile','world_space')):raise ValueError('POLICY_SCOPE_INVALID')
        if not adoption.get('evidence'):raise ValueError('PHYSICAL_EVIDENCE_NOT_ADOPTED')
        if 'current_declaration' in adoption:pinned(adoption['current_declaration'])
        for pin in adoption['evidence']:
            e=pinned(pin)
            if e['schema']!=('monaka.physical-evidence.v2' if semantic else 'monaka.physical-evidence.v1') or e['evidence_kind']!='physical_capture':raise ValueError('PHYSICAL_EVIDENCE_REQUIRED')
            time_contract(e,now_ms,semantic,True)
            if (e['criteria_id'],e['criteria_version'],e['criteria_sha256'])!=(policy['id'],policy['version'],adoption['policy']['sha256']):raise ValueError('EVIDENCE_POLICY_MISMATCH')
            result=pinned(e['native_result']);pinned(e['raw']);pinned(e['config_preimage'])
            if result['result_id']!=e['measurement_result_id'] or result['world']!=e['world'] or result['config_sha256']!=e['config_preimage_sha256']:raise ValueError('EVIDENCE_NATIVE_RESULT_MISMATCH')
            for k in ('rms_position','rms_orientation','max_position','max_orientation'):
                if number(result[k])>limits[k]:raise ValueError('EVIDENCE_RESIDUAL_LIMIT:'+k)
            for k in ('max_position','max_orientation'):
                v=number(result['all_residuals'][k])
                if not semantic and v>limits['all_'+k]:raise ValueError('EVIDENCE_RESIDUAL_LIMIT:all_'+k)
            if not 0<result['inliers']<=result['pairs'] or result['pairs']<limits['min_pairs'] or result['inliers']<limits['min_inliers'] or result['inliers']/result['pairs']<limits['min_inlier_ratio']:raise ValueError('EVIDENCE_PAIR_LIMIT')
            if semantic:
                if 'quality' not in result or 'measurement_options' not in result:raise ValueError('APPROVAL_NATIVE_QUALITY_MISSING')
                q=result['quality'];minimum=policy['motion_quality']
                if q['sufficient'] is not True or q['samples']!=result['inliers'] or number(q['valid_ratio'])>1:raise ValueError('APPROVAL_NATIVE_QUALITY_INVALID')
                for k in ('valid_ratio','span_seconds','accumulated_rotation_rad','axis_diversity'):
                    if number(q[k])<number(minimum[k]):raise ValueError('APPROVAL_MOTION_QUALITY_FAILED')
                if len(q['extent_m'])!=3 or len(minimum['extent_m'])!=3:raise ValueError('APPROVAL_NATIVE_QUALITY_INVALID')
                if any(number(v)<number(b) for v,b in zip(q['extent_m'],minimum['extent_m'])):raise ValueError('APPROVAL_MOTION_QUALITY_FAILED')
                for k in ('recording_seconds','relative_motion_min_rad','relative_motion_max_rad','inlier_position_m','inlier_orientation_rad'):
                    if number(result['measurement_options'][k])!=number(policy['measurement_options'][k]):raise ValueError('APPROVAL_MEASUREMENT_OPTIONS_MISMATCH')
    except (ValueError,KeyError,TypeError,OverflowError) as error:failures.append(str(error))
    return failures

def package_checks(package,now_ms):
    failures=[];manifest=load(package/'artifact-manifest.json')
    if manifest.get('schema')!='monaka.hil-artifact-set.v1':raise ValueError('ARTIFACT_SCHEMA_UNSUPPORTED')
    for relative,sha in manifest['files_sha256'].items():
        if digest(inside(package,relative))!=sha:failures.append('ARTIFACT_HASH_MISMATCH:'+relative)
    for name in ('build','full_regression','revision','offline_rehearsal','lifecycle'):
        receipt=load(inside(package,manifest['receipts'][name]))
        if receipt.get('status')!='PASS':failures.append('RECEIPT_NOT_PASS:'+name)
        if receipt.get('compatibility_id')!=manifest['compatibility_id']:failures.append('RECEIPT_BUILD_MISMATCH:'+name)
    if manifest.get('known_p0_blockers'):failures.append('KNOWN_P0_BLOCKERS')
    if not manifest.get('rollback_files') or any(p not in manifest['files_sha256'] for p in manifest['rollback_files']):failures.append('ROLLBACK_PACKAGE_MISSING')
    features=set(manifest.get('native_features',[]))
    for feature in ('file_output_authority_v1','max_residuals_v1','first_failure_v1'):
        if feature not in features:failures.append('NATIVE_FEATURE_MISSING:'+feature)
    if manifest.get('config_transaction')!='monaka-config-transaction-v1':failures.append('CONFIG_TRANSACTION_UNSUPPORTED')
    failures.extend(authority_package_checks(package/'monaka/OutputApproval',now_ms))
    return failures

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--package',type=Path,default=Path(__file__).resolve().parents[1]);parser.add_argument('--expectation',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    report={'schema':'monaka.hil-preflight.v1','hardware':'NOT_RUN','read_only':True,'status':'FAIL','reasons':[]}
    try:report['reasons']+=package_checks(args.package,int(datetime.datetime.now(datetime.timezone.utc).timestamp()*1000))
    except Exception as exc:report['reasons'].append('PACKAGE_CHECK_FAILED:'+str(exc))
    try:
        expectation=load(args.expectation or args.package/'hil-environment.json');actual=inventory(expectation,args.package.resolve());report['environment']=actual;report['reasons']+=evaluate_environment(actual,expectation)
        if actual['windows_identity']!=expectation.get('windows_identity'):report['reasons'].append('WINDOWS_USER_IDENTITY_MISMATCH')
    except Exception as exc:report['reasons'].append('ENVIRONMENT_CHECK_FAILED:'+str(exc))
    try:
        expectation=load(args.expectation or args.package/'hil-environment.json')
        reasons,diagnostics=input_readiness(expectation,int(datetime.datetime.now(datetime.timezone.utc).timestamp()*1000))
        report['input_readiness']=diagnostics;report['reasons']+=reasons
    except Exception as exc:report['reasons'].append('INPUT_READINESS_FAILED:'+str(exc))
    report['status']='FAIL' if report['reasons'] else 'PASS';args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False));return 0 if report['status']=='PASS' else 1

if __name__=='__main__':sys.exit(main())
