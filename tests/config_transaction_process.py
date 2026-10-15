"""Actual CLI/process CAS controls. No production service, network or device."""
from pathlib import Path
import sys,subprocess,json,time,concurrent.futures,uuid
cli,probe=map(Path,sys.argv[1:3]);root=Path(sys.argv[3])/str(uuid.uuid4());root.mkdir(parents=True)
base=dict(namespace='monaka.bridge',version=2,bridge_id='transaction-fixture',mapping_revision=10,policy='steamvr',pose_timeout_ms=500,profiles={},mappings=[],unknown_extension={'retain':[1,2,3]})
cases=[]
def fixture(name):
 p=root/name;p.mkdir();target=p/'config.json';target.write_text(json.dumps(base),encoding='utf-8');old=p/'expected.json';old.write_bytes(target.read_bytes());candidate=p/'candidate.json';candidate.write_text(json.dumps(dict(base,policy='both',mapping_revision=11)),encoding='utf-8');return target,old,candidate
def run(*args):return subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=20)
def check(value,message):
 if not value:raise RuntimeError(message)
def record(name):cases.append(name);print('PASS',name,flush=True)
t,e,c=fixture('policy-preserves-unknown');p=run(cli,t,'policy','disabled');check(p.returncode==0,p.stderr);d=json.loads(t.read_text());check(d==dict(base,policy='disabled',mapping_revision=11),'typed native save dropped unknown fields');record('native CLI preserves unknown members and increments once')
t,e,c=fixture('stale');check(run(cli,t,'policy','disabled').returncode==0,'external update failed');p=run(cli,t,'commit',e,c);check(p.returncode!=0 and 'changed' in p.stderr,'stale snapshot not rejected');check(json.loads(t.read_text())['policy']=='disabled','lost external update');record('exact stale snapshot rejected after external CLI update')
t,e,c=fixture('parallel');other=c.with_name('other.json');other.write_text(json.dumps(dict(base,policy='monaka',mapping_revision=11)))
with concurrent.futures.ThreadPoolExecutor(2) as pool:results=list(pool.map(lambda p:run(cli,t,'commit',e,p),[c,other]))
check(sum(p.returncode==0 for p in results)==1,'not exactly one concurrent commit');check(json.loads(t.read_text())['mapping_revision']==11,'duplicate revision');record('two simultaneous same-snapshot writers have exactly one winner')
for stage in ['locked','validated','staged','backed-up','before-replace']:
 t,e,c=fixture('failure-'+stage);original=t.read_bytes();p=run(probe,t,e,c,stage,'fail');check(p.returncode!=0,'injection not active');check(t.read_bytes()==original,'failure changed target');check(run(cli,t,'commit',e,c).returncode==0,'lock not released after failure');record('failure at '+stage+' preserves original and releases ownership')
for stage in ['locked','staged','before-replace']:
 t,e,c=fixture('kill-'+stage);original=t.read_bytes();child=subprocess.Popen(list(map(str,[probe,t,e,c,stage,'hold'])),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  for i in range(200):
   if Path(str(t)+'.probe').exists():break
   if child.poll() is not None:raise RuntimeError('owned probe exited early')
   time.sleep(.01)
  else:raise RuntimeError('owned probe readiness timeout')
  check(run(cli,t,'policy','disabled').returncode!=0,'overlapping writer ignored OS ownership')
 finally:child.kill();child.communicate(timeout=10)
 check(t.read_bytes()==original,'killed precommit changed target');check(run(cli,t,'policy','disabled').returncode==0,'OS lock not crash released');check(json.loads(t.read_text())==dict(base,policy='disabled',mapping_revision=11),'orphan promoted or unknown fields lost');record('owned process kill at '+stage+' releases retained sidecar and ignores orphan staging')
t,e,c=fixture('kill-after-commit');child=subprocess.Popen(list(map(str,[probe,t,e,c,'after-commit','hold'])),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
try:
 for i in range(200):
  if Path(str(t)+'.probe').exists():break
  if child.poll() is not None:raise RuntimeError('postcommit probe exited early')
  time.sleep(.01)
 else:raise RuntimeError('postcommit probe readiness timeout')
 check(t.read_bytes()==c.read_bytes(),'committed document not complete')
finally:child.kill();child.communicate(timeout=10)
check(run(cli,t,'policy','disabled').returncode==0,'postcommit owner did not release');check(json.loads(t.read_text())['mapping_revision']==12,'committed revision rolled back');record('hard exit after linearization preserves committed result and permits next writer')
t,e,c=fixture('typed-current');check(run(probe,t,e,c,'unused','typed').returncode==0,'typed native save failed');check(json.loads(t.read_text())==dict(base,policy='both',mapping_revision=11),'typed save lost unknown members');record('typed native save used by calibrator preserves unknown members')
t,e,c=fixture('typed-threads');check(run(probe,t,e,c,'unused','typed-threads').returncode==0,'same snapshot threads failed');check(json.loads(t.read_text())==dict(base,policy='both',mapping_revision=11),'threaded typed save lost data');record('two native threads sharing one immutable snapshot have exactly one winner')
t,e,c=fixture('typed-stale');check(run(cli,t,'policy','disabled').returncode==0,'external write failed');check(run(probe,t,e,c,'unused','typed').returncode!=0,'typed stale save accepted');check(json.loads(t.read_text())['policy']=='disabled','typed stale overwrite');record('typed native save used by calibrator rejects stale source snapshot')
t,e,c=fixture('invalid');c.write_text(json.dumps(dict(base,policy='both',mapping_revision=12)));check(run(cli,t,'commit',e,c).returncode!=0,'revision skip accepted');check(t.read_bytes()==e.read_bytes(),'invalid revision changed target');record('revision skip rejected without target mutation')
(root/'summary.json').write_text(json.dumps(dict(result='PASS',cases=len(cases),checks=cases,scope='Actual updated native CLI and test-only hook process; old noncooperating binaries excluded'),indent=2))
print('PASS config transaction process',len(cases),'cases',flush=True)
