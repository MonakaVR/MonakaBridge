"""Compile production WPF sources and run isolated first-source native-CAS acceptance."""
from pathlib import Path
import argparse, subprocess, json, hashlib
p=argparse.ArgumentParser()
for name in ['root','out','helper']:p.add_argument('--'+name,required=True)
p.add_argument('--config');p.add_argument('--health');p.add_argument('--tracker',default='logical')
p.add_argument('--world-x',type=float,default=12);p.add_argument('--mount-x',type=float,default=3)
p.add_argument('--source',default='');p.add_argument('--device',default='');p.add_argument('--profile',default='')
a=p.parse_args();root=Path(a.root).resolve();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
compiler=Path('C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe')
references=Path('C:/Program Files (x86)/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8')
if not (references/'PresentationFramework.dll').exists():references=Path('C:/Windows/Microsoft.NET/Framework64/v4.0.30319/WPF')
files=sorted((root/'ui').glob('*.cs'))+[root/'tests/p0_gui_tests.cs']
refs=['System.dll','System.Core.dll','System.Web.Extensions.dll','System.Drawing.dll','System.Windows.Forms.dll']+[str(references/(n+'.dll')) for n in ['WindowsBase','PresentationCore','PresentationFramework','System.Xaml']]
exe=out/'p0-gui.exe'
commands=[[str(compiler),'/nologo','/utf8output','/target:exe','/optimize+','/main:P0GuiTests','/out:'+str(exe)]+['/reference:'+n for n in refs]+['/resource:'+str(root/('ui/assets/monaka-bridge-tray-'+c+'.ico'))+',MonakaBridge.Icons.'+c+'.ico' for c in ['black','white']]+list(map(str,files)),[str(exe),a.helper,str(out/'runs')]]
records=[]
if a.config or a.health:
 if not a.config or not a.health:raise ValueError('both --config and --health are required for integration')
 commands[1].extend([str(Path(a.config).resolve()),str(Path(a.health).resolve()),a.tracker,str(a.world_x),str(a.mount_x),a.source,a.device,a.profile])
for n,command in enumerate(commands):
 r=subprocess.run(command,capture_output=True,timeout=120);(out/(str(n)+'.stdout.txt')).write_bytes(r.stdout);(out/(str(n)+'.stderr.txt')).write_bytes(r.stderr)
 records.append(dict(command=command,exit_code=r.returncode));(out/'receipt.json').write_text(json.dumps(dict(records=records,source_sha256={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},physical_hil='NOT_RUN'),indent=2)+'\n')
 print(r.stdout.decode('utf-8',errors='replace'));print(r.stderr.decode('utf-8',errors='replace'))
 if r.returncode:raise SystemExit(r.returncode)
latest=out/'runs/latest-run.json'
if latest.exists():(out/'latest-run.json').write_bytes(latest.read_bytes())
