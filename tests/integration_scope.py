"""Current policy admission; historical projections are provenance, not rules.

The committed successor receipt pins actual current bytes before any predecessor
view is supplied to frozen HIL audits. Functional tests use the real sources.
"""
from pathlib import Path
from functools import lru_cache
import hashlib,json,subprocess
PIN='dependencies/integration-policy-baseline.json'
PARENT='f4f17bbf2af1a8f1691625a959116b178b7c5d95'
RULES='dependencies/current-rule-hierarchy.json'
CLAUSES=(
 'Producer identity is data, not a switch selecting product-specific logic.',
 'MUST NOT silently assume unknown, undeclared, or unverified input coordinate spaces are identity transforms.',
 'It MUST NOT embed product-specific installation searches, namespace substitutions, resource tables, or asset-selection conditions.',
 'Do not assume such fields already exist.',
 'keep them isolated as migration data, not hard-coded branching, and record their removal plan.',
 'Do not delete branches or files, force-push, rebase, amend, reset away work, or otherwise rewrite history.',
 'Do not claim physical-device HIL without actually performing it.',
)
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def repository(root):
 p=Path(root).resolve()
 while not (p/'.git').exists():
  if p.parent==p:raise RuntimeError('Integration history unavailable')
  p=p.parent
 return str(p)
@lru_cache(maxsize=None)
def git_bytes(repo,ref,name):return subprocess.check_output(['git','--no-optional-locks','-C',repo,'show',ref+':'+name])
def semantic(root):
 root=Path(root);agents=(root/'AGENTS.md').read_text(encoding='utf-8-sig')
 for clause in CLAUSES:
  if clause not in agents:raise RuntimeError('Integration required policy clause missing: '+clause)
 for name in ['outputs/steamvr/driver/presentation.cpp','outputs/steamvr/driver/presentation.hpp','outputs/steamvr/driver/render_manifest.hpp.in','tools/prepare_presentation.cpp']:
  text=(root/name).read_text(encoding='utf-8-sig').lower()
  if any(token in text for token in ('pico','altra','vive')):raise RuntimeError('Integration product-specific resolver code: '+name)
def approved(root,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return set()
 repo=repository(git_root or root);raw=(root/PIN).read_bytes()
 if digest(raw)!=digest(git_bytes(repo,'HEAD',PIN)):raise RuntimeError('Integration receipt differs from committed review')
 p=json.loads(raw)
 if p['historical_hil_commit']!=PARENT:raise RuntimeError('Integration historical authority changed')
 for name,e in p['files'].items():
  try:actual=(root/name).read_bytes()
  except OSError as exc:raise RuntimeError('Integration missing source: '+name) from exc
  if digest(actual)!=e['sha256']:raise RuntimeError('Integration current source drift: '+name)
  if e['historical_sha256'] is not None and digest(git_bytes(repo,PARENT,name))!=e['historical_sha256']:raise RuntimeError('Integration historical provenance drift: '+name)
 hierarchy=json.loads((root/RULES).read_bytes())
 for rule in hierarchy['rules']:
  if digest((root/rule['path']).read_bytes())!=rule['normalized_sha256']:raise RuntimeError('Integration current rule drift: '+rule['path'])
  if digest(git_bytes(repo,rule['source_commit'],rule['path']))!=rule['normalized_sha256']:raise RuntimeError('Integration rule source mismatch')
 # Include untracked nested instructions in the audit, not only Git's inventory.
 agents={str(x.relative_to(root)).replace('\\','/') for x in root.rglob('AGENTS.md') if '.git' not in x.parts and 'build' not in x.parts}
 if agents!={r['path'] for r in hierarchy['rules'] if r['path'].endswith('AGENTS.md')}:raise RuntimeError('Integration instruction hierarchy changed')
 semantic(root)
 return set(p['files'])|{PIN}
def project(root,name,raw,git_root=None):
 root=Path(root)
 if not (root/PIN).exists():return raw
 e=json.loads((root/PIN).read_bytes())['files'].get(name)
 if e and e['historical_sha256'] is not None and digest(raw)==e['sha256']:
  return git_bytes(repository(git_root or root),PARENT,name)
 return raw
def project_text(root,text):
 root=Path(root)
 if not (root/PIN).exists():return text
 p=json.loads((root/PIN).read_bytes());sha=digest(text.encode())
 values={git_bytes(repository(root),PARENT,n).decode('utf-8-sig') for n,e in p['files'].items() if e['historical_sha256'] is not None and e['sha256']==sha}
 if len(values)>1:raise RuntimeError('Integration ambiguous historical projection')
 return values.pop() if values else text
