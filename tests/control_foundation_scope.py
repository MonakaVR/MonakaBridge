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
ARCHIVE_AGENTS_BLOB='5086ee762f4eb0f9c0daf781f34acfc1db6120d5'
LEGACY_REASON='Exact upstream AGENTS inheritance; immutable archive document and all other assertions retained'
# Test-guard sources only: these are not additions to the 19 implementation paths.
LEGACY_GUARDS={
 'tests/receiver_scope.py':{
  'sha256':'ffde44f0bf018edb0b8202c23e95d6c318fe581c4a41dd43845d760cc1adbc8e',
  'base_sha256':'e9d544a9e626c32721ae4e37459c6852fab4a54a7078a1a8feeb05593f972b64',
  'archive_base':'f2f0a46199af711f91c7e575c6f9229bdff51614',
  'assertion':"            assert current==old,'5AI-P production/test baseline changed: '+member.name\n",
  'reason':LEGACY_REASON},
 'tests/trusted_live_source_scope.py':{
  'sha256':'f73c15f451b8f94fc403fc40fe2bcb1163c7fa92b2db563726856c51eb1e490b',
  'base_sha256':'0908b4ef0de7b0ae8dbd6c36697367f4562fb05a3ff0f24a0a28824e5917252b',
  'archive_base':'a2a5df9640ffff5dcfac67faf845cc880286142f',
  'assertion':"            assert current==old,'R2 production/test baseline changed: '+member.name\n",
  'reason':LEGACY_REASON}}
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

def verify_archive_agents(root,base,old,current,git_root=None):
 # The real archive bytes stay old; the worktree/HEAD must separately be current.
 # Never replace, normalize or project either side of this raw-byte boundary.
 root=Path(root);repo=Path(git_root or root);approved_upstream_agents(root,repo)
 if base not in {e['archive_base'] for e in LEGACY_GUARDS.values()}:raise RuntimeError('unapproved AGENTS archive base')
 expected=subprocess.check_output(['git','--no-optional-locks','-C',str(repo),'show',base+':'+UPSTREAM_AGENTS])
 blob=hashlib.sha1(b'blob '+str(len(old)).encode()+b'\0'+old).hexdigest()
 if old!=expected or blob!=ARCHIVE_AGENTS_BLOB:raise RuntimeError('historical archive AGENTS drift')
 upstream=subprocess.check_output(['git','--no-optional-locks','-C',str(repo),'show',UPSTREAM_COMMIT+':'+UPSTREAM_AGENTS])
 head=subprocess.check_output(['git','--no-optional-locks','-C',str(repo),'rev-parse','HEAD:'+UPSTREAM_AGENTS]).decode().strip()
 if current!=(root/UPSTREAM_AGENTS).read_bytes() or current!=upstream or head!=UPSTREAM_BLOB:raise RuntimeError('current raw/HEAD AGENTS differs from exact upstream')
 print('PASS exact current AGENTS raw/blob/LF and immutable archive AGENTS: '+base)

def legacy_guard_source(root,name,raw,git_root=None):
 # Reconstruct only these two test sources, using audited markers and exact pins.
 # No product source or AGENTS document is projected by this extension.
 e=LEGACY_GUARDS.get(name)
 if not e:return raw
 original=historical(str(git_root or root),name)
 if digest(original)!=e['base_sha256']:raise RuntimeError('legacy guard predecessor drift: '+name)
 if raw==original:return raw
 if digest(raw)!=e['sha256']:raise RuntimeError('unreviewed legacy guard drift: '+name)
 text=raw.decode().replace('\r\n','\n')
 imports='# BEGIN WS01A EXACT AGENTS IMPORT\nfrom control_foundation_scope import verify_archive_agents\n# END WS01A EXACT AGENTS IMPORT\n'
 start='            # BEGIN WS01A EXACT AGENTS COMPARISON\n';end='            # END WS01A EXACT AGENTS COMPARISON\n'
 if text.count(imports)!=1 or text.count(start)!=1 or text.count(end)!=1:raise RuntimeError('legacy guard marker drift: '+name)
 text=text.replace(imports,'');a=text.index(start);b=text.index(end,a)+len(end)
 restored=(text[:a]+e['assertion']+text[b:]).encode()
 if restored!=original:raise RuntimeError('legacy guard assertions changed: '+name)
 return restored
def approved_control(root,git_root=None):
 root=Path(root);repo=Path(git_root or root);p=json.loads((root/PIN).read_text(encoding='utf-8'))
 approved_upstream_agents(root,repo)
 if p['base_commit']!=BASE or set(p['files'])!=FILES:raise RuntimeError('control scope/base mismatch')
 if set(LEGACY_GUARDS)!={'tests/receiver_scope.py','tests/trusted_live_source_scope.py'}:raise RuntimeError('legacy guard scope changed')
 for name,e in LEGACY_GUARDS.items():
  if e['reason']!=LEGACY_REASON or digest((root/name).read_bytes())!=e['sha256']:raise RuntimeError('unreviewed legacy guard approval: '+name)
  legacy_guard_source(root,name,(root/name).read_bytes(),repo)
 for name in OLD_PINS:
  if digest((root/name).read_bytes())!=digest(historical(str(repo),name)):raise RuntimeError('historical baseline changed: '+name)
 for name,e in p['files'].items():
  if digest((root/name).read_bytes())!=e['sha256']:raise RuntimeError('unreviewed control drift: '+name)
  if existed(str(repo),name)!=e['existed_at_base']:raise RuntimeError('control predecessor presence drift: '+name)
  if e['existed_at_base'] and digest(historical(str(repo),name))!=e['base_sha256']:raise RuntimeError('control predecessor drift: '+name)
 # This exact upstream document is validated separately, never projected into
 # the historical implementation manifest or admitted by a Markdown wildcard.
 return FILES|{PIN,UPSTREAM_AGENTS}|set(LEGACY_GUARDS)
def verify_diff_scope(root):
 allowed=approved_control(root)
 names=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'diff','--name-only',BASE,'--']).decode().splitlines()
 names+=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'ls-files','--others','--exclude-standard']).decode().splitlines()
 unexpected=set(names)-allowed
 if unexpected:raise RuntimeError('outside WS01a scope: '+','.join(sorted(unexpected)))
def predecessor(root,name,raw,git_root=None):
 if name in LEGACY_GUARDS:return legacy_guard_source(root,name,raw,git_root)
 root=Path(root);p=json.loads((root/PIN).read_text(encoding='utf-8'));e=p['files'].get(name)
 if not e:return raw
 if e['existed_at_base'] and digest(raw)==e['base_sha256']:return raw
 if digest(raw)!=e['sha256']:raise RuntimeError('unreviewed control drift: '+name)
 return historical(str(git_root or root),name) if e['existed_at_base'] else raw
def predecessor_text(root,text):
 root=Path(root);approved_control(root);p=json.loads((root/PIN).read_text(encoding='utf-8'))
 for name,e in LEGACY_GUARDS.items():
  if digest(text.encode())==e['sha256']:return legacy_guard_source(root,name,text.encode()).decode()
 for name,e in p['files'].items():
  if e['existed_at_base'] and digest(text.encode())==e['sha256']:return historical(str(root),name).decode()
 return text
