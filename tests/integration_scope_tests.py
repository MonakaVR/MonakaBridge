from pathlib import Path
import json,shutil,tempfile
from integration_scope import approved,semantic,PIN,CLAUSES,digest
root=Path(__file__).resolve().parents[1];names=approved(root);assert names
with tempfile.TemporaryDirectory(dir=root/'build') as folder:
 scratch=Path(folder)
 for name in names:
  path=scratch/name;path.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,path)
 approved(scratch,root)
 def reject(fn):
  try:fn()
  except RuntimeError:return
  raise AssertionError('Mutant accepted')
 for name in ('AGENTS.md','outputs/steamvr/driver/presentation.cpp','outputs/steamvr/package/resources/render-catalog.json'):
  path=scratch/name;original=path.read_bytes();path.write_bytes(original+b'\nUNREVIEWED\n')
  reject(lambda:approved(scratch,root))
  pin=scratch/PIN;receipt=pin.read_bytes();p=json.loads(receipt);p['files'][name]['sha256']=digest(path.read_bytes());pin.write_text(json.dumps(p))
  reject(lambda:approved(scratch,root));pin.write_bytes(receipt);path.write_bytes(original)
 path=scratch/'AGENTS.md';original=path.read_text()
 for clause in CLAUSES:
  path.write_text(original.replace(clause,''));reject(lambda:semantic(scratch))
 path.write_text(original)
 path=scratch/'outputs/steamvr/driver/presentation.cpp';original=path.read_text();path.write_text(original+'\nif (kind == "pico") selectModel();\n');reject(lambda:semantic(scratch));path.write_text(original)
 path=scratch/'outputs/AGENTS.md';path.write_text('Unreviewed nested rules');reject(lambda:approved(scratch,root));path.unlink()
print('PASS current hierarchy, semantic clause deletion, product branch, source mutation, receipt rehash and nested-rule refusal')
