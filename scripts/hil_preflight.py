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
    if actual.get('services')!=expected.get('services'):reasons.append('CURRENT_SERVICE_BUILD_MISMATCH')
    return reasons

def inventory(expected,package):
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
$services=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -in @('monaka_bridge_service.exe','pico_ot_bridge_service.exe')} | Select-Object ExecutablePath)
@{endpoints=$endpoints;services=$services} | ConvertTo-Json -Depth 5 -Compress
"""
    query=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',script],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    if query.returncode:raise ValueError('WINDOWS_INVENTORY_QUERY_FAILED:'+query.stderr.strip()[:1000])
    state=json.loads(query.stdout);actual['mirror_port_in_use']=any(x['LocalPort']==expected['mirror_port'] for x in state['endpoints'])
    actual['services']=sorted([{'path':x.get('ExecutablePath'),'sha256':digest(x['ExecutablePath']) if x.get('ExecutablePath') else None} for x in state['services']],key=lambda x:x.get('path') or '')
    return actual

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
    adoption=load(package/'monaka/OutputApproval/adoption.json')
    if not adoption.get('enabled'):failures.append('PHYSICAL_POLICY_NOT_ADOPTED')
    else:
        root=package/'monaka/OutputApproval';pin=adoption['policy'];policy_path=inside(root,pin['file']);policy=load(policy_path)
        if digest(policy_path)!=pin['sha256']:failures.append('POLICY_HASH_MISMATCH')
        if not policy.get('enabled') or policy.get('schema')!='monaka.physical-criteria.v1' or not policy['valid_from_utc_ms']<=now_ms<policy['valid_until_utc_ms']:failures.append('POLICY_INVALID_OR_EXPIRED')
        limits=policy['limits']
        for k in ('min_pairs','min_inliers','min_inlier_ratio','rms_position','rms_orientation','max_position','max_orientation','all_max_position','all_max_orientation'):
            if type(limits[k]) not in (float,int) or not math.isfinite(limits[k]) or limits[k]<0:failures.append('POLICY_LIMIT_INVALID:'+k)
        if type(limits['min_pairs']) is not int or type(limits['min_inliers']) is not int or limits['min_pairs']<1 or limits['min_inliers']<1 or limits['min_inlier_ratio']>1:failures.append('POLICY_PAIR_LIMIT_INVALID')
        if policy['measurement_mode']!='trajectory' or any(not isinstance(policy['scope'][k],str) or not policy['scope'][k] for k in ('source','input_space','profile','world_space')):failures.append('POLICY_SCOPE_INVALID')
        if not adoption.get('evidence'):failures.append('PHYSICAL_EVIDENCE_NOT_ADOPTED')
        for pin in adoption.get('evidence',[]):
            if digest(inside(root,pin['file']))!=pin['sha256']:failures.append('EVIDENCE_HASH_MISMATCH')
            evidence=load(inside(root,pin['file']))
            if evidence['schema']!='monaka.physical-evidence.v1' or evidence['evidence_kind']!='physical_capture':failures.append('PHYSICAL_EVIDENCE_REQUIRED')
            if not evidence['measured_at_utc_ms']<=now_ms<evidence['expires_at_utc_ms']:failures.append('EVIDENCE_EXPIRED_OR_FUTURE')
            if (evidence['criteria_id'],evidence['criteria_version'],evidence['criteria_sha256'])!=(policy['id'],policy['version'],adoption['policy']['sha256']):failures.append('EVIDENCE_POLICY_MISMATCH')
            for member in ('native_result','raw','config_preimage'):
                target=inside(root,evidence[member]['file'])
                if digest(target)!=evidence[member]['sha256']:failures.append('EVIDENCE_MEMBER_HASH_MISMATCH:'+member)
            result=load(inside(root,evidence['native_result']['file']))
            if result['result_id']!=evidence['measurement_result_id'] or result['world']!=evidence['world'] or result['config_sha256']!=evidence['config_preimage_sha256']:failures.append('EVIDENCE_NATIVE_RESULT_MISMATCH')
            for k in ('rms_position','rms_orientation','max_position','max_orientation','all_max_position','all_max_orientation'):
                v=result['all_residuals'][k[4:]] if k.startswith('all_') else result[k]
                if type(v) not in (int,float) or not math.isfinite(v) or v<0 or v>limits[k]:failures.append('EVIDENCE_RESIDUAL_LIMIT:'+k)
            if not 0<result['inliers']<=result['pairs'] or result['pairs']<limits['min_pairs'] or result['inliers']<limits['min_inliers'] or result['inliers']/result['pairs']<limits['min_inlier_ratio']:failures.append('EVIDENCE_PAIR_LIMIT')
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
    report['status']='FAIL' if report['reasons'] else 'PASS';args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False));return 0 if report['status']=='PASS' else 1

if __name__=='__main__':sys.exit(main())
