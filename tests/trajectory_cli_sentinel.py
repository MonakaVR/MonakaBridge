"""Real CLI test bytes; only an isolated test DLL may satisfy SDK imports."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile
exe,sentinel=map(lambda p:Path(p).resolve(),sys.argv[1:3])
root=Path(tempfile.mkdtemp(prefix='mb-n1-cli-'));target=root/exe.name
shutil.copyfile(exe,target);shutil.copyfile(sentinel,root/'openvr_api.dll')
assert target.read_bytes()==exe.read_bytes()
env=os.environ.copy();marker=root/'vr-called.txt';env['MONAKA_B2_VR_SENTINEL']=str(marker)
p=subprocess.run([str(target)],capture_output=True,env=env,timeout=30)
sys.stdout.buffer.write(p.stdout);sys.stderr.buffer.write(p.stderr)
if p.returncode:raise SystemExit(p.returncode)
assert not marker.exists(),'CLI unit test unexpectedly invoked SDK API'
print('PASS isolated sentinel CLI; official SDK DLL/runtime not executed; evidence',root)
