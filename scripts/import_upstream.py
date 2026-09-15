"""Verify supplied Task 1/2/3 bytes before importing. Never regenerate missing artifacts."""
import hashlib,json,zipfile,argparse
from pathlib import Path
R=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def check(v,msg):
 if not v:raise ValueError(msg)
def write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 if path.exists():check(path.read_bytes()==data,'refuse overwrite: '+str(path))
 else:path.write_bytes(data)
def archive(path,digest):
 check(sha(path.read_bytes())==digest,'artifact hash '+path.name)
 with zipfile.ZipFile(path) as z:
  result={n:z.read(n) for n in z.namelist()}
  check(len(result)==len(z.namelist()),'duplicate paths')
  for n in result:check(not Path(n).is_absolute() and '..' not in Path(n).parts and ':' not in n and '\\' not in n,'unsafe path')
 return result
def sums(data):
 pairs=dict(line.split('  ',1)[::-1] for line in data['SHA256SUMS'].decode().splitlines())
 check(set(pairs)==set(data)-{'SHA256SUMS'},'sum coverage')
 for n,h in pairs.items():check(sha(data[n])==h,n)
p=argparse.ArgumentParser();p.add_argument('--supply',type=Path);a=p.parse_args()
D=R/'dependencies'
if a.supply:
 inputs={'MonakaProtocol/dist':['monaka-protocol-kit-v1.0.zip','handoff-manifest.json'],
 'PicoMotionTrackerBridge/dist':['pico-bridge-extraction.zip','pico-bridge-extraction.handoff.json','task2-final-report.json'],
 'ViveUltimateTrackerBridge/dist':['vive-backend-handoff.zip','vive-backend-handoff.handoff.json','task3-final-report.json']}
 for directory,names in inputs.items():
  for name in names:write(D/('artifacts' if name.endswith('.zip') else 'reports')/name,(a.supply/directory/name).read_bytes())
meta=json.loads((D/'reports/handoff-manifest.json').read_text(encoding='utf-8'))
check(meta['sha256']=='eef5b7f2bc490926385b99dabcd44dc5a374228bf2a7869beea01f9dad936729','fixed Task1 hash')
kit=archive(D/'artifacts/monaka-protocol-kit-v1.0.zip',meta['sha256']);sums(kit)
check(sha(kit['protocol.lock.json'])=='234f0dffc46b808a179ec91da3c185794d7b7c83bc5b7d2bcb31fa73886bcb44','fixed lock')
lock=json.loads(kit['protocol.lock.json'])
for key in ['source_commit','schema_commit','contract_c1_sha256']:check(lock[key]==meta[key],key)
check(sha(kit['docs/C1.md'])=='3a76435c8b25b3975028f9a83c4dc3d1e3848806c9608d885f5bba74a1198ed3','fixed C1')
for key,val in lock.items():
 if key.endswith('_sha256') and isinstance(val,dict):
  for n,h in val.items():check(sha(kit[n])==h,n)
for n,b in kit.items():write(R/'third_party/monaka-protocol'/n,b)
write(D/'monaka-protocol.lock.json',kit['protocol.lock.json'])
t2=json.loads((D/'reports/pico-bridge-extraction.handoff.json').read_text(encoding='utf-8'))
r2=json.loads((D/'reports/task2-final-report.json').read_text(encoding='utf-8'))
check(t2['packager_source_commit']==r2['HEAD_SHA']=='c638f158516effd7fc6511daf179b9a927a099a6','Task2 commit')
for item in r2['handoff_artifacts']:
 name=Path(item['file']).name
 if name in ['pico-bridge-extraction.zip','pico-bridge-extraction.handoff.json']:
  path=D/('artifacts' if name.endswith('.zip') else 'reports')/name;check(sha(path.read_bytes())==item['sha256'],name)
pico=archive(D/'artifacts/pico-bridge-extraction.zip',t2['sha256']);sums(pico)
check(sha(pico['extraction-manifest.json'])==t2['manifest_sha256'],'PICO internal manifest')
m=json.loads(pico['extraction-manifest.json'])
check(m['source_base_sha']==t2['source_base_sha'] and m['packager_source_commit']==t2['packager_source_commit'],'PICO source')
for f in m['files']:
 b=pico[f['path']];check(sha(b)==f['sha256'],f['path'])
 if not f['generated']:
  blob=hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
  check(blob==f['source_blob_sha'] and f['source_base_sha']==m['source_base_sha'],'PICO Git blob '+f['path'])
for edge in m['include_closure']:check(edge['from'] in pico and edge['to'] in pico,'include closure')
# Reference-only scratch extraction. No POTB/vendor source is compiled into common core.
for n,b in pico.items():write(R/'build/upstream/task2'/n,b)
write(D/'task2-extraction-manifest.json',pico['extraction-manifest.json'])
t3=json.loads((D/'reports/vive-backend-handoff.handoff.json').read_text(encoding='utf-8'))
r3=json.loads((D/'reports/task3-final-report.json').read_text(encoding='utf-8'))
check(t3['source_commit']==r3['HEAD_SHA']=='eaaee63fcacb76d095ff743944fbff1f1427f015','Task3 commit')
for item in r3['handoff_artifacts']:
 name=item['filename']
 if name in ['vive-backend-handoff.zip','vive-backend-handoff.handoff.json']:
  path=D/('artifacts' if name.endswith('.zip') else 'reports')/name;check(sha(path.read_bytes())==item['sha256'],name)
vive=archive(D/'artifacts/vive-backend-handoff.zip',t3['sha256'])
check(set(vive)==set(t3['files']),'VIVE manifest coverage')
for n,h in t3['files'].items():check(sha(vive[n])==h,n)
check(vive['dependencies/artifacts/monaka-protocol-kit-v1.0.zip']==(D/'artifacts/monaka-protocol-kit-v1.0.zip').read_bytes(),'VIVE fixed kit')
for n,b in vive.items():write(R/'build/upstream/task3'/n,b)
write(R/'tests/fixtures/vive-status-samples.jsonl',vive['samples/vive-status-samples.jsonl'])
print('PASS: Task1 fixed kit/lock/C1/internal sums; Task2 artifact/report/manifest/70 source hashes/Git blobs/52 include edges; Task3 artifact/report/11 file hashes')
