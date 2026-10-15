"""Copy source-pinned test data and the actual frozen Core comparison, no runtime migration."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];W=R.parents[1]
pin=json.loads((R/'dependencies/mvip.lock.json').read_text());source=R.parent/'MonakaVRInputProtocol'
names=subprocess.check_output(['git','-C',str(source),'ls-tree','-r','--name-only',pin['source_commit'],'fixtures/p1']).decode().splitlines()
names+=subprocess.check_output(['git','-C',str(source),'ls-tree','-r','--name-only',pin['source_commit'],'fixtures/v2.2']).decode().splitlines()
hashes={}
runner='jvm/src/test/kotlin/dev/monaka/protocol/v2/Runner.kt'
b=subprocess.check_output(['git','-C',str(source),'show',pin['source_commit']+':'+runner]);out=R/runner;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(b);hashes[runner]=hashlib.sha256(b).hexdigest()
for name in names:
 b=subprocess.check_output(['git','-C',str(source),'show',pin['source_commit']+':'+name]);out=R/'tests'/name;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(b);hashes['tests/'+name]=hashlib.sha256(b).hexdigest()
p0=W/'hil/phase2cp0-protocol-audit-20261015/snapshots/core-integration'
core_paths=['server/core/src/main/java/dev/monaka/tracking/TrackerInputCapabilities.kt','server/core/src/main/java/dev/monaka/tracking/mtp/MtpObservationBackend.kt']
for name in core_paths:
 b=(p0/name).read_bytes();out=R/'tests/reference/core'/pathlib.Path(name).name;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(b);hashes[out.relative_to(R).as_posix()]=hashlib.sha256(b).hexdigest()
lock=dict(mvip_source_commit=pin['source_commit'],core_repository='MonakaVR/MonakaVR',core_source_commit='0cee1fda1f12c4d8cb9379b6ab15e3efe2a91b4b',core_source_paths=core_paths,files_sha256=hashes)
(R/'tests/evidence.lock.json').write_text(json.dumps(lock,indent=2)+'\n',encoding='utf-8')
print('PASS pinned test fixtures / exact Core source evidence',len(hashes))
