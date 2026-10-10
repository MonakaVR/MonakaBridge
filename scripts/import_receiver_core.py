"""Explicit 5AB pure-state extraction; never imports Slime/device/IK/network runtime."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];source=Path(sys.argv[1]);commit='299dda9d31d98ac28da018c5be2a916e36193566'
assert subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD']).decode().strip()==commit
assert not subprocess.check_output(['git','-C',str(source),'status','--porcelain']).strip()
names=['server/core/src/main/java/dev/monaka/tracking/'+n for n in ['CommonWorldAuthority.kt','TrustedHmdCommonWorldStream.kt']]
names += ['server/core/src/main/java/io/github/axisangles/ktmath/'+n for n in ['Vector3.kt','Quaternion.kt','Matrix3.kt','EulerAngles.kt','Transform.kt','LICENSE-MIT','LICENSE-APACHE']]
names+=['LICENSE-MIT','LICENSE-APACHE']
assert all(not (ROOT/'receiver/upstream'/n.removeprefix('server/core/src/main/java/')).exists() for n in names),'Initial extraction only; do not overwrite reviewed recovery APIs'
rows=[]
for n in names:
    raw=(source/n).read_bytes(); blob=subprocess.check_output(['git','-C',str(source),'show',commit+':'+n])
    assert raw.replace(b'\r\n',b'\n')==blob.replace(b'\r\n',b'\n'),'Only Git checkout CRLF conversion is permitted'
    short=n.removeprefix('server/core/src/main/java/');dest=ROOT/'receiver/upstream'/short;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(raw)
    rows.append({'source_path':n,'path':dest.relative_to(ROOT).as_posix(),'original_sha256':hashlib.sha256(raw).hexdigest(),'git_blob_sha256':hashlib.sha256(blob).hexdigest()})
(ROOT/'receiver/provenance.json').write_text(json.dumps({'repository':'MonakaVR/MonakaVR','commit':commit,'scope':'two Common World state classes and ktmath only; receiver recovery API additions documented separately','files':rows},indent=2)+'\n')
print('PASS extracted actual 5AB Core with source commit / byte provenance and licenses')
