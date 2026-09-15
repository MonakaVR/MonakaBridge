"""Record actual exit codes and artifacts; never infer PASS from progress messages."""
import argparse,datetime,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cmake-bin',type=Path,required=True);p.add_argument('--java-home',type=Path,required=True);p.add_argument('--dumpbin',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];os.chdir(root);evidence=root/'build/validation';evidence.mkdir(parents=True,exist_ok=True)
results={};bins=root/'build/Release'
def checkpoint(status):
 (root/'build/validation-results.json').write_text(json.dumps({'status':status,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'checks':results,'hardware':'NOT RUN'},indent=2))
checkpoint('RUNNING')
def run(name,cmd,timeout=90):
 try:r=subprocess.run([str(x) for x in cmd],capture_output=True,timeout=timeout)
 except Exception:
  results[name]={'result':'FAIL'};checkpoint('FAIL');raise
 text=r.stdout.decode('utf-8',errors='replace')+r.stderr.decode('utf-8',errors='replace');(evidence/(name+'.log')).write_text(text,encoding='utf-8');results[name]={'exit_code':r.returncode,'result':'PASS' if r.returncode==0 else 'FAIL','command':[str(x) for x in cmd]};checkpoint('RUNNING' if r.returncode==0 else 'FAIL');print(name,results[name]['result'],flush=True);assert r.returncode==0,text;return text
repro=run('null-read-reproduction',[bins/'old_context_repro.exe']);assert 'expected_null_read=true' in repro and 'TrackerDevice::RequestOrientationZero' in repro and 'vr::VRDriverLog' in repro
run('direct-standalone',[bins/'direct_regression.exe'])
imports=run('direct-imports',[a.dumpbin,'/dependents',bins/'direct_regression.exe']);assert 'openvr_api.dll' not in imports.lower() and 'vrclient' not in imports.lower()
run('ctest',[a.cmake_bin/'ctest.exe','--test-dir','build','-C','Release','--output-on-failure','--timeout','40'])
run('gui-config-smoke',[Path(os.environ['WINDIR'])/'System32/WindowsPowerShell/v1.0/powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File','tests/ui_config_smoke.ps1','-RepoRoot',root])
run('separate-process-udp',[os.sys.executable,'tests/udp_regression.py','--bin',bins])
run('fixed-codec-interop',[os.sys.executable,'tests/protocol_interop.py','--cpp',root/'build/third_party/monaka-protocol/cpp/Release/monaka_codec_runner.exe','--java-home',a.java_home])
run('derived-provenance',[os.sys.executable,'scripts/record_provenance.py','--check'])
perf=json.loads(run('performance',[bins/'bridge_performance.exe']));(root/'build/performance-results.json').write_text(json.dumps(perf,indent=2))
files=['direct_regression.exe','old_context_repro.exe','monaka_bridge_service.exe','direct_udp_consumer.exe']
outputs=['build/Release/'+f for f in files+['monaka_bridge_config.exe','monaka_bridge_calibrator.exe','openvr_api.dll']]+['build-gui/monaka_bridge_control.exe','build/steamvr/monaka_bridge/bin/win64/driver_monaka_bridge.dll']
report={'status':'PASS','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'checks':results,'performance':perf,'binary_sha256':{f:hashlib.sha256((bins/f).read_bytes()).hexdigest() for f in files},'hardware':'NOT RUN','initial_crash':'confirmed null OpenVR driver context in standalone harness; retained reproduction stack; isolated mock logger fixed harness'}
report['output_sha256']={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in outputs}
report['verification_command']=[os.sys.executable,*os.sys.argv]
(root/'build/validation-results.json').write_text(json.dumps(report,indent=2));print('All recorded software checks PASS; hardware NOT RUN',flush=True)
