"""Admit this current remediation before immutable predecessor audit views."""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
from lifecycle_serialization_scope import approved as lifecycle_approved, project as lifecycle_project, project_text as lifecycle_text
PIN='dependencies/preflight-remediation-baseline.json'
PARENT='e857c5bcf2e38b9f18a9601f98a12fab21afbb65'
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def repository(root):
    p=Path(root).resolve()
    while not (p/'.git').exists():
        if p.parent==p:raise RuntimeError('Preflight history unavailable')
        p=p.parent
    return str(p)
@lru_cache(maxsize=None)
def git_bytes(repo,ref,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',ref+':'+name])
def approved(root,git_root=None):
    root=Path(root)
    extra=lifecycle_approved(root,git_root)
    if not (root/PIN).exists():return set()
    raw=(root/PIN).read_bytes();repo=repository(git_root or root)
    if digest(raw)!=digest(git_bytes(repo,'HEAD',PIN)):raise RuntimeError('Preflight receipt differs from committed review')
    p=json.loads(raw)
    if p['parent']!=PARENT:raise RuntimeError('Preflight predecessor changed')
    for name,entry in p['files'].items():
        if digest(lifecycle_project(root,name,(root/name).read_bytes(),git_root))!=entry['sha256']:raise RuntimeError('Preflight current source drift: '+name)
        if entry['parent_sha256'] is not None and digest(git_bytes(repo,PARENT,name))!=entry['parent_sha256']:
            raise RuntimeError('Preflight predecessor drift: '+name)
    return set(p['files'])|{PIN}|extra
def project(root,name,raw,git_root=None):
    root=Path(root)
    raw=lifecycle_project(root,name,raw,git_root)
    if not (root/PIN).exists():return raw
    e=json.loads((root/PIN).read_bytes())['files'].get(name)
    if e and e['parent_sha256'] is not None and digest(raw)==e['sha256']:
        return git_bytes(repository(git_root or root),PARENT,name)
    return raw
def project_text(root,text):
    root=Path(root)
    text=lifecycle_text(root,text)
    if not (root/PIN).exists():return text
    sha=digest(text.encode());p=json.loads((root/PIN).read_bytes())
    values={git_bytes(repository(root),PARENT,n).decode('utf-8-sig') for n,e in p['files'].items() if e['parent_sha256'] is not None and e['sha256']==sha}
    if len(values)>1:raise RuntimeError('Ambiguous preflight predecessor view')
    return values.pop() if values else text
