import hashlib,json,pathlib,zipfile
R=pathlib.Path(__file__).resolve().parents[1]
pin=json.loads((R/'dependencies/mvip.lock.json').read_text())
archive=R/pin['artifact_path'];assert hashlib.sha256(archive.read_bytes()).hexdigest()==pin['sha256']
dest=R/pin['content_path']
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None
 assert set(z.namelist())==set(pin['files_sha256'])|{'kit.lock.json'}
 actual={p.relative_to(dest).as_posix() for p in dest.rglob('*') if p.is_file()}
 assert actual==set(z.namelist()),'MVIP vendor inventory drift'
 for n in z.namelist():
  assert (dest/n).read_bytes()==z.read(n),'MVIP vendor drift '+n
  if n!='kit.lock.json':assert hashlib.sha256(z.read(n)).hexdigest()==pin['files_sha256'][n]
 lock=json.loads(z.read('kit.lock.json'));assert lock['source_commit']==pin['source_commit'] and lock['source_tree_clean']
print('PASS pinned MVIP source/ZIP/member/JAR inventory:',pin['source_commit'])
