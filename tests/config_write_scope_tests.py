from pathlib import Path
import json,shutil,uuid
from config_write_scope import approved_config_write
root=Path(__file__).resolve().parents[1]
approved_config_write(root)
scratch=root/'build/config-scope-drift'/str(uuid.uuid4());scratch.mkdir(parents=True)
pin=json.loads((root/'dependencies/config-write-baseline.json').read_text())
for n in list(pin['files'])+['dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json']:
 p=scratch/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/n,p)
approved_config_write(scratch,root)
count=0
for name in ['CMakeLists.txt','src/mapping/config.cpp','dependencies/p2r-runtime-baseline.json']:
 p=scratch/name;old=p.read_bytes();p.write_bytes(old+b'\n# deliberate drift mutant\n')
 try:approved_config_write(scratch,root)
 except RuntimeError:count+=1
 else:raise RuntimeError('Guard accepted mutant: '+name)
 p.write_bytes(old)
if count!=3:raise RuntimeError('Missing drift negative control')
print('PASS exact config-write scope;3 deliberate source/historical-manifest mutants rejected')
