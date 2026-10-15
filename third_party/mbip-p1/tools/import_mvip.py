"""Import one explicit source-pinned kit, never a live sibling-source dependency."""
import argparse,hashlib,json,pathlib,zipfile
R=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--kit',type=pathlib.Path,required=True);p.add_argument('--manifest',type=pathlib.Path,required=True);a=p.parse_args()
pin=json.loads(a.manifest.read_text());data=a.kit.read_bytes()
assert hashlib.sha256(data).hexdigest()==pin['sha256']
with zipfile.ZipFile(a.kit) as z:
 assert z.testzip() is None
 assert set(z.namelist())==set(pin['files_sha256'])|{'kit.lock.json'}
 lock=json.loads(z.read('kit.lock.json'));assert lock['source_tree_clean'] and lock['source_commit']==pin['source_commit']
 for n in z.namelist():
  parts=pathlib.PurePosixPath(n);assert not parts.is_absolute() and '..' not in parts.parts and ':' not in n and '\\' not in n
  b=z.read(n)
  if n!='kit.lock.json':assert hashlib.sha256(b).hexdigest()==pin['files_sha256'][n]
  out=R/'third_party/mvip'/n;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(b)
(R/'dependencies').mkdir(exist_ok=True);(R/'dependencies/mvip-capabilities-p1.zip').write_bytes(data)
pin.update(artifact_path='dependencies/mvip-capabilities-p1.zip',content_path='third_party/mvip')
(R/'dependencies/mvip.lock.json').write_text(json.dumps(pin,indent=2)+'\n',encoding='utf-8')
print('PASS imported exact MVIP kit',pin['source_commit'])
