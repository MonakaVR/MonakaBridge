"""Exact N0a layer over accepted B2. Current bytes are never approval-cached."""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
from writer_fixture_scope import approved_writer_fixture,predecessor as writer_predecessor,predecessor_text as writer_predecessor_text
BASE='b39216bcc69687ae2c0cce974ba259146c5982d7'
PIN='dependencies/provider-options-baseline.json'
FILES={'include/monaka_bridge/calibration_session.hpp','src/calibration/calibration_session.cpp',
 'tests/calibration_fixture_provider.hpp','tests/calibration_session_tests.cpp',
 'tests/calibration_session_process_fixture.cpp','tests/calibration_session_process.py',
 'docs/provider-open-options.md','tests/provider_options_scope.py','tests/provider_options_scope_tests.py',
 'CMakeLists.txt','tests/calibration_session_scope.py','tests/calibration_session_scope_tests.py'}
OLD_PINS={'dependencies/calibration-session-baseline.json','dependencies/measurement-target-baseline.json',
 'dependencies/capabilities-runtime-baseline.json','dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json'}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
@lru_cache(maxsize=None)
def historical(repo,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',BASE+':'+name])
@lru_cache(maxsize=None)
def existed(repo,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',BASE+':'+name],capture_output=True).returncode==0
def approved_provider_options(root,git_root=None):
 root=Path(root);repo=str(git_root or root);extra=approved_writer_fixture(root,git_root);p=json.loads((root/PIN).read_bytes())
 if p['base_commit']!=BASE or set(p['files'])!=FILES:raise RuntimeError('N0a scope/base mismatch')
 for name in OLD_PINS:
  if digest((root/name).read_bytes())!=digest(historical(repo,name)):raise RuntimeError('N0a historical pin changed: '+name)
 for name,e in p['files'].items():
  if digest(writer_predecessor(root,name,(root/name).read_bytes(),git_root))!=e['sha256']:raise RuntimeError('N0a unreviewed current source: '+name)
  if existed(repo,name)!=e['existed_at_base']:raise RuntimeError('N0a predecessor presence: '+name)
  if e['existed_at_base'] and digest(historical(repo,name))!=e['base_sha256']:raise RuntimeError('N0a predecessor hash: '+name)
 return FILES|{PIN}|extra
def verify_diff_scope(root):
 names=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'diff','--name-only',BASE,'--']).decode().splitlines()
 names+=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'ls-files','--others','--exclude-standard']).decode().splitlines()
 unexpected=set(names)-FILES-{PIN}-approved_writer_fixture(root)
 if unexpected:raise RuntimeError('outside N0a scope: '+','.join(sorted(unexpected)))
def predecessor(root,name,raw,git_root=None):
 root=Path(root);raw=writer_predecessor(root,name,raw,git_root);p=json.loads((root/PIN).read_bytes());e=p['files'].get(name)
 if not e:return raw
 if e['existed_at_base'] and digest(raw)==e['base_sha256']:return raw
 if digest(raw)!=e['sha256']:raise RuntimeError('N0a unreviewed projection: '+name)
 return historical(str(git_root or root),name) if e['existed_at_base'] else raw
def predecessor_text(root,text):
 root=Path(root);text=writer_predecessor_text(root,text);approved_provider_options(root);p=json.loads((root/PIN).read_bytes())
 for name,e in p['files'].items():
  if e['existed_at_base'] and digest(text.encode())==e['sha256']:return historical(str(root),name).decode()
 return text
