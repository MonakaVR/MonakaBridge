"""Owned process watchdog and actual local-API idle invalidation, no devices."""
from pathlib import Path
import json,os,queue,subprocess,sys,tempfile,threading,time,uuid
exe=Path(sys.argv[1]);out=Path(sys.argv[2])/str(uuid.uuid4());out.mkdir(parents=True)
# Native drafts retain the full 64-character run ID. Keep their fixture root
# short; deep build/evidence paths are only for Python's collected raw logs.
native_root=Path(tempfile.mkdtemp(prefix='mb-n0b-'))
def start(mode):
 p=subprocess.Popen([str(exe),str(native_root/mode),mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
 lines=queue.Queue();captured=[]
 def read():
  for raw in iter(p.stdout.readline,b''):captured.append(raw);lines.put(raw)
  lines.put(None)
 t=threading.Thread(target=read,daemon=True);t.start()
 return p,lines,captured,t
receipts=[]
for mode in ['owner-stall','solver-stall','commit-stall']:
 p,lines,captured,reader=start(mode);started=time.monotonic();ready=False
 try:
  while time.monotonic()-started<6:
   line=lines.get(timeout=max(.01,6-(time.monotonic()-started)))
   if line and line.startswith(b'WATCHDOG_READY'):ready=True;break
   if line is None:break
  assert ready,(mode,captured,p.poll())
  watch=time.monotonic()
  try:p.wait(timeout=.5)
  except subprocess.TimeoutExpired:pass
  else:raise AssertionError('fixture did not remain stalled')
  # Independent parent deadline, not the child's worker-done writer grace.
  p.kill();code=p.wait(timeout=3);reader.join(timeout=1)
  assert code!=0 and time.monotonic()-watch<3.5
  receipts.append(dict(mode=mode,fixture_root=str(native_root/mode),ready=True,parent_watchdog=True,exit=code,shutdown_seconds=time.monotonic()-watch,commit_sent=mode=='commit-stall',commit_outcome='UNKNOWN / no retry' if mode=='commit-stall' else 'NOT_SENT',u1_product_client=False))
 finally:
  if p.poll() is None:p.kill();p.wait(timeout=3)
  (out/(mode+'.stdout.bin')).write_bytes(b''.join(captured));(out/(mode+'.stderr.bin')).write_bytes(p.stderr.read())
p,lines,captured,reader=start('api-idle')
def line():
 raw=lines.get(timeout=5);assert raw is not None,(captured,p.poll());return json.loads(raw)
try:
 seed=line()['test_only_fixture_open'];p.stdin.write((json.dumps(seed)+'\n').encode());p.stdin.flush();opened=line();assert opened['status']=='OPEN'
 for index,op in enumerate(['discover','confirm_target'],2):
  request=dict(api_version=1,request_id=str(index),op=op,session_id=opened['session_id'],run_id=opened['run_id'])
  p.stdin.write((json.dumps(request)+'\n').encode());p.stdin.flush();reply=line();assert reply['status']==('DISCOVERED' if op=='discover' else 'CONFIRMED'),reply
 terminal=line();assert terminal['status']=='INSPECT_ONLY' and terminal['reason']=='SESSION_INVALIDATED',terminal
 p.stdin.close();code=p.wait(timeout=3);reader.join(timeout=1);assert code in (0,3),code
 receipts.append(dict(mode='api-idle',fixture_root=str(native_root/'api-idle'),terminal=terminal,exit=code,hardware=False))
finally:
 if p.poll() is None:p.kill();p.wait(timeout=3)
 (out/'api-idle.stdout.bin').write_bytes(b''.join(captured));(out/'api-idle.stderr.bin').write_bytes(p.stderr.read())
(out/'receipts.json').write_text(json.dumps(receipts,indent=2)+'\n',encoding='utf-8')
print(f'PASS N0b parent watchdog3 (owner/solver/commit) + actual local API idle invalidation1; evidence={out}; U1/SDK/GUI/HIL NOT_RUN')
