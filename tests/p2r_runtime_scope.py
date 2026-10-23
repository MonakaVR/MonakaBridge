"""Explicit P2R evolution of historical scope freezes; never an exception to 5S.
Only the enumerated runtime integration/fixture files may differ. Every such
file must match the new exact baseline, while historical authority/receiver/math
algorithms and all other bytes continue through the original scope assertions.
"""
from pathlib import Path
import hashlib,json,subprocess
from config_write_scope import approved_config_write,predecessor_bytes,predecessor_text
BASE='5fbf2a9a39450e10d4362c440bbecf2c286a4833'
def digest(raw):return hashlib.sha256(raw.replace(b'\r\n',b'\n')).hexdigest()
def approved_runtime(root):
 extra=approved_config_write(root)
 pin=json.loads((Path(root)/'dependencies/p2r-runtime-baseline.json').read_text())
 if pin['base_commit']!=BASE:raise RuntimeError('P2R baseline commit mismatch')
 for name,entry in pin['files'].items():
  raw=predecessor_bytes(root,name,(Path(root)/name).read_bytes())
  actual=digest(raw) if entry['text'] else hashlib.sha256(raw).hexdigest()
  if actual!=entry['sha256']:raise RuntimeError('Unreviewed P2R runtime baseline drift: '+name)
 return set(pin['files'])|{'dependencies/p2r-runtime-baseline.json'}|extra
def runtime_bytes(root,name,raw):
 root=Path(root);approved_runtime(root)
 return _runtime_bytes(root,name,raw,json.loads((root/'dependencies/p2r-runtime-baseline.json').read_text()))

def _runtime_bytes(root,name,raw,pin):
 raw=predecessor_bytes(root,name,raw)
 if name not in pin['files']:return raw
 entry=pin['files'][name]
 if not entry['existed_at_base']:return raw
 old=subprocess.check_output(['git','-C',str(root),'show',BASE+':'+name])
 if digest(old)!=entry['base_sha256']:raise RuntimeError('P2R historical baseline drift: '+name)
 return old

class RuntimeArchiveReview:
 """Validate all scope bytes before and after a complete historical archive.

 The caller still compares every archive member. Per-member predecessor
 projection keeps its exact hash checks; this avoids repeating the same full
 transitive approval graph thousands of times inside an unchanged file list.
 """
 def __init__(self,root):
  self.root=Path(root);approved_runtime(self.root)
  self.pin=json.loads((self.root/'dependencies/p2r-runtime-baseline.json').read_text())
 def project(self,name,raw):return _runtime_bytes(self.root,name,raw,self.pin)
 def verify_current(self):approved_runtime(self.root)
def runtime_text(text):
 root=Path(__file__).resolve().parents[1];text=predecessor_text(root,text);pin=json.loads((root/'dependencies/p2r-runtime-baseline.json').read_text())
 for name,entry in pin['files'].items():
  if entry['text'] and entry['existed_at_base'] and digest(text.encode())==entry['sha256']:
   return runtime_bytes(root,name,text.encode()).decode()
 return text
