"""Build-time exact source/artifact verification; no network or regeneration."""
from pathlib import Path
import hashlib,json,tarfile
R=Path(__file__).resolve().parents[1]
def require(condition,reason):
 if not condition:raise RuntimeError(reason)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
pin=json.loads((R/'dependencies/mbip-p1.lock.json').read_text())
require(pin['source_commit']=='3ea3194479cfa1a04b0fcbcf04916186efb4f9d3','MBIP P1 source pin mismatch')
require(sha(R/pin['archive'])==pin['archive_sha256'],'MBIP archive hash mismatch')
dest=R/pin['content_path'];actual={p.relative_to(dest).as_posix():sha(p) for p in dest.rglob('*') if p.is_file()}
require(actual==pin['files_sha256'],'MBIP canonical source inventory/content drift')
require(sha(dest/'dependencies/mvip-capabilities-p1.zip')==pin['mvip_kit_sha256'],'MVIP kit hash mismatch')
print('PASS pinned canonical P1 runtime source/artifact:',pin['source_commit'],pin['mvip_kit_sha256'])
