"""Exact reviewed config-I/O evolution; old P2R manifest remains immutable."""
from pathlib import Path
import hashlib,json,subprocess
from functools import lru_cache
from capability_runtime_scope import approved_capabilities,predecessor as capability_predecessor,predecessor_text as capability_predecessor_text
BASE='5278c839893b0716ba72ffd11117b6f90eb77bc2'
PIN='dependencies/config-write-baseline.json'
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',BASE+':'+name])
def pin(root):return json.loads((Path(root)/PIN).read_text(encoding='utf-8'))
def approved_config_write(root,git_root=None):
 root=Path(root);p=pin(root);repo=Path(git_root) if git_root else root
 extra=approved_capabilities(root,git_root)
 if p['base_commit']!=BASE:raise RuntimeError('config write baseline commit mismatch')
 if digest((root/'dependencies/p2r-runtime-baseline.json').read_bytes())!=p['unchanged_p2r_manifest_sha256']:raise RuntimeError('historical P2R manifest changed')
 for name,e in p['files'].items():
  if digest(capability_predecessor(root,name,(root/name).read_bytes(),git_root))!=e['sha256']:raise RuntimeError('unreviewed config write drift: '+name)
  if e['existed_at_base']:
   old=historical(str(repo),name)
   if digest(old)!=e['base_sha256']:raise RuntimeError('config write predecessor drift: '+name)
 return set(p['files'])|{PIN}|extra
def predecessor_bytes(root,name,raw):
 raw=capability_predecessor(root,name,raw)
 e=pin(root)['files'].get(name)
 if not e:return raw
 if e['existed_at_base'] and digest(raw)==e['base_sha256']:return raw
 if digest(raw)!=e['sha256']:raise RuntimeError('unreviewed config write drift: '+name)
 if not e['existed_at_base']:return raw
 old=historical(str(root),name)
 if digest(old)!=e['base_sha256']:raise RuntimeError('config write predecessor drift: '+name)
 return old
def predecessor_text(root,text):
 text=capability_predecessor_text(root,text)
 for name,e in pin(root)['files'].items():
  if e['existed_at_base'] and digest(text.encode())==e['sha256']:return predecessor_bytes(root,name,text.encode()).decode()
 return text
