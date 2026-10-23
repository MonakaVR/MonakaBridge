"""Normal WPF types unshown, explicit typed drafts and real native CAS only."""
from pathlib import Path
import argparse,json,subprocess,time,hashlib
from hil_discovery_process import fixture
p=argparse.ArgumentParser()
for n in ['root','out','compiler','references','config']:p.add_argument('--'+n,required=True)
a=p.parse_args();root=Path(a.root).resolve();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
seed=out/'seed.json';seed.write_text(json.dumps(fixture()),encoding='utf-8')
sources=['control_models','installation_control','steamvr_control_gui_main','steamvr_control_wpf','steamvr_control_tray','calibration_json','calibration_profile','calibration_models','calibration_owned_process','calibration_pipe_frames','calibration_process_client','calibration_panel','calibration_profile_dialog','discovery_models','discovery_panel','discovery_window']
files=['ui/'+n+'.cs' for n in sources]+['tests/hil_discovery_gui_tests.cs']
files+=['ui/bridge_icons.cs','ui/service_lifecycle_control.cs']
references=['System.dll','System.Core.dll','System.Web.Extensions.dll','System.Drawing.dll','System.Windows.Forms.dll']+[str(Path(a.references)/(n+'.dll')) for n in ['WindowsBase','PresentationCore','PresentationFramework','System.Xaml']]
exe=out/'hil-discovery-tests.exe';commands=[[a.compiler,'/nologo','/utf8output','/target:exe','/optimize+','/main:HilDiscoveryGuiTests','/out:'+str(exe)]+['/reference:'+n for n in references]+[str(root/n) for n in files],[str(exe),str(seed),a.config,str(out)]]
records=[]
commands[0]+=['/resource:'+str(root/('ui/assets/monaka-bridge-tray-'+color+'.ico'))+',MonakaBridge.Icons.'+color+'.ico' for color in ['black','white']]
for index,cmd in enumerate(commands):
 start=time.monotonic();r=subprocess.run(cmd,capture_output=True,timeout=120);(out/(str(index)+'.stdout.txt')).write_bytes(r.stdout);(out/(str(index)+'.stderr.txt')).write_bytes(r.stderr);records.append(dict(command=cmd,exit_code=r.returncode,seconds=time.monotonic()-start));(out/'receipt.json').write_text(json.dumps(dict(records=records,source_sha256={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in files},actual_sdk='NOT_RUN',physical_hil='NOT_RUN'),indent=2)+'\n');print(r.stdout.decode('utf-8',errors='replace'));print(r.stderr.decode('utf-8',errors='replace'))
 if r.returncode:raise SystemExit(r.returncode)
