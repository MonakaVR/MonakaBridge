"""R3 exact current admission; preserve all immutable historical receipts.

Only historical static checks project to R2. Every functional test compiles and
executes current product sources. A forged receipt cannot admit a source change.
"""
from pathlib import Path
import hashlib, json, subprocess
from functools import lru_cache
PARENT='7371bb842ae84523e7195740deb6ea2247c99b39'
PIN='dependencies/native-owner-r3.json'
FILES={'tests/native_owner_scope.py', 'tests/native_owner_scope_tests.py', 'tools/native_owner.cpp', 'tests/installation_owner_scope_tests.py', 'tests/run_native_owner_client.ps1', 'ui/installation_control.cs', 'tools/bridge_service.cpp', 'include/monaka_bridge/service_runtime.hpp', 'tests/native_owner_process.py', 'CMakeLists.txt', 'tests/installation_owner_scope.py', 'ui/steamvr_control_gui_main.cs', 'docs/native-owner.md', 'tests/native_owner_client_tests.cs'}

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
    if not (root/PIN).exists(): return set()
    repo=repository(git_root or root);raw=(root/PIN).read_bytes();pin=json.loads(raw)
    if digest(raw)!=digest(git_bytes(repo,'HEAD',PIN)): raise RuntimeError('Owner receipt not committed')
    if pin['parent']!=PARENT or pin['authority']!='MONAKA-OFFLINE-PREHIL-CLOSURE-20261028-R3 / SA-01 native successor' or set(pin['files'])!=FILES: raise RuntimeError('Owner exact scope changed')
    for name,entry in pin['files'].items():
        if digest((root/name).read_bytes())!=entry['sha256']: raise RuntimeError('Owner current source drift: '+name)
        if entry['parent_sha256'] is not None and digest(git_bytes(repo,PARENT,name))!=entry['parent_sha256']: raise RuntimeError('Owner predecessor drift: '+name)
    return FILES|{PIN}
def project(root,name,raw,git_root=None):
    root=Path(root)
    if name not in FILES or not (root/PIN).exists(): return raw
    entry=json.loads((root/PIN).read_bytes())['files'].get(name)
    if entry and entry['parent_sha256'] is not None and digest(raw)==entry['sha256']: return git_bytes(repository(git_root or root),PARENT,name)
    return raw
def project_text(root,text):
    root=Path(root)
    if not (root/PIN).exists(): return text
    matches={git_bytes(repository(root),PARENT,name).decode('utf-8-sig') for name,entry in json.loads((root/PIN).read_bytes())['files'].items() if entry['parent_sha256'] is not None and entry['sha256']==digest(text.encode())}
    if len(matches)>1: raise RuntimeError('Ambiguous owner predecessor')
    return matches.pop() if matches else text
