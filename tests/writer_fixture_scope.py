"""Exact test-only writer fixture layer; all accepted pins stay immutable."""
from pathlib import Path
from functools import lru_cache
import hashlib,json,re,subprocess
BASE='b39216bcc69687ae2c0cce974ba259146c5982d7'
N0A_BASE='31f47a4672b0eef22ef55bd2c6d866103e483ee6'
PIN='dependencies/writer-fixture-baseline.json'
N0A_PIN='dependencies/writer-fixture-n0a-baseline.json'
PROVIDER_PIN='dependencies/provider-options-baseline.json'
FILES={'tests/calibration_session_process_fixture.cpp','tests/calibration_session_process.py',
 'tests/calibration_review_events.hpp','tests/calibration_review_events.py','CMakeLists.txt',
 'tests/writer_fixture_scope.py','tests/writer_fixture_scope_tests.py','docs/writer-fixture-events.md',
 'tests/calibration_session_scope.py','tests/calibration_session_scope_tests.py'}
N0A_EXTRA={'tests/provider_options_scope.py','tests/provider_options_scope_tests.py'}
OLD_PINS={'dependencies/calibration-session-baseline.json','dependencies/measurement-target-baseline.json',
 'dependencies/capabilities-runtime-baseline.json','dependencies/config-write-baseline.json','dependencies/p2r-runtime-baseline.json'}
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
@lru_cache(maxsize=None)
def historical(repo,base,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',base+':'+name])
@lru_cache(maxsize=None)
def existed(repo,base,name):return subprocess.run(['git','--no-optional-locks','-C',repo,'cat-file','-e',base+':'+name],capture_output=True).returncode==0
def profile(root):
 return (N0A_BASE,N0A_PIN,FILES|N0A_EXTRA,OLD_PINS|{PROVIDER_PIN}) if (Path(root)/PROVIDER_PIN).exists() else (BASE,PIN,FILES,OLD_PINS)
def approved_writer_fixture(root,git_root=None):
 root=Path(root);repo=str(git_root or root);base,pin,files,old_pins=profile(root);p=json.loads((root/pin).read_bytes())
 if p['base_commit']!=base or set(p['files'])!=files:raise RuntimeError('writer fixture scope/base mismatch')
 for name in old_pins:
  if (root/name).read_bytes()!=historical(repo,base,name):raise RuntimeError('writer fixture changed accepted pin: '+name)
 if pin==N0A_PIN:
  dependency=p['writer_dependency_commit']
  if not re.fullmatch('[0-9a-f]{40}',dependency):raise RuntimeError('writer dependency identity')
  if subprocess.run(['git','--no-optional-locks','-C',repo,'merge-base','--is-ancestor',dependency,'HEAD'],capture_output=True).returncode:raise RuntimeError('writer dependency ancestry')
  if (root/PIN).read_bytes()!=historical(repo,dependency,PIN):raise RuntimeError('writer dependency pin changed')
 for name,e in p['files'].items():
  if digest((root/name).read_bytes())!=e['sha256']:raise RuntimeError('writer fixture unreviewed current source: '+name)
  if existed(repo,base,name)!=e['existed_at_base']:raise RuntimeError('writer fixture predecessor presence: '+name)
  if e['existed_at_base'] and digest(historical(repo,base,name))!=e['base_sha256']:raise RuntimeError('writer fixture predecessor hash: '+name)
 return files|{PIN}|({N0A_PIN} if pin==N0A_PIN else set())
def check_names(names,allowed):
 unexpected=set(names)-set(allowed)
 if unexpected:raise RuntimeError('outside writer fixture scope: '+','.join(sorted(unexpected)))
def verify_diff_scope(root):
 base,_,_,_=profile(root)
 names=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'diff','--name-only',base,'--']).decode().splitlines()
 names+=subprocess.check_output(['git','--no-optional-locks','-C',str(root),'ls-files','--others','--exclude-standard']).decode().splitlines()
 check_names(names,approved_writer_fixture(root))
def predecessor(root,name,raw,git_root=None):
 root=Path(root);base,pin,_,_=profile(root);e=json.loads((root/pin).read_bytes())['files'].get(name)
 if not e:return raw
 if e['existed_at_base'] and digest(raw)==e['base_sha256']:return raw
 if digest(raw)!=e['sha256']:raise RuntimeError('writer fixture unreviewed projection: '+name)
 return historical(str(git_root or root),base,name) if e['existed_at_base'] else raw
def predecessor_text(root,text):
 root=Path(root);approved_writer_fixture(root);base,pin,_,_=profile(root)
 for name,e in json.loads((root/pin).read_bytes())['files'].items():
  if e['existed_at_base'] and digest(text.encode())==e['sha256']:return historical(str(root),base,name).decode()
 return text
