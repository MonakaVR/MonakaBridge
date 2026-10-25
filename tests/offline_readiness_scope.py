"""Explicit successor scope. Admit current bytes before legacy predecessor views.

The immutable parent remains the source of historical byte assertions. The
current receipt must itself be committed; merely rehashing a mutant cannot
expand authorization. Functional tests always compile/run current sources.
"""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
from final_integration_scope import approved as integration_approved, project as integration_project, project_text as integration_text
PARENT='8f8ca0d4465ada163a122f174b49be13bf7ea80b'
PIN='dependencies/offline-readiness-baseline.json'
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def repository(location):
    p=Path(location).resolve()
    while not (p/'.git').exists():
        if p.parent==p:raise RuntimeError('Readiness history missing')
        p=p.parent
    return str(p)
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',PARENT+':'+name])
def metadata(root):return json.loads((Path(root)/PIN).read_bytes())
@lru_cache(maxsize=None)
def committed_receipt(repo):
    # One immutable HEAD snapshot per test process, never cache mutable files.
    commit=subprocess.check_output(['git','--no-optional-locks','-C',repo,'rev-parse','HEAD']).decode().strip()
    return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',commit+':'+PIN])
def approved_offline(root,git_root=None):
    root=Path(root)
    extra=integration_approved(root,git_root)
    if not (root/PIN).exists():return extra
    repo=repository(git_root or root);raw=(root/PIN).read_bytes()
    if digest(raw)!=digest(committed_receipt(repo)):raise RuntimeError('Readiness receipt differs from committed review')
    p=json.loads(raw)
    if p['parent']!=PARENT or p['authority']!='MonakaVR_Astra_RenderModel_Axis_Alignment_Prepare_and_HIL_Codex_20261024.md':raise RuntimeError('Readiness authority/parent mismatch')
    if hashlib.sha256((root/'dependencies/render-model-axis-authority.md').read_bytes()).hexdigest()!=p['authority_sha256']:raise RuntimeError('Readiness successor authority drift')
    for name,e in p['files'].items():
        try:actual=integration_project(root,name,(root/name).read_bytes(),git_root)
        except OSError as exc:raise RuntimeError('Readiness missing source: '+name) from exc
        if digest(actual)!=e['sha256']:raise RuntimeError('Readiness unreviewed current bytes: '+name)
        if e['parent'] is not None and digest(historical(repo,name))!=e['parent']:raise RuntimeError('Readiness predecessor drift: '+name)
    return set(p['files'])|{PIN}|extra
def project(root,name,raw,git_root=None):
    root=Path(root)
    raw=integration_project(root,name,raw,git_root)
    if not (root/PIN).exists():return raw
    e=metadata(root)['files'].get(name)
    if e and e['parent'] is not None and digest(raw)==e['sha256']:return historical(repository(git_root or root),name)
    return raw
def project_text(root,text):
    root=Path(root)
    text=integration_text(root,text)
    if not (root/PIN).exists():return text
    values={historical(repository(root),name).decode('utf-8-sig') for name,e in metadata(root)['files'].items() if e['parent'] is not None and e['sha256']==digest(text.encode())}
    if len(values)>1:raise RuntimeError('Readiness ambiguous predecessor view')
    return values.pop() if values else text
