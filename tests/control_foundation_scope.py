"""Exact WS01a UI/control evolution above immutable runtime/config baselines."""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
BASE='279a01363f47ad3564b5cd8b9346180c49ee41d3'
PIN='dependencies/control-foundation-baseline.json'
UPSTREAM_AGENTS='AGENTS.md'
UPSTREAM_COMMIT='3473624a451ac172c36ea46cc5d897d84eadad43'
UPSTREAM_BLOB='6b4de2a3c7e1dd010adabb43cb7e6b8e67fb10fe'
UPSTREAM_SHA256='a01520ec5cf61d2abc7c9800d3e90b4f39a9ec4f4c2bb9b31aff6006b9a39736'
FILES={
 'ui/control_models.cs','ui/installation_control.cs','ui/steamvr_control_gui_main.cs','ui/steamvr_control_wpf.cs','ui/steamvr_control_tray.cs',
 'scripts/build_gui.ps1','scripts/package_control.ps1',
 'tests/ui_production_tests.cs','tests/control_foundation_tests.cs','tests/control_helper_fixture.cs','tests/control_foundation_tests.ps1',
 'tests/control_packaged_smoke.py','tests/control_foundation_scope.py','tests/control_foundation_scope_tests.py',
 'tests/capability_runtime_scope.py','tests/capability_runtime_scope_tests.py','tests/config_write_scope_tests.py',
 'docs/bridge-control-foundation.md','docs/ws01a-audit-correction.md'}
OLD_PINS={'dependencies/capabilities-runtime-baseline.json','dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json'}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',BASE+':'+name])
@lru_cache(maxsize=None)
def existed(repo,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',BASE+':'+name],capture_output=True).returncode==0
def approved_upstream_agents(root,repo):
 try:raw=(Path(root)/UPSTREAM_AGENTS).read_bytes().replace(b'\r\n',b'\n')
 except OSError as error:raise RuntimeError('upstream AGENTS missing/unreadable') from error
 blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
 if digest(raw)!=UPSTREAM_SHA256 or blob!=UPSTREAM_BLOB:raise RuntimeError('unreviewed upstream AGENTS content')
 upstream=subprocess.check_output(['git','--no-optional-locks','-C',str(repo),'rev-parse',UPSTREAM_COMMIT+':'+UPSTREAM_AGENTS]).decode().strip()
 if upstream!=UPSTREAM_BLOB:raise RuntimeError('upstream AGENTS commit/blob mismatch')
def approved_control(root,git_root=None):
 root=Path(root);repo=Path(git_root or root);p=json.loads((root/PIN).read_text(encoding='utf-8'))
 approved_upstream_agents(root,repo)
 if p['base_commit']!=BASE or set(p['files'])!=FILES:raise RuntimeError('control scope/base mismatch')
 for name in OLD_PINS:
  if digest((root/name).read_bytes())!=digest(historical(str(repo),name)):raise RuntimeError('historical baseline changed: '+name)
 for name,e in p['files'].items():
  if digest((root/name).read_bytes())!=e['sha256']:raise RuntimeError('unreviewed control drift: '+name)
  if existed(str(repo),name)!=e['existed_at_base']:raise RuntimeError('control predecessor presence drift: '+name)
  if e['existed_at_base'] and digest(historical(str(repo),name))!=e['base_sha256']:raise RuntimeError('control predecessor drift: '+name)
 # This exact upstream document is validated separately, never projected into
 # the historical implementation manifest or admitted by a Markdown wildcard.
 return FILES|{PIN,UPSTREAM_AGENTS}
def verify_diff_scope(root):
 allowed=approved_control(root)
 names=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'diff','--name-only',BASE,'--']).decode().splitlines()
 names+=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'ls-files','--others','--exclude-standard']).decode().splitlines()
 unexpected=set(names)-allowed
 if unexpected:raise RuntimeError('outside WS01a scope: '+','.join(sorted(unexpected)))
def predecessor(root,name,raw,git_root=None):
 root=Path(root);p=json.loads((root/PIN).read_text(encoding='utf-8'));e=p['files'].get(name)
 if not e:return raw
 if e['existed_at_base'] and digest(raw)==e['base_sha256']:return raw
 if digest(raw)!=e['sha256']:raise RuntimeError('unreviewed control drift: '+name)
 return historical(str(git_root or root),name) if e['existed_at_base'] else raw
def predecessor_text(root,text):
 root=Path(root);approved_control(root);p=json.loads((root/PIN).read_text(encoding='utf-8'))
 for name,e in p['files'].items():
  if e['existed_at_base'] and digest(text.encode())==e['sha256']:return historical(str(root),name).decode()
 return text
