"""Build real WPF controls and invoke their normal align action without Show."""
from pathlib import Path
import argparse,subprocess,json,hashlib
p=argparse.ArgumentParser()
for name in ['root','out','compiler','references','config']:p.add_argument('--'+name,required=True)
a=p.parse_args();root=Path(a.root);out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
files=sorted((root/'ui').glob('*.cs'))+[root/'tests/world_approval_gui_tests.cs']
refs=['System.dll','System.Core.dll','System.Web.Extensions.dll','System.Drawing.dll','System.Windows.Forms.dll']+[str(Path(a.references)/(n+'.dll')) for n in ['WindowsBase','PresentationCore','PresentationFramework','System.Xaml']]
exe=out/'world-approval-gui.exe';commands=[[a.compiler,'/nologo','/utf8output','/target:exe','/optimize+','/main:WorldApprovalGuiTests','/out:'+str(exe)]+['/reference:'+n for n in refs]+list(map(str,files)),[str(exe),str(root/'tests/fixtures/world-approval/approved-groups.json'),a.config,str(out/'runs')]]
records=[]
for index,command in enumerate(commands):
 process=subprocess.run(command,capture_output=True,timeout=90);(out/(str(index)+'.stdout.txt')).write_bytes(process.stdout);(out/(str(index)+'.stderr.txt')).write_bytes(process.stderr)
 records.append(dict(command=command,exit_code=process.returncode));(out/'receipt.json').write_text(json.dumps(dict(records=records,source_sha256={f.relative_to(root).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in files},actual_sdk='NOT_RUN',physical_hil='NOT_RUN'),indent=2)+'\n')
 print(process.stdout.decode('utf-8',errors='replace'));print(process.stderr.decode('utf-8',errors='replace'))
 if process.returncode:raise SystemExit(process.returncode)
