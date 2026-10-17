"""Owned isolated children only. No real OpenVR DLL, devices, service or sockets."""
from pathlib import Path
import hashlib,json,os,queue,shutil,subprocess,sys,tempfile,threading,time
from calibration_review_events import ReviewObserver
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
 for mode in ['reader-failure','reader-ttl','reader-backpressure','writer-race','ttl-busy']+([] if before else ['writer-deadline']):
  started=time.perf_counter_ns();points={'popen_start':started};case=root/('review-'+mode);case.mkdir();events=[];seedraw=b'';wire=[];errors=bytearray();sync=None;d=None;original=None;reason=None;timeout=False;stage_error=None;replyraw=[]
  p=subprocess.Popen([str(fixture),str(case),'review-'+mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0)
  def stderr_read():
   while True:
    b=p.stderr.read(4096)
    if not b:break
    errors.extend(b)
  et=threading.Thread(target=stderr_read,daemon=True);et.start();seedq=queue.Queue()
  st=threading.Thread(target=lambda:seedq.put(p.stdout.readline()),daemon=True);st.start()
  def send(j):
   raw=(json.dumps(j)+'\n').encode('utf-8');wire.append(raw);p.stdin.write(raw);p.stdin.flush()
  def read():
   raw=p.stdout.readline();replyraw.append(raw);return json.loads(raw)
  try:
   seedraw=seedq.get(timeout=6);points['seed']=time.perf_counter_ns();seed_packet=json.loads(seedraw);seed=seed_packet['test_only_fixture_open'];d=Path(seed['config_path']).parent;original=Path(seed['config_path']).read_bytes();sync=ReviewObserver(p,seed_packet['test_only_review_events'])
   if mode in ('reader-failure','reader-ttl'):
    raw=(json.dumps(seed)+'\n{').encode();wire.append(raw);p.stdin.write(raw);p.stdin.flush();sync.wait('reader-paused')
    if mode=='reader-failure':
     sync.wait('writer-paused');sync.bind_threads();p.stdout.close();sync.signal('trigger-write');sync.wait('writer-failed')
    else:
     sync.wait('worker-stopped');sync.wait('writer-finished');sync.bind_threads()
    sync.signal('release-reader')
   elif mode=='reader-backpressure':
    send(seed);opened=read();events.append(opened);sync.bind_threads();ids={k:opened[k] for k in ('session_id','run_id')}
    for i in range(8):send(dict(api_version=1,request_id='b'+str(i),op='capture_point',**ids))
    wire.append(b'{');p.stdin.write(b'{');p.stdin.flush();sync.wait('reader-paused');sync.wait('worker-stopped');sync.signal('release-writer');sync.signal('release-reader')
   elif mode in ('writer-race','writer-deadline'):
    send(seed);sync.wait('writer-paused');points['writer_paused']=time.perf_counter_ns();sync.bind_threads();p.stdin.close();points['stdin_closed']=time.perf_counter_ns();sync.wait('writer-stop-requested');points['stop_seen']=time.perf_counter_ns()
    # Race release occurs inside the fixture's stop hook, after the first cancel.
    # The parent never has to complete a disk roundtrip inside the100ms window.
   else:
    send(seed);events.append(read());sync.bind_threads();send(seed);events.append(read());reason=events[-1].get('reason')
   try:exit_code=p.wait(timeout=3)
   except subprocess.TimeoutExpired:timeout=True;p.kill();exit_code=p.wait(timeout=3)
   points['exit_observed']=time.perf_counter_ns()
  except Exception as e:
   stage_error=repr(e)
   if p.poll() is None:p.kill();p.wait(timeout=3)
   exit_code=p.returncode
  finally:
   if p.poll() is None:p.kill();p.wait(timeout=3)
   et.join(1);st.join(1)
   if not p.stdout.closed:replyraw.append(p.stdout.read())
   for pipe in (p.stdin,p.stdout,p.stderr):
    if not pipe.closed:pipe.close()
  telemetry=sync.snapshot() if sync else {};markers=sync.markers(d) if sync and d else [];threads=telemetry.get('threads',{});native=telemetry.get('points',{});signaled=telemetry.get('events',{})
  shutdown=None
  if 'worker' in threads and threads['worker']['times']['exit']:
   shutdown=(telemetry['process_times']['exit']-threads['worker']['times']['exit'])/10000000
  startup=(points.get('seed',started)-started)/1000000000
  total=(points.get('exit_observed',time.perf_counter_ns())-started)/1000000000
  correct=stage_error is None and not timeout and total<2.8 and startup<6 and shutdown is not None and 0<=shutdown<2.8 and exit_code in (0,3)
  if mode=='ttl-busy':correct=correct and reason=='WORKER_TTL_EXPIRED'
  if mode=='writer-race':
   order=[native.get(key,0) for key in ['writer-paused','writer-stop-requested','release-writer','writer-call-ready','writer-failed','writer-finished']]
   correct=correct and all(order) and order==sorted(order) and not signaled.get('writer-deadline-expired') and threads.get('writer',{}).get('wait_zero')==0 and threads['writer']['exit_code']==0
  if mode=='writer-deadline':
   correct=correct and signaled.get('writer-deadline-expired') and not signaled.get('release-writer') and not signaled.get('writer-call-ready') and exit_code==3 and threads.get('writer',{}).get('wait_zero')==0 and threads['writer']['exit_code']==3
  unchanged=d is not None and Path(seed['config_path']).read_bytes()==original
  observation=dict(mode=mode,elapsed_seconds=(time.perf_counter_ns()-started)/1000000000,startup_seconds=startup,process_total_seconds=total,shutdown_seconds=shutdown,observed_timeout=timeout,stage_error=stage_error,exit_code=exit_code,events=events,meets_fixed_acceptance=bool(correct),fixture_root=str(d),config_unchanged=unchanged,points_ns=points,telemetry=telemetry,markers=markers,stderr=errors.decode('utf-8',errors='replace'))
  (case/'stdin.bin').write_bytes(b''.join(wire));(case/'stdout.bin').write_bytes(seedraw+b''.join(replyraw));(case/'stderr.bin').write_bytes(errors);(case/'observation.json').write_text(json.dumps(observation,indent=2),encoding='utf-8');results.append(observation)
  if sync:sync.close()
  check(unchanged,'review fixture changed config')
 (root/'review-process.json').write_text(json.dumps(dict(before=before,results=results),indent=2),encoding='utf-8');print(json.dumps(dict(before=before,results=results,evidence=str(root)),indent=2),flush=True)
 check(all(not x['meets_fixed_acceptance'] if before else x['meets_fixed_acceptance'] for x in results),'review process before/after expectation mismatch')
 return len(results)

def provider_options_process():
 def proof(c,seed):
  c.close();p=json.loads(c.errors.decode('utf-8'));check(p['captures']==0 and not (Path(seed['config_path']).parent/'drafts').exists(),'N0a resource/draft activity');return p
 c=Client(fixture,[root/'n0a-once','n0a-options']);seed=c.fixture_open();check(c.execute(seed)['replayed'] is True,'open replay');bad=dict(seed,readonly=True);check(c.execute(bad)['reason']=='REQUEST_ID_REUSE_MISMATCH','open mismatch');check(c.execute(dict(seed,request_id='second'))['status']=='REJECTED','second open');p=proof(c,seed);check(p['configuration_checks']==1 and p['configuration_present'] and p['source']=='pico' and p['tracker']=='logical-pico' and p['declaration_present'] and p['mirror_port']==49123,'configured snapshot');record('N0a stdio configure exactly once/replay/mismatch/second open')
 c=Client(fixture,[root/'n0a-failure','n0a-configure-fail']);seed=c.read()['test_only_fixture_open'];seed['request_id']='open';out=c.execute(seed);check(out['reason']=='FIXTURE_CONFIGURE_REJECTED' and out['phase']=='FAILED','configure failure');check(c.execute(seed)['replayed'] is True,'failure replay');check(c.execute(dict(seed,request_id='new-open'))['status']=='REJECTED','failure retry');check(c.execute(c.command('discover'))['status']=='REJECTED','failed discover');p=proof(c,seed);check(p['configuration_checks']==1 and not p['configuration_present'] and p['discoveries']==0,'failed partial publication');record('N0a stdio configure failure terminal/unpublished/replay')
 c=Client(fixture,[root/'n0a-invalid','n0a-options']);seed=c.read()['test_only_fixture_open'];seed['request_id']='open';before=Path(seed['config_path']).read_bytes();check(c.execute(dict(seed,mirror_port=65536))['status']=='REJECTED','invalid port');seed.update(request_id='valid',readonly=True,reference_role='hmd',mirror_port=49345);check(c.execute(seed)['status']=='OPEN','valid after invalid');p=proof(c,seed);check(p['configuration_checks']==1 and p['readonly'] and p['reference_role']=='hmd' and p['mirror_port']==49345 and Path(seed['config_path']).read_bytes()==before,'readonly snapshot');record('N0a invalid open never configures; valid readonly metadata')
 c=Client(fixture,[root/'n0a-optional','n0a-options']);seed=c.read()['test_only_fixture_open'];seed['request_id']='open';path=Path(seed['config_path']);j=json.loads(path.read_bytes());[m.pop('runtime_contract',None) for m in j['mappings'] if m['source_id']=='pico' and m['device_id']=='device'];path.write_text(json.dumps(j),encoding='utf-8');check(c.execute(seed)['status']=='OPEN','optional declaration open');check(c.execute(c.command('discover'))['reason']=='EXPLICIT_DECLARATION_REQUIRED','missing declaration refusal');p=proof(c,seed);check(p['configuration_checks']==1 and not p['declaration_present'] and p['discoveries']==1,'optional snapshot');record('N0a optional declaration absent at open/discover refusal')
 (root/'summary.json').write_text(json.dumps(dict(result='PASS',cases=len(cases),checks=cases,transcripts=transcripts),indent=2),encoding='utf-8');print('PASS N0a process',len(cases),'cases; evidence',root,flush=True)

if len(sys.argv)>4 and sys.argv[4]=='--provider-options':
 provider_options_process();sys.exit(0)
if len(sys.argv)>4 and sys.argv[4] in ('--review-only','--review-before'):
 n=review_process(sys.argv[4]=='--review-before');print('PASS B2 review process',n,'cases',flush=True);sys.exit(0)


# Fixture-only bootstrap produces a synthetic config and the explicit open DTO.
c=Client(fixture,[root/'seed']);seed=c.read()['test_only_fixture_open'];c.finish()
testtool=root/'actual-executable';testtool.mkdir();actual=testtool/production.name;shutil.copyfile(production,actual);shutil.copyfile(sentinel,testtool/'openvr_api.dll')
check(actual.read_bytes()==production.read_bytes(),'actual binary copy drift')
env=os.environ.copy();env['LOCALAPPDATA']=str(root/'appdata');Path(env['LOCALAPPDATA']).mkdir()
marker=root/'bootstrap-vr-calls.txt';env['MONAKA_B2_VR_SENTINEL']=str(marker)
p=subprocess.run([str(actual),'--local-api-version'],capture_output=True,env=env,timeout=5);transcripts.append(dict(command=p.args,exit_code=p.returncode,stdout_utf8=p.stdout.decode('utf-8'),stderr=p.stderr.decode('utf-8')));check(p.returncode==0 and json.loads(p.stdout)['capture_supported'] is True,'version bootstrap');check(not marker.exists(),'version called OpenVR');record('actual executable version calls zero OpenVR imports')
c=Client(actual,['--local-api'],env);op=dict(seed,request_id='open',helper_path=str(actual),helper_sha256=hashlib.sha256(actual.read_bytes()).hexdigest());before=Path(seed['config_path']).read_bytes();opened=c.execute(op);check(opened['status']=='OPEN' and opened['capture_supported'] is True,str(opened));c.close();check(not marker.exists() and not (root/'appdata/MonakaBridge').exists() and Path(seed['config_path']).read_bytes()==before,'bootstrap writes or VR');record('actual executable open/close resource-free; implemented capture available only after discover')
# N0a contract through the actual product executable/RuntimeCaptureProvider,
# not the older generic fixture Provider. A missing declaration is optional at
# open and mandatory at discover before any SDK call or mirror acquisition.
missing_config=root/'canonical-missing-declaration.json';missing=json.loads(before)
for mapping in missing['mappings']:mapping.pop('runtime_contract',None)
missing_bytes=json.dumps(missing).encode('utf-8');missing_config.write_bytes(missing_bytes)
missing_marker=root/'missing-declaration-vr-calls.txt';missing_env=dict(env,MONAKA_B2_VR_SENTINEL=str(missing_marker))
for readonly in (False,True):
 c=Client(actual,['--local-api'],missing_env);request=dict(op,request_id='missing-open',config_path=str(missing_config),readonly=readonly)
 opened=c.execute(request);check(opened['status']=='OPEN' and opened['phase']=='OPEN' and opened['readonly']==readonly,str(opened))
 rejected=c.execute(c.command('discover'));check(rejected['status']=='REJECTED' and rejected['reason']=='EXPLICIT_DECLARATION_REQUIRED' and rejected['phase']=='INSPECT_ONLY',str(rejected));c.close()
 check(not missing_marker.exists() and missing_config.read_bytes()==missing_bytes and not (root/'appdata/MonakaBridge').exists(),'missing declaration touched SDK/config/draft')
record('actual production RuntimeCaptureProvider missing-declaration open writable/readonly succeeds; discover rejects with zero sentinel SDK calls; no draft/config writes')
c=Client(actual,['--local-api'],env);check(c.execute(op)['status']=='OPEN','N1 open');out=c.execute(c.command('discover'));check(out.get('reason')=='OPENVR_BACKGROUND_UNAVAILABLE',str(out));c.close();check(marker.exists() and b'VR_CALL' in marker.read_bytes() and Path(seed['config_path']).read_bytes()==before and not (root/'appdata/MonakaBridge').exists(),'N1 failed init safety');record('actual production discover enters SDK sentinel and fails before mirror bind; no real SDK/socket/draft/config activity')
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
