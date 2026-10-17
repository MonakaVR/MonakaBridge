"""Owned isolated children only. No real OpenVR DLL, devices, service or sockets."""
from pathlib import Path
import hashlib,json,os,queue,shutil,subprocess,sys,tempfile,threading,time
fixture,production,sentinel=map(lambda p:Path(p).resolve(),sys.argv[1:4])
root=Path(tempfile.mkdtemp(prefix='mb-b2-process-'));cases=[];transcripts=[]
def check(ok,why):
 if not ok:raise RuntimeError(why)
def record(name):cases.append(name);print('PASS',name,flush=True)
class Client:
 def __init__(self,exe,args=(),env=None):
  self.p=subprocess.Popen([str(exe),*map(str,args)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
  self.q=queue.Queue();self.events=[];self.errors=bytearray();self.raw=[];self.requests=[];self.last=0;self.index=0;self.ids={}
  def output():
   for raw in iter(self.p.stdout.readline,b''):
    if len(raw)>65536:self.q.put(RuntimeError('oversized output'));break
    self.raw.append(raw);self.q.put(raw)
   self.q.put(None)
  def errors():
   while True:
    b=self.p.stderr.read(4096)
    if not b:break
    if len(self.errors)+len(b)>256*1024:self.q.put(RuntimeError('stderr limit'));break
    self.errors.extend(b)
  self.reader=threading.Thread(target=output,daemon=True);self.reader.start();self.err=threading.Thread(target=errors,daemon=True);self.err.start()
 def read(self,timeout=15):
  raw=self.q.get(timeout=timeout)
  if isinstance(raw,Exception):raise raw
  if raw is None:raise EOFError('owned child stdout closed')
  event=json.loads(raw);self.events.append(event)
  if 'event_sequence' in event:
   check(event['event_sequence']>self.last and event['previous_emitted_sequence']==self.last,'event chain mismatch');self.last=event['event_sequence']
  return event
 def send(self,j):
  wire=(json.dumps(j,ensure_ascii=False)+'\n').encode('utf-8');self.requests.append(wire);self.p.stdin.write(wire);self.p.stdin.flush()
 def command(self,op,**fields):
  self.index+=1;j=dict(api_version=1,request_id=str(self.index),op=op,**self.ids,**fields);return j
 def execute(self,j):
  self.send(j)
  while True:
   out=self.read()
   if out.get('request_id')==j['request_id']:
    if 'session_id' in out:self.ids={k:out[k] for k in ('session_id','run_id')}
    return out
 def finish(self,expected=0):
  if not self.p.stdin.closed:self.p.stdin.close()
  check(self.p.wait(timeout=5)==expected,'owned child exit mismatch '+str(self.p.returncode))
  self.reader.join(1);self.err.join(1);transcripts.append(dict(command=self.p.args,exit_code=self.p.returncode,events=self.events,stdin_utf8=b''.join(self.requests).decode('utf-8'),stdout_utf8=b''.join(self.raw).decode('utf-8'),stderr=self.errors.decode('utf-8',errors='replace')))
 def fixture_open(self):
  initial=self.read()['test_only_fixture_open'];initial['request_id']='open';out=self.execute(initial);check(out['status']=='OPEN',str(out));return initial
 def prepared(self):
  opened=self.fixture_open();check(self.execute(self.command('discover'))['status']=='DISCOVERED','discover')
  confirmed=self.execute(self.command('confirm_target'));begin=self.command('begin',confirmation_token=confirmed['confirmation_token']);r=self.execute(begin);check(r['status']=='RESULT_READY',str(r))
  p=self.execute(self.command('prepare_commit',result_id=r['result']['result_id']));check(p['status']=='PREPARED',str(p));return opened,p
 def close(self):self.execute(self.command('close'));self.finish()


def review_process(before=False):
 results=[]
 def wait_mark(p,d,n):
  deadline=time.monotonic()+6
  while time.monotonic()<deadline:
   if (d/(n+'.fixture')).exists():return
   if p.poll() is not None:raise RuntimeError('child exited before barrier '+n+': '+str(p.returncode))
   time.sleep(.002)
  raise RuntimeError('barrier timeout '+n)
 for mode in ['reader-failure','reader-ttl','reader-backpressure','writer-race','ttl-busy']+([] if before else ['writer-deadline']):
  started=time.monotonic()
  p=subprocess.Popen([str(fixture),str(root/('review-'+mode)),'review-'+mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0)
  seed=json.loads(p.stdout.readline())['test_only_fixture_open'];d=Path(seed['config_path']).parent;original=Path(seed['config_path']).read_bytes();events=[];timeout=False;reason=None
  def send(j):p.stdin.write((json.dumps(j)+'\n').encode('utf-8'));p.stdin.flush()
  def release(n):(d/(n+'.fixture')).write_text('release')
  try:
   if mode in ('reader-failure','reader-ttl'):
    p.stdin.write((json.dumps(seed)+'\n{').encode());p.stdin.flush();wait_mark(p,d,'reader-paused')
    if mode=='reader-failure':p.stdout.close();release('trigger-write');wait_mark(p,d,'writer-failed')
    else:wait_mark(p,d,'worker-stopped')
    release('release-reader')
   elif mode=='reader-backpressure':
    send(seed);opened=json.loads(p.stdout.readline());events.append(opened);ids={k:opened[k] for k in ('session_id','run_id')}
    for i in range(8):send(dict(api_version=1,request_id='b'+str(i),op='capture_point',**ids))
    p.stdin.write(b'{');p.stdin.flush();wait_mark(p,d,'reader-paused');wait_mark(p,d,'worker-stopped');release('release-writer');release('release-reader')
   elif mode in ('writer-race','writer-deadline'):
    send(seed);wait_mark(p,d,'writer-paused');p.stdin.close();wait_mark(p,d,'writer-stop-requested')
    if mode=='writer-race':release('release-writer')
   else:
    send(seed);events.append(json.loads(p.stdout.readline()));send(seed);events.append(json.loads(p.stdout.readline()));reason=events[-1].get('reason')
   try:exit_code=p.wait(timeout=3)
   except subprocess.TimeoutExpired:timeout=True;p.kill();exit_code=p.wait(timeout=3)
   correct=(not timeout and reason=='WORKER_TTL_EXPIRED') if mode=='ttl-busy' else (not timeout and exit_code in (0,3))
   if not before:
    correct=correct and time.monotonic()-started<2.8
    if mode=='writer-race':correct=correct and (d/'writer-finished.fixture').exists() and not (d/'writer-deadline-expired.fixture').exists()
    if mode=='writer-deadline':correct=correct and (d/'writer-deadline-expired.fixture').exists() and exit_code==3
   results.append(dict(mode=mode,elapsed_seconds=time.monotonic()-started,observed_timeout=timeout,exit_code=exit_code,events=events,meets_fixed_acceptance=correct,fixture_root=str(d),config_unchanged=Path(seed['config_path']).read_bytes()==original))
   check(results[-1]['config_unchanged'],'review fixture changed config')
  finally:
   if p.poll() is None:p.kill();p.wait(timeout=3)
   for pipe in (p.stdin,p.stdout,p.stderr):
    if not pipe.closed:pipe.close()
 (root/'review-process.json').write_text(json.dumps(dict(before=before,results=results),indent=2),encoding='utf-8');print(json.dumps(dict(before=before,results=results,evidence=str(root)),indent=2),flush=True)
 check(all(not x['meets_fixed_acceptance'] if before else x['meets_fixed_acceptance'] for x in results),'review process before/after expectation mismatch')
 return len(results)

if len(sys.argv)>4 and sys.argv[4] in ('--review-only','--review-before'):
 n=review_process(sys.argv[4]=='--review-before');print('PASS B2 review process',n,'cases',flush=True);sys.exit(0)


# Fixture-only bootstrap produces a synthetic config and the explicit open DTO.
c=Client(fixture,[root/'seed']);seed=c.read()['test_only_fixture_open'];c.finish()
testtool=root/'actual-executable';testtool.mkdir();actual=testtool/production.name;shutil.copyfile(production,actual);shutil.copyfile(sentinel,testtool/'openvr_api.dll')
check(actual.read_bytes()==production.read_bytes(),'actual binary copy drift')
env=os.environ.copy();env['LOCALAPPDATA']=str(root/'appdata');Path(env['LOCALAPPDATA']).mkdir()
marker=root/'bootstrap-vr-calls.txt';env['MONAKA_B2_VR_SENTINEL']=str(marker)
p=subprocess.run([str(actual),'--local-api-version'],capture_output=True,env=env,timeout=5);transcripts.append(dict(command=p.args,exit_code=p.returncode,stdout_utf8=p.stdout.decode('utf-8'),stderr=p.stderr.decode('utf-8')));check(p.returncode==0 and json.loads(p.stdout)['capture_supported'] is False,'version bootstrap');check(not marker.exists(),'version called OpenVR');record('actual executable version calls zero OpenVR imports')
c=Client(actual,['--local-api'],env);op=dict(seed,request_id='open',helper_path=str(actual),helper_sha256=hashlib.sha256(actual.read_bytes()).hexdigest());before=Path(seed['config_path']).read_bytes();opened=c.execute(op);check(opened['status']=='OPEN' and opened['capture_supported'] is False,str(opened));check(c.execute(c.command('discover'))['reason']=='CAPTURE_UNSUPPORTED_B3_REQUIRED','production capture fallback');c.close();check(not marker.exists() and not (root/'appdata/MonakaBridge').exists() and Path(seed['config_path']).read_bytes()==before,'bootstrap writes or VR');record('actual executable open/discover/close no VR/drafts/config writes; capture unsupported')
# Positive control makes the same actual exe call its legacy VR path into a
# sentinel, never the real SDK DLL/runtime. This proves the sentinel is active.
control=root/'legacy-vr-positive.txt';env['MONAKA_B2_VR_SENTINEL']=str(control)
p=subprocess.run([str(actual),'--list'],capture_output=True,env=env,timeout=5);transcripts.append(dict(command=p.args,exit_code=p.returncode,stdout_utf8=p.stdout.decode('utf-8'),stderr=p.stderr.decode('utf-8')));check(p.returncode==8 and control.exists() and b'VR_CALL' in control.read_bytes(),'OpenVR sentinel negative control inactive');record('legacy --list positive control detects OpenVR calls without real runtime')
check(b'test_only_fixture_open' not in actual.read_bytes(),'fixture provider leaked into production binary');record('test-only provider marker absent from actual production executable')
unicode_config=Path(seed['config_path']).with_name('measurement space \u6e2c\u5b9a.json');shutil.copyfile(seed['config_path'],unicode_config)
c=Client(actual,['--local-api'],env);op=dict(seed,request_id='unicode',config_path=str(unicode_config),helper_path=str(actual),helper_sha256=hashlib.sha256(actual.read_bytes()).hexdigest());check(c.execute(op)['status']=='OPEN','UTF-8 path boundary');c.close();record('actual stdio UTF-8/space config path selected without shell interpolation')

c=Client(fixture,[root/'success']);op,p=c.prepared();old=Path(op['config_path']).read_bytes();candidate=Path(p['candidate_path']).read_bytes();j=c.command('commit',**{k:p[k] for k in ('result_id','review_token','candidate_sha256')});saved=c.execute(j);check(saved['status']=='SAVED' and saved['runtime']=='UNCONFIRMED',str(saved));check(c.execute(j)['replayed'] is True,'commit replay');check(Path(op['config_path']).read_bytes()==candidate and json.loads(candidate)['mapping_revision']==json.loads(old)['mapping_revision']+1,'same bytes or revision');c.close();record('actual fixture process same-result commit exactly once and replay')
c=Client(fixture,[root/'readonly']);op=c.fixture_open();c.close();c=Client(fixture,[root/'readonly-new']);op=c.read()['test_only_fixture_open'];op.update(request_id='open',readonly=True);check(c.execute(op)['status']=='OPEN','readonly open');check(c.execute(c.command('discover'))['status']=='DISCOVERED','readonly measure');f=c.execute(c.command('confirm_target'));r=c.execute(c.command('begin',confirmation_token=f['confirmation_token']));check(r['status']=='RESULT_READY','readonly measure result');check(c.execute(c.command('prepare_commit',result_id=r['result']['result_id']))['reason']=='READONLY_COMMIT_REJECTED','readonly prepare');c.close();record('readonly native process measures but cannot prepare/commit')
c=Client(fixture,[root/'reuse','slow']);op=c.fixture_open();c.execute(c.command('discover'));f=c.execute(c.command('confirm_target'));begin=c.command('begin',confirmation_token=f['confirmation_token']);c.send(begin);time.sleep(.03);c.send(begin);bad=dict(begin,op='cancel');bad.pop('confirmation_token');c.send(bad);seen=[]
while True:
 e=c.read();seen.append(e)
 if e.get('request_id')==begin['request_id'] and e.get('status')=='RESULT_READY':break
check(any(e.get('status')=='IN_PROGRESS' for e in seen) and any(e.get('reason')=='REQUEST_ID_REUSE_MISMATCH' for e in seen),'pending/reused-ID isolation');c.close();record('pending duplicate is IN_PROGRESS; same-ID different cancel cannot cancel capture')
c=Client(fixture,[root/'cancel','slow']);op=c.fixture_open();old=Path(op['config_path']).read_bytes();c.execute(c.command('discover'));f=c.execute(c.command('confirm_target'));begin=c.command('begin',confirmation_token=f['confirmation_token']);c.send(begin);time.sleep(.05);cancel=c.command('cancel');c.send(cancel);seen=[]
while True:
 e=c.read();seen.append(e)
 if e.get('request_id')==cancel['request_id']:break
check(Path(op['config_path']).read_bytes()==old and any(e.get('phase')=='CANCELLED' for e in seen),'cancelled capture changed config');c.close();record('reserved cancel interrupts owned fixture capture without config change')
for mode,expected in [('post-replace-unknown',0),('post-replace-exit',73)]:
 c=Client(fixture,[root/mode,mode]);op,p=c.prepared();candidate=Path(p['candidate_path']).read_bytes();j=c.command('commit',**{k:p[k] for k in ('result_id','review_token','candidate_sha256')});c.send(j)
 if expected==0:
  e=c.read();check(e['status']=='UNKNOWN','post-replace error faked cancel');c.close()
 else:
  try:c.read()
  except EOFError:pass
  else:raise RuntimeError('hard exit unexpectedly replied')
  c.finish(expected)
 check(Path(op['config_path']).read_bytes()==candidate,'post-replace rollback or incomplete replacement');record(mode+' preserves actual saved bytes; no fake cancellation/retry')
c=Client(fixture,[root/'eof','slow']);op=c.fixture_open();old=Path(op['config_path']).read_bytes();c.execute(c.command('discover'));f=c.execute(c.command('confirm_target'));c.send(c.command('begin',confirmation_token=f['confirmation_token']));time.sleep(.05);c.finish();check(Path(op['config_path']).read_bytes()==old,'EOF changed config');record('EOF during capture closes owned worker with original config')
# Broken/blocked pipe cases use only the handles of these owned fixture children.
p=subprocess.Popen([str(fixture),str(root/'broken')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE);op=json.loads(p.stdout.readline())['test_only_fixture_open'];old=Path(op['config_path']).read_bytes();p.stdout.close();p.stdin.write((json.dumps(op)+'\n').encode());p.stdin.flush();check(p.wait(timeout=5)==3,'broken stdout did not close owned worker');p.stdin.close();check(Path(op['config_path']).read_bytes()==old,'broken stdout changed config');record('broken stdout cancels owned reader/worker without config writes')
p=subprocess.Popen([str(fixture),str(root/'slow-output')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE);op=json.loads(p.stdout.readline())['test_only_fixture_open'];p.stdin.write((json.dumps(op)+'\n').encode());p.stdin.flush();opened=json.loads(p.stdout.readline());ids={k:opened[k] for k in ('session_id','run_id')};old=Path(op['config_path']).read_bytes()
try:
 for i in range(100):p.stdin.write((json.dumps(dict(api_version=1,request_id='flood'+str(i),op='capture_point',**ids))+'\n').encode())
 p.stdin.flush()
except OSError as e:
 if e.errno not in (22,32):raise
try:p.stdin.close()
except OSError as e:
 if e.errno not in (22,32):raise
 if not p.stdin.raw.closed:p.stdin.raw.close()
check(p.wait(timeout=5) in (0,3),'blocked stdout exceeded grace');check(Path(op['config_path']).read_bytes()==old,'slow output changed config');record('slow stdout/queue flood bounded; own process exits within shutdown grace')
# Two independent native sessions freeze the same actual config; the existing
# transaction must have exactly one winning replacement, without rebasing.
a=Client(fixture,[root/'cas-a']);op,pa=a.prepared();b=Client(fixture,[root/'cas-b']);ob=b.read()['test_only_fixture_open'];ob.update(request_id='open',config_path=op['config_path']);check(b.execute(ob)['status']=='OPEN','second CAS open');b.execute(b.command('discover'));confirmed=b.execute(b.command('confirm_target'));result=b.execute(b.command('begin',confirmation_token=confirmed['confirmation_token']));pb=b.execute(b.command('prepare_commit',result_id=result['result']['result_id']));old=json.loads(Path(op['config_path']).read_bytes());a.send(a.command('commit',**{k:pa[k] for k in ('result_id','review_token','candidate_sha256')}));b.send(b.command('commit',**{k:pb[k] for k in ('result_id','review_token','candidate_sha256')}));outcomes=[a.read(),b.read()];check(sum(e.get('status') in ('SAVED','SAVED_CONFIG_EVIDENCE_STALE') for e in outcomes)==1,str(outcomes));check(json.loads(Path(op['config_path']).read_bytes())['mapping_revision']==old['mapping_revision']+1,'CAS double revision');a.close();b.close();record('two actual B2 sessions same-snapshot CAS have exactly one winner and revision+1')
c=Client(fixture,[root/'eof-committing','slow-commit']);op,p=c.prepared();candidate=Path(p['candidate_path']).read_bytes();c.send(c.command('commit',**{k:p[k] for k in ('result_id','review_token','candidate_sha256')}));entered=Path(op['config_path']).parent/'commit-entered.fixture';deadline=time.monotonic()+5
while not entered.exists() and time.monotonic()<deadline:time.sleep(.005)
check(entered.exists(),'owned fixture never entered committing');c.p.stdin.close();reply=c.read();check(reply['status']=='SAVED_CONFIG_EVIDENCE_STALE',str(reply));c.finish();check(Path(op['config_path']).read_bytes()==candidate,'EOF during committing rolled back saved bytes');record('EOF after actual transaction entry reports saved/stale without fake cancellation')
(root/'summary.json').write_text(json.dumps(dict(result='PASS',cases=len(cases),checks=cases,transcripts=transcripts,actual_executable_sha256=hashlib.sha256(actual.read_bytes()).hexdigest()),ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS B2 process',len(cases),'cases; evidence',root,flush=True)
