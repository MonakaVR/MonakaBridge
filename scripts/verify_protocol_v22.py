"""Read-only integrity of the actual clean-commit publisher-identity kit."""
from pathlib import Path,PurePosixPath
import hashlib,json,zipfile
R=Path(__file__).resolve().parents[1]
def require(ok,why):
    if not ok:raise RuntimeError(why)
def sha(b):return hashlib.sha256(b).hexdigest()
def verify():
    pin=json.loads((R/'dependencies/monaka-protocol-v2.2.lock.json').read_bytes())
    require(pin['wire_version']=={'major':2,'minor':2},'publisher identity requires version 2.2')
    archive,metadata=R/pin['artifact_path'],R/pin['manifest_path'];destination=R/'third_party/monaka-protocol-v22'
    require(sha(archive.read_bytes())==pin['sha256'],'ZIP SHA256 mismatch')
    require(sha(metadata.read_bytes())==pin['manifest_sha256'],'manifest SHA256 mismatch')
    manifest=json.loads(metadata.read_bytes())
    for k in ['sha256','source_commit','wire_version','protocol_lock_sha256','contract_revision_sha256']:
        require(manifest[k]==pin[k],'manifest mismatch '+k)
    with zipfile.ZipFile(archive) as z:
        names=z.namelist();require(z.testzip() is None,'ZIP CRC mismatch')
        require(len(names)==len(set(names))==len({n.casefold() for n in names}),'duplicate archive path')
        for n in names:
            p=PurePosixPath(n);require(not p.is_absolute() and '..' not in p.parts and ':' not in n and '\\' not in n,'unsafe archive path')
        require(sha(z.read('protocol.lock.json'))==pin['protocol_lock_sha256'],'lock mismatch')
        lock=json.loads(z.read('protocol.lock.json'));require(lock['source_tree_clean'] and lock['source_commit']==pin['source_commit'],'committed clean source provenance')
        require(lock['wire_version']==pin['wire_version'],'lock version mismatch')
        for doc,k in [('C2.md','contract_c2_sha256'),('C2.1.md','contract_c21_sha256'),('C2.2.md','contract_c22_sha256')]:
            require(sha(z.read('docs/'+doc))==pin[k]==lock[k],'contract mismatch '+doc)
        sums={}
        for line in z.read('SHA256SUMS').decode().splitlines():
            h,n=line.split('  ',1);require(n not in sums and sha(z.read(n))==h,'internal hash '+n);sums[n]=h
        require(set(sums)==set(names)-{'SHA256SUMS'},'incomplete checksum coverage')
        for n,h in lock['files_sha256'].items():require(sha(z.read(n))==h,'lock content '+n)
        actual={p.relative_to(destination).as_posix() for p in destination.rglob('*') if p.is_file()}
        require(actual==set(names),'fixed kit member inventory mismatch')
        for n in names:require(not (destination/n).is_symlink() and (destination/n).read_bytes()==z.read(n),'modified kit member '+n)
    print('PASS exact production protocol 2.2 ZIP/manifest/lock/contracts/member hashes; 2.1 remains separate')
if __name__=='__main__':verify()
