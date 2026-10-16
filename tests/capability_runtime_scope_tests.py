from pathlib import Path
import shutil,uuid
from capability_runtime_scope import FILES,PIN,approved_capabilities,historical
root=Path(__file__).resolve().parents[1];approved_capabilities(root)
scratch=root/'build/capability-scope-mutants'/str(uuid.uuid4());scratch.mkdir(parents=True)
for name in FILES|{PIN,'dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json'}:
 p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
approved_capabilities(scratch,root);rejected=0
for name in ['CMakeLists.txt','src/observation/registry.cpp','tests/config_write_scope.py','dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json']:
 p=scratch/name;old=p.read_bytes();p.write_bytes(old+b'\n# deliberate drift\n')
 try:approved_capabilities(scratch,root)
 except RuntimeError:rejected+=1
 else:raise RuntimeError('Capability scope accepted drift: '+name)
 p.write_bytes(old)
p=scratch/'src/observation/registry.cpp';old=p.read_bytes();p.write_bytes(historical(str(root),'src/observation/registry.cpp'))
try:approved_capabilities(scratch,root)
except RuntimeError:rejected+=1
else:raise RuntimeError('Capability scope accepted predecessor rollback')
p.write_bytes(old);assert rejected==6
print('PASS capability current-source scope;6 drift/rollback mutants rejected;old config/P2R pins immutable')
