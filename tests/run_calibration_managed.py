"""Headless .NET tests. Never launches product GUI, NotifyIcon or genuine SDK."""
from pathlib import Path
import argparse,json,subprocess,tempfile,time,hashlib,sys
sys.stdout.reconfigure(encoding="utf-8",errors="replace");sys.stderr.reconfigure(encoding="utf-8",errors="replace")
p=argparse.ArgumentParser();p.add_argument('--group',required=True,choices=['json','profile','models','job','launch','client','wpf','b4'])
for name in ['root','out','compiler','references','native','config']:p.add_argument('--'+name,required=True)
a=p.parse_args();root=Path(a.root).resolve();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
base=['ui/control_models.cs','ui/installation_control.cs','ui/calibration_json.cs','ui/calibration_profile.cs']
process=base+['ui/calibration_models.cs','ui/calibration_owned_process.cs','ui/calibration_pipe_frames.cs','ui/calibration_process_client.cs']
wpf=process+['ui/steamvr_control_gui_main.cs','ui/steamvr_control_wpf.cs','ui/steamvr_control_tray.cs','ui/calibration_panel.cs','ui/calibration_profile_dialog.cs','ui/discovery_models.cs','ui/discovery_panel.cs','ui/discovery_window.cs']
refs=['System.dll','System.Core.dll','System.Web.Extensions.dll'];records=[]
def build(name,main,sources,gui=False):
 executable=out/(name+'.exe');references=refs+(['System.Drawing.dll','System.Windows.Forms.dll']+[str(Path(a.references)/(n+'.dll')) for n in ['WindowsBase','PresentationCore','PresentationFramework','System.Xaml']] if gui else [])
 cmd=[a.compiler,'/nologo','/utf8output','/target:exe','/optimize+','/main:'+main,'/out:'+str(executable)]+['/reference:'+x for x in references]+[str(root/x) for x in sources]
 run(cmd,name+'-build');return executable
def run(cmd,name):
 start=time.monotonic();r=subprocess.run([str(x) for x in cmd],capture_output=True,timeout=210)
 (out/(name+'.stdout.txt')).write_bytes(r.stdout);(out/(name+'.stderr.txt')).write_bytes(r.stderr)
 records.append(dict(command=[str(x) for x in cmd],exit_code=r.returncode,seconds=time.monotonic()-start))
 (out/'receipt.json').write_text(json.dumps(dict(group=a.group,records=records,source_sha256={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in sorted(set(base+process+wpf))}),indent=2)+'\n')
 print(r.stdout.decode('utf-8',errors='replace'));print(r.stderr.decode('utf-8',errors='replace'))
 if r.returncode:raise SystemExit(r.returncode)
short=Path(tempfile.mkdtemp(prefix='mg-'))
if a.group in ['json','profile','models']:
 names={'json':('CalibrationJsonTests','calibration_json_tests.cs'),'profile':('CalibrationProfileTests','calibration_profile_tests.cs'),'models':('CalibrationModelTests','calibration_model_tests.cs')};main,test=names[a.group]
 exe=build(a.group,main,process+['tests/'+test]);run([exe],a.group)
elif a.group=='job':
 fixture=build('job-fixture','CalibrationJobFixture',process+['tests/calibration_job_fixture.cs']);exe=build('job-tests','CalibrationJobTests',process+['tests/calibration_job_tests.cs']);run([exe,fixture,short],a.group)
elif a.group=='launch':
 fixture=build('launch-fixture','CalibrationLaunchFixture',process+['tests/calibration_launch_tests.cs']);exe=build('launch-tests','CalibrationLaunchTests',process+['tests/calibration_launch_tests.cs']);run([exe,fixture,short],a.group)
elif a.group=='client':
 fault=build('protocol-fault.fixture','CalibrationProtocolFaultFixture',['tests/calibration_protocol_fault_fixture.cs']);exe=build('client-tests','CalibrationClientProcessTests',process+['tests/calibration_client_process_tests.cs']);run([exe,a.native,fault,short],a.group)
elif a.group=='b4':
 exe=build('unshown-b4-tests','CalibrationB4GuiTests',wpf+['tests/calibration_wpf_tests.cs','tests/calibration_b4_gui_tests.cs'],True);run([exe,a.native,a.config,short],a.group)
else:
 exe=build('unshown-wpf-tests','CalibrationWpfTests',wpf+['tests/calibration_wpf_tests.cs'],True);run([exe,a.native,a.config,short],a.group)
