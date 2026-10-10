from pathlib import Path
import json, sys, time, concurrent.futures
ROOT=Path(__file__).resolve().parents[1]; sys.path.insert(0,str(ROOT/'scripts'))
from capture_common_world_fault import LocalControlClient, save_fault_pair
binary=Path(sys.argv[1]).resolve(); base=Path(sys.argv[2]).resolve()/str(time.monotonic_ns()); base.mkdir(parents=True)
source=[json.loads(s) for s in (ROOT/'third_party/monaka-protocol-v2/fixtures/v2.1/ordered-streams/source-normal.jsonl').read_text(encoding='utf-8').splitlines()]
cases=0
def check(ok):
 if not ok: raise AssertionError('control host expectation failed')
def run(name,fn):
 global cases
 fn(); cases+=1; print('PASS '+name)
def client(name):
 p=base/name; p.mkdir(); (p/'state').mkdir(); (p/'control').mkdir()
 c=json.loads((ROOT/'deployment/production.activation.json').read_text()); c.update(enabled=False,endpoint='udp://192.0.2.10:30111',owner_id='software-only',world_space_id='software-world',source_publisher_id=source[0]['publisher_id'],revision_path=str(p/'state/world.rev'))
 path=p/'config.json'; path.write_text(json.dumps(c)); return LocalControlClient(binary,path,p/'control/EMERGENCY_STOP'),p
c,p=client('continuous')
try:
 run('local inherited pipe keeps runtime Created alive',lambda:check(c.command('status')['after']['gate_state']=='DisabledByDefault'))
 run('unknown command excludes raw secret operation from output',lambda:check('SYNTHETIC_SECRET' not in json.dumps(c.command('SYNTHETIC_SECRET'))))
 run('explicit prepare retains OFF ArmedDisabled',lambda:check(c.command('prepare',envelope=source[0])['after']['gate_state']=='ArmedDisabled'))
 identity=c.snapshot['runtime_instance']; generation=c.snapshot['sender']['source_generation']
 run('temporary no-data status same source and runtime',lambda:check(c.command('status')['after']['sender']['source_generation']==generation and c.snapshot['runtime_instance']==identity))
 run('continuous trusted pose admitted with no publication',lambda:check(c.command('input',source_lease=c.snapshot['source_lease'],source_generation=generation,envelope=source[1])['result']=='pose' and c.snapshot['sender']['queue_depth']==0 and c.snapshot['sender']['local_submission_record'] is None))
 oldlease=c.snapshot['source_lease']
 run('fresh reconnect exposed through production API',lambda:check(c.command('source-reconnect')['after']['sender']['source_generation']>generation and not c.snapshot['sender']['owner_approved']))
 run('late old generation machine rejection',lambda:check(c.command('input',source_lease=oldlease,source_generation=generation,envelope=source[1])['result']=='stale-source'))
 run('stale control session rejected without state change',lambda:check(c.command('owner-revoke',control_session='retired')['result']=='stale-control-session' and not c.snapshot['owner_revoked']))
 response=c.command('transport-reconnect')
 run('paired fault evidence exact instance and operation',lambda:check(save_fault_pair(response,p/'evidence')['operation_id']==response['operation_id']))
 run('source EOF explicit retains process',lambda:check(c.command('source-eof')['result']=='producer-eof' and c.process.poll() is None))
 run('shutdown structured terminal quiescence',lambda:check(c.command('shutdown')['after']['shutdown']))
finally: c.close()
c,p=client('sentinel')
try:
 c.command('prepare',envelope=source[0]); (p/'control/EMERGENCY_STOP').write_text('software-only stop')
 with concurrent.futures.ThreadPoolExecutor() as pool: response=pool.submit(c.read).result(timeout=10)
 run('independent sentinel works during no-data',lambda:check(response['after']['shutdown'] and response['after']['emergency_revoked'] and response['after']['sender']['queue_depth']==0))
finally: c.close()
c,p=client('pipe-eof'); c.command('prepare',envelope=source[0]); c.process.stdin.close(); response=c.read(); c.process.wait(timeout=10)
run('control pipe EOF producer shutdown observable',lambda:check(response['event']=='control-pipe-eof' and response['snapshot']['shutdown']))
print(f'PASS {cases} local control host cases; no activate request, production remains OFF, no production contact')
