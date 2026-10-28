"""Exact successor admission; historical receipts stay immutable.

Projection is for historical scope checks only. Current behavioral tests compile
and execute the actual tree. Changing either source or receipt fails admission.
"""
from pathlib import Path
import hashlib, json, subprocess
from functools import lru_cache
from installation_owner_scope import approved as owner_approved, project as owner_project, project_text as owner_text

PARENT = '629e2682e113bd81f4854c470982f68cc3b187dd'
PIN = 'dependencies/tray-reopen-baseline.json'
FILES = {'ui/steamvr_control_tray.cs', 'tests/control_foundation_tests.cs',
         'tests/tray_window_tests.cs', 'tests/run_tray_window.py',
         'tests/tray_reopen_scope.py', 'tests/tray_reopen_scope_tests.py',
         'tests/lifecycle_serialization_scope.py',
         'tests/lifecycle_serialization_scope_tests.py', 'CMakeLists.txt'}

def digest(raw): return hashlib.sha256(raw.replace(b'\r\n', b'\n')).hexdigest()
def repository(root):
    root = Path(root).resolve()
    while not (root / '.git').exists():
        if root.parent == root: raise RuntimeError('Tray predecessor history missing')
        root = root.parent
    return str(root)
@lru_cache(maxsize=None)
def git_bytes(repo, ref, name):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', repo, 'show', ref + ':' + name])
def approved(root, git_root=None):
    root = Path(root)
    extra = owner_approved(root, git_root)
    if not (root / PIN).exists(): return set()
    repo = repository(git_root or root); raw = (root / PIN).read_bytes(); pin = json.loads(raw)
    if digest(raw) != digest(git_bytes(repo, 'HEAD', PIN)): raise RuntimeError('Tray receipt not committed')
    if pin['parent'] != PARENT or pin['authority'] != 'MONAKA-ORCH-OFFLINE-20261028-R2 / SA-01' or set(pin['files']) != FILES:
        raise RuntimeError('Tray exact scope changed')
    for name, entry in pin['files'].items():
        if digest(owner_project(root,name,(root / name).read_bytes(),git_root)) != entry['sha256']: raise RuntimeError('Tray source drift: ' + name)
        if entry['parent_sha256'] is not None and digest(git_bytes(repo, PARENT, name)) != entry['parent_sha256']:
            raise RuntimeError('Tray predecessor drift: ' + name)
    return FILES | {PIN} | extra
def project(root, name, raw, git_root=None):
    raw = owner_project(root,name,raw,git_root)
    root = Path(root)
    if name not in FILES or not (root / PIN).exists(): return raw
    entry = json.loads((root / PIN).read_bytes())['files'].get(name)
    if entry and entry['parent_sha256'] is not None and digest(raw) == entry['sha256']:
        return git_bytes(repository(git_root or root), PARENT, name)
    return raw
def project_text(root, text):
    root = Path(root)
    text = owner_text(root,text)
    if not (root / PIN).exists(): return text
    matches = {git_bytes(repository(root), PARENT, name).decode('utf-8-sig')
               for name, entry in json.loads((root / PIN).read_bytes())['files'].items()
               if entry['parent_sha256'] is not None and entry['sha256'] == digest(text.encode())}
    if len(matches) > 1: raise RuntimeError('Ambiguous tray predecessor')
    return matches.pop() if matches else text
