"""Exact capability integration layer above immutable config/P2R baselines."""
from pathlib import Path
import hashlib,json,subprocess
from functools import lru_cache
BASE='e08b95f8e147d75a9d48c4eb9e747a1db9194c1a'
PIN='dependencies/capabilities-runtime-baseline.json'
FILES={'tests/world_calibration_tests.cpp','tests/trajectory_calibration_tests.cpp','include/monaka_bridge/config.hpp','src/mapping/config.cpp','CMakeLists.txt','include/monaka_bridge/bridge.hpp','include/monaka_bridge/registry.hpp',
 'include/monaka_bridge/runtime_normalization.hpp','src/observation/registry.cpp','src/routing/bridge.cpp',
 'src/routing/runtime_normalization.cpp','tests/rotation_only_downstream_tests.cpp','tests/capability_propagation_tests.cpp',
 'tests/config_write_scope.py','tests/config_write_scope_tests.py','tests/capability_runtime_scope.py',
 'tests/capability_runtime_scope_tests.py','docs/capabilities-runtime-integration.md'}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',BASE+':'+name])
def approved_capabilities(root,git_root=None):
 root=Path(root);repo=str(git_root or root);p=json.loads((root/PIN).read_text())
 if p['base_commit']!=BASE or set(p['files'])!=FILES:raise RuntimeError('capability scope/base mismatch')
 for name in ['dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json']:
  if digest((root/name).read_bytes())!=digest(historical(repo,name)):raise RuntimeError('historical baseline changed: '+name)
 for name,e in p['files'].items():
  if digest((root/name).read_bytes())!=e['sha256']:raise RuntimeError('unreviewed capability drift: '+name)
  if e['existed_at_base'] and digest(historical(repo,name))!=e['base_sha256']:raise RuntimeError('capability predecessor drift: '+name)
 return FILES|{PIN}
def predecessor(root,name,raw,git_root=None):
 # Approval boundaries validate every current source without caching. Projection
 # checks just the supplied bytes; repeating the whole approval for every entry
 # in every historical archive otherwise makes the guard quadratic.
 root=Path(root);p=json.loads((root/PIN).read_text());e=p['files'].get(name)
 if not e:return raw
 if e['existed_at_base'] and digest(raw)==e['base_sha256']:return raw
 if digest(raw)!=e['sha256']:raise RuntimeError('unreviewed capability drift: '+name)
 return historical(str(git_root or root),name) if e['existed_at_base'] else raw
def predecessor_text(root,text):
 root=Path(root);approved_capabilities(root);p=json.loads((root/PIN).read_text())
 for name,e in p['files'].items():
  if e['existed_at_base'] and digest(text.encode())==e['sha256']:return historical(str(root),name).decode()
 return text
