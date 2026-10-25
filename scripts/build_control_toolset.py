"""Fresh committed-source software build/test receipt; never launches real SDK."""
from pathlib import Path
from datetime import datetime,timezone
import argparse,hashlib,json,subprocess,sys,time,re
p=argparse.ArgumentParser()
for name in ['build','sdk','cmake','ctest']:p.add_argument('--'+name,required=True)
p.add_argument('--parallel',type=int,default=2);a=p.parse_args()
root=Path(__file__).resolve().parents[1];build=Path(a.build).resolve();sdk=Path(a.sdk).resolve()
def git(*args):return subprocess.check_output(['git','--no-optional-locks','-C',str(root),*args]).decode().strip()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def sources():return {n:sha(root/n) for n in git('ls-files').splitlines()}
if git('status','--porcelain'):raise SystemExit('Source must be clean and committed before final build')
if build.exists():raise SystemExit('Fresh build requires a new directory; existing evidence is preserved')
head=git('rev-parse','HEAD');before=sources();build.mkdir(parents=True);records=[]
def run(name,cmd):
 start=time.monotonic();r=subprocess.run([str(x) for x in cmd],cwd=root,capture_output=True)
 (build/(name+'.stdout.txt')).write_bytes(r.stdout);(build/(name+'.stderr.txt')).write_bytes(r.stderr)
 records.append(dict(name=name,command=[str(x) for x in cmd],exit_code=r.returncode,seconds=time.monotonic()-start))
 (build/'build-steps.json').write_text(json.dumps(records,indent=2)+'\n')
 print(json.dumps(records[-1]),flush=True)
 if r.returncode:raise SystemExit('Failed '+name+'; inspect raw build logs')
run('sdk-provenance',[sys.executable,'-B',root/'scripts/verify_calibration_sdk.py',sdk])
run('configure',[a.cmake,'-S',root,'-B',build,'-G','Visual Studio 18 2026','-A','x64','-DOPENVR_SDK='+str(sdk),'-DPython3_EXECUTABLE='+sys.executable,'-DMB_BUILD_CONTROL_GUI=ON'])
run('build',[a.cmake,'--build',build,'--config','Release','--parallel',str(a.parallel)])
# Exact inherited accepted software suite plus all new GUI groups.
SELECTED=['common_world_control_scope', 'common_world_deployment_scope', 'common_world_activation_scope', 'common_world_live_sender_scope', 'world_calibration', 'trajectory_calibration', 'common_world_receiver_scope', 'trusted_live_source_scope', 'p1_runtime_contract', 'static_capability_propagation', 'capability_runtime_scope', 'config_transaction_process', 'config_write_scope_drift', 'measurement_target', 'measurement_target_scope', 'calibration_session', 'calibration_review_source-stale', 'calibration_review_reference-stale', 'calibration_review_ttl-replay', 'calibration_review_draft-publish', 'calibration_review_staging-peak', 'calibration_review_freshness-boundary', 'calibration_review_ttl-saved', 'calibration_session_scope', 'writer_fixture_scope', 'calibration_provider_options', 'provider_options_scope', 'calibration_provider_options_process', 'calibration_session_process', 'calibration_session_process_review', 'continuous_capture_monitor', 'continuous_capture_monitor_process', 'continuous_monitor_scope', 'trajectory_cli', 'calibration_runtime_provider', 'native_capture_scope', 'native_control_integration_scope', 'calibration_gui_json', 'calibration_gui_profile', 'calibration_gui_models', 'calibration_gui_job', 'calibration_gui_launch', 'calibration_gui_client', 'calibration_gui_wpf', 'calibration_gui_scope', 'calibration_b4_native', 'calibration_gui_b4', 'calibration_b4_scope', 'calibration_b4_product_process']
run('ctest',[a.ctest,'--test-dir',build,'-C','Release','--output-on-failure','-R','^('+'|'.join(re.escape(n) for n in SELECTED)+')$','--parallel',str(a.parallel),'--output-junit',str(build/'full-regression.xml')])
after=sources()
if git('rev-parse','HEAD')!=head or git('status','--porcelain') or after!=before:raise SystemExit('Committed source changed during final build/test')
products=['Release/monaka_bridge_calibrator.exe','Release/monaka_bridge_config.exe','Release/monaka_bridge_service.exe','control/Release/monaka_bridge_control.exe']
identity=dict(schema='monaka.bridge.calibration-build.v1',source_commit=head,source_tree=git('rev-parse','HEAD^{tree}'),git_clean=True,selected_software_tests=SELECTED,source_sha256=before,products_sha256={n:sha(build/n) for n in products},records=records,regression_junit_sha256=sha(build/'full-regression.xml'),completed_utc=datetime.now(timezone.utc).isoformat(),actual_sdk_execution='NOT_RUN',os_visual_validation='NOT_RUN',physical_hil='NOT_RUN')
(build/'build-identity.json').write_text(json.dumps(identity,indent=2)+'\n',encoding='utf-8')
print('PASS clean committed-source build/full software regression; genuine SDK compile/link only',flush=True)
