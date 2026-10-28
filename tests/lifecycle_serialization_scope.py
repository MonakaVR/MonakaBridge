"""Current shutdown repair admission before immutable historical source views."""
from pathlib import Path
from functools import lru_cache
import hashlib, json, subprocess
from tray_reopen_scope import approved as reopen_approved, project as reopen_project, project_text as reopen_text
PARENT = '0c874d34d2f68b92c167a7b2e5025fb95ec367da'
PIN = 'dependencies/lifecycle-serialization-baseline.json'
FILES = {'ui/service_lifecycle_control.cs', 'tests/service_lifecycle_fixture.cs',
         'tests/service_lifecycle_serialization_tests.cs', 'tests/run_service_lifecycle_serialization.ps1',
         'tests/lifecycle_serialization_scope.py', 'tests/lifecycle_serialization_scope_tests.py',
         'tests/preflight_remediation_scope.py', 'CMakeLists.txt'}
def digest(raw): return hashlib.sha256(raw.replace(b'\r\n', b'\n')).hexdigest()
def repository(root):
    root = Path(root).resolve()
    while not (root / '.git').exists():
        if root.parent == root: raise RuntimeError('Lifecycle history missing')
        root = root.parent
    return str(root)
@lru_cache(maxsize=None)
def git_bytes(repo, ref, name):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', repo, 'show', ref + ':' + name])
def approved(root, git_root=None):
    root = Path(root)
    extra = reopen_approved(root, git_root)
    if not (root / PIN).exists(): return set()
    repo = repository(git_root or root); raw = (root / PIN).read_bytes(); pin = json.loads(raw)
    if digest(raw) != digest(git_bytes(repo, 'HEAD', PIN)): raise RuntimeError('Lifecycle receipt not committed')
    if pin['parent'] != PARENT or pin['authority'] != 'MONAKA-ORCH-HIL-SW-20261028-R1 / SA-01' or set(pin['files']) != FILES:
        raise RuntimeError('Lifecycle exact scope changed')
    for name, entry in pin['files'].items():
        if digest(reopen_project(root, name, (root / name).read_bytes(), git_root)) != entry['sha256']: raise RuntimeError('Lifecycle source drift: ' + name)
        if entry['parent_sha256'] is not None and digest(git_bytes(repo, PARENT, name)) != entry['parent_sha256']:
            raise RuntimeError('Lifecycle predecessor drift: ' + name)
    return FILES | {PIN} | extra
def project(root, name, raw, git_root=None):
    raw = reopen_project(root, name, raw, git_root)
    if name not in FILES: return raw
    root = Path(root)
    if not (root / PIN).exists(): return raw
    entry = json.loads((root / PIN).read_bytes())['files'].get(name)
    if entry and entry['parent_sha256'] is not None and digest(raw) == entry['sha256']:
        return git_bytes(repository(git_root or root), PARENT, name)
    return raw
def project_text(root, text):
    root = Path(root)
    text = reopen_text(root, text)
    if not (root / PIN).exists(): return text
    matches = {git_bytes(repository(root), PARENT, name).decode('utf-8-sig')
               for name, entry in json.loads((root / PIN).read_bytes())['files'].items()
               if entry['parent_sha256'] is not None and entry['sha256'] == digest(text.encode())}
    if len(matches) > 1: raise RuntimeError('Ambiguous lifecycle predecessor')
    return matches.pop() if matches else text
