import hashlib,json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1];sys.dont_write_bytecode=True
sys.path.insert(0,str(root/'third_party/monaka-protocol-v2/tools'))
from ordered_stream_reference import Reference
directory=Path(sys.argv[1]); expected={'sender-normal':['world','mapping','pose','pose'],
 'sender-reconnect':['world','mapping','pose','world','mapping','pose']}; manifest={}
for name,outcomes in expected.items():
 p=directory/(name+'.jsonl'); events=[json.loads(s) for s in p.read_text().splitlines()];r=Reference()
 actual=[];session=None
 for e in events:
  if session is not None and e['session_id']!=session:r=Reference()
  session=e['session_id'];actual.append(r.accept(e))
 assert actual==outcomes,(name,actual)
 manifest[p.name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'events':len(events),'outcomes':actual}
 print('PASS sender wire stream',name,len(events),'events')
(directory/'generated-stream-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
