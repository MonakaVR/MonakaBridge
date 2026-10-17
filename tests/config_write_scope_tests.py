from pathlib import Path
import json,shutil,uuid
from config_write_scope import approved_config_write,historical
root=Path(__file__).resolve().parents[1]
approved_config_write(root)
scratch=root/'build/config-scope-drift'/str(uuid.uuid4());scratch.mkdir(parents=True)
pin=json.loads((root/'dependencies/config-write-baseline.json').read_text())
from capability_runtime_scope import approved_capabilities
from measurement_target_scope import UNCHANGED
for n in set(pin['files'])|approved_config_write(root)|approved_capabilities(root)|UNCHANGED|{'dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json','docs/dev-integration/phase2cp2r-20261015.md'}:
 p=scratch/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/n,p)
approved_config_write(scratch,root)
count=0
for name in ['CMakeLists.txt','src/mapping/config.cpp','dependencies/p2r-runtime-baseline.json','docs/dev-integration/phase2cp2r-20261015.md']:
 p=scratch/name;old=p.read_bytes();p.write_bytes(old+b'\n# deliberate drift mutant\n')
 try:approved_config_write(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('Guard accepted mutant: '+name)
 p.write_bytes(old)
p=scratch/'src/mapping/config.cpp';current=p.read_bytes();p.write_bytes(historical(str(root),'src/mapping/config.cpp'))
try:approved_config_write(scratch,root)
except RuntimeError:count+=1
else:raise RuntimeError('Restoring predecessor bypassed current-source pin')
p.write_bytes(current)
if count!=5:raise RuntimeError('Missing drift negative control')
print('PASS exact config-write scope;5 deliberate source/historical-manifest mutants rejected')
