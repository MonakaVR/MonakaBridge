"""R3 exact current admission; preserve all immutable historical receipts.

Only historical static checks project to R2. Every functional test compiles and
executes current product sources. A forged receipt cannot admit a source change.
"""
from pathlib import Path
import hashlib, json, subprocess
from functools import lru_cache
from native_owner_scope import approved as native_approved, project as native_project, project_text as native_text
PARENT='0bda5a09a3af187cf1af230b741e596932ebadc8'
PIN='dependencies/installation-owner-r3.json'
FILES={'ui/installation_control.cs','ui/service_lifecycle_control.cs',
       'ui/steamvr_control_gui_main.cs','ui/steamvr_control_tray.cs','ui/steamvr_control_wpf.cs',
       'tests/installation_owner_tests.cs','tests/run_installation_owner.ps1',
       'tests/installation_owner_scope.py','tests/installation_owner_scope_tests.py',
       'tests/tray_reopen_scope.py','tests/tray_reopen_scope_tests.py',
       'docs/installation-owner.md','CMakeLists.txt'}
def digest(raw): return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def repository(root):
    root=Path(root).resolve()
    while not (root/'.git').exists():
        if root.parent==root: raise RuntimeError('Owner predecessor unavailable')
        root=root.parent
    return str(root)
@lru_cache(maxsize=None)
def git_bytes(repo,ref,name): return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',ref+':'+name])
def approved(root,git_root=None):
    root=Path(root)
    extra=native_approved(root,git_root)
    if not (root/PIN).exists(): return set()
    repo=repository(git_root or root);raw=(root/PIN).read_bytes();pin=json.loads(raw)
    if digest(raw)!=digest(git_bytes(repo,'HEAD',PIN)): raise RuntimeError('Owner receipt not committed')
    if pin['parent']!=PARENT or pin['authority']!='MONAKA-OFFLINE-PREHIL-CLOSURE-20261028-R3 / SA-01' or set(pin['files'])!=FILES: raise RuntimeError('Owner exact scope changed')
    for name,entry in pin['files'].items():
        if digest(native_project(root,name,(root/name).read_bytes(),git_root))!=entry['sha256']: raise RuntimeError('Owner current source drift: '+name)
        if entry['parent_sha256'] is not None and digest(git_bytes(repo,PARENT,name))!=entry['parent_sha256']: raise RuntimeError('Owner predecessor drift: '+name)
    return FILES|{PIN}|extra
def project(root,name,raw,git_root=None):
    raw=native_project(root,name,raw,git_root)
    root=Path(root)
    if name not in FILES or not (root/PIN).exists(): return raw
    entry=json.loads((root/PIN).read_bytes())['files'].get(name)
    if entry and entry['parent_sha256'] is not None and digest(raw)==entry['sha256']: return git_bytes(repository(git_root or root),PARENT,name)
    return raw
def project_text(root,text):
    text=native_text(root,text)
    root=Path(root)
    if not (root/PIN).exists(): return text
    matches={git_bytes(repository(root),PARENT,name).decode('utf-8-sig') for name,entry in json.loads((root/PIN).read_bytes())['files'].items() if entry['parent_sha256'] is not None and entry['sha256']==digest(text.encode())}
    if len(matches)>1: raise RuntimeError('Ambiguous owner predecessor')
    return matches.pop() if matches else text
