from pathlib import Path
import json,shutil,tempfile
from native_owner_scope import approved,project,PIN,PARENT,FILES,digest,git_bytes,repository
root=Path(__file__).resolve().parents[1]
names=approved(root)
assert names==FILES|{PIN}
(root/'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=root/'build') as folder:
    scratch=Path(folder)
    for name in names:
        path=scratch/name;path.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,path)
    approved(scratch,root)
    for name in FILES:
        path=scratch/name;original=path.read_bytes();path.write_bytes(original+b'\nUNREVIEWED\n')
        for rehash in (False,True):
            receipt=(scratch/PIN).read_bytes()
            if rehash:
                pin=json.loads(receipt);pin['files'][name]['sha256']=digest(path.read_bytes());(scratch/PIN).write_text(json.dumps(pin))
            try: approved(scratch,root)
            except RuntimeError: pass
            else: raise AssertionError('Mutant accepted: '+name)
            (scratch/PIN).write_bytes(receipt)
        path.write_bytes(original)
        path.unlink()
        try: approved(scratch,root)
        except RuntimeError as error: assert 'Owner current source unavailable: '+name in str(error)
        else: raise AssertionError('Missing source accepted: '+name)
        path.write_bytes(original)
    for name,entry in json.loads((root/PIN).read_bytes())['files'].items():
        if entry['parent_sha256'] is not None: assert project(root,name,(root/name).read_bytes())==git_bytes(repository(root),PARENT,name)
print('PASS exact owner admission, source/missing-input/forged receipt mutants, exact managed predecessor projection')
