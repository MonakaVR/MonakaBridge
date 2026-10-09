"""Check actual 5AE adapter vectors using the unchanged pinned ordering oracle."""
import hashlib,json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.dont_write_bytecode=True
sys.path.insert(0,str(root/'third_party/monaka-protocol-v2/tools'))
from ordered_stream_reference import Reference
expected={'live-normal':['world','mapping','pose','unavailable','pose'],
 'live-replacement':['world','mapping','pose','revoked','world','mapping','pose']}
directory=Path(sys.argv[1]); manifest={}
for name,outcomes in expected.items():
 p=directory/(name+'.jsonl'); events=[json.loads(line) for line in p.read_text().splitlines()]
 r=Reference(); actual=[r.accept(e) for e in events]
 assert actual==outcomes,(name,actual,outcomes)
 for event in events:
  assert event['timestamp_kind']=='receive' and event['timestamp_ns']==event['sent_at_ns']
  if event['type']=='common_pose':
   assert event['common_position']==event['source_position'] and event['common_orientation']==event['source_orientation']
 manifest[p.name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'events':len(events),'outcomes':actual}
 print('PASS live ingress stream',p.name,len(events),'events')
(directory/'generated-stream-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
