from pathlib import Path
import json, shutil, tempfile
from tray_reopen_scope import approved, project, PIN, PARENT, FILES, digest, git_bytes, repository
root = Path(__file__).resolve().parents[1]
names = approved(root)
assert names == FILES | {PIN}
(root / 'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=root / 'build') as folder:
    scratch = Path(folder)
    for name in names:
        path = scratch / name; path.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(root / name, path)
    approved(scratch, root)
    for name in FILES:
        path = scratch / name; original = path.read_bytes(); path.write_bytes(original + b'\nUNREVIEWED\n')
        for rehash in (False, True):
            receipt = (scratch / PIN).read_bytes()
            if rehash:
                pin = json.loads(receipt); pin['files'][name]['sha256'] = digest(path.read_bytes())
                (scratch / PIN).write_text(json.dumps(pin))
            try: approved(scratch, root)
            except RuntimeError: pass
            else: raise AssertionError('Mutant accepted: ' + name)
            (scratch / PIN).write_bytes(receipt)
        path.write_bytes(original)
    for name, entry in json.loads((root / PIN).read_bytes())['files'].items():
        if entry['parent_sha256'] is not None:
            assert project(root, name, (root / name).read_bytes()) == git_bytes(repository(root), PARENT, name)
print('PASS exact current admission; 18 source/receipt mutation rejections; historical projection')
