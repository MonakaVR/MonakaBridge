"""Software only: actual current-user local pipe, production converter and A2 host.
Raw deterministic source data is test-only; no ALVR/SteamVR/device is started.
Optionally replay frames captured by the Rust native accepted-evidence test.
"""
from pathlib import Path
import argparse, copy, ctypes, json, queue, subprocess, sys, threading, time, uuid
from ctypes import wintypes as W

K=ctypes.WinDLL('kernel32',use_last_error=True)
K.CreateFileW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,W.LPVOID,W.DWORD,W.DWORD,W.HANDLE]
K.CreateFileW.restype=W.HANDLE
K.WriteFile.argtypes=[W.HANDLE,W.LPCVOID,W.DWORD,ctypes.POINTER(W.DWORD),W.LPVOID]
K.CloseHandle.argtypes=[W.HANDLE]
K.WaitNamedPipeW.argtypes=[W.LPCWSTR,W.DWORD]
checks=[]
def check(name,condition):
    assert condition,name
    checks.append(name)
def raw(event='sample',obs=0,session='1'*32):
    j={'schema':'alvr-openxr-authority-export-1','event':event,'export_session':session,
       'export_time_ns':'1','export_reconnect_count':1,'export_drop_count':0,'export_reject_count':0,'last_stream_failure':None}
    if event=='sample':
        j.update(source_token='7'*32,observation_id=str(obs),reference_generation='0',source_locate_time_ns=str(obs),
                 source_pose_bits=[0,1067450368,3225419776,0,0,0,1065353216],validity_mask=63)
    return j
class Process:
    def __init__(self,command):
        self.p=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
            text=True,encoding='utf-8',creationflags=subprocess.CREATE_NO_WINDOW)
        self.q=queue.Queue(64);self.rows=[]
        def pump():
            for line in self.p.stdout:
                self.q.put(json.loads(line),timeout=10)
        self.thread=threading.Thread(target=pump,daemon=True);self.thread.start()
    def read(self):
        try:r=self.q.get(timeout=10)
        except queue.Empty:raise AssertionError(('host timeout',self.p.poll(),self.rows))
        self.rows.append(r);return r
    def event(self,name):
        for _ in range(32):
            r=self.read()
            if r.get('event')==name:return r
        raise AssertionError('event absent '+name)
    def command(self,operation,**args):
        self.p.stdin.write(json.dumps({'operation':operation,**args})+'\n');self.p.stdin.flush()
    def close(self):
        if self.p.poll() is None:
            self.p.stdin.close()
            try:self.p.wait(timeout=10)
            except subprocess.TimeoutExpired:self.p.kill();self.p.wait(timeout=5)
        self.thread.join(timeout=5)
class Pipe:
    def __init__(self,name):
        path='\\\\.\\pipe\\'+name
        self.h=K.CreateFileW(path,0x40000000,0,None,3,0,None)
        if self.h==ctypes.c_void_p(-1).value and ctypes.get_last_error()==231:
            assert K.WaitNamedPipeW(path,5000),('local pipe readiness',ctypes.get_last_error())
            self.h=K.CreateFileW(path,0x40000000,0,None,3,0,None)
        assert self.h not in (None,ctypes.c_void_p(-1).value),('local pipe client',ctypes.get_last_error())
    def write(self,value):
        b=json.dumps(value,separators=(',',':')).encode() if isinstance(value,dict) else value
        n=W.DWORD();assert K.WriteFile(self.h,b,len(b),ctypes.byref(n),None) and n.value==len(b)
    def close(self):
        if self.h:K.CloseHandle(self.h);self.h=None

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--producer',required=True);ap.add_argument('--runtime',required=True)
    ap.add_argument('--work',required=True);ap.add_argument('--alvr-frames');args=ap.parse_args()
    root=Path(__file__).resolve().parents[1];out=Path(args.work).absolute()/str(uuid.uuid4());out.mkdir(parents=True)
    config=json.loads((root/'deployment/trusted-live-source.inactive.json').read_text())
    config.update(source_publisher_id='source-publisher-A',source_id='software-test-head',source_space_id='software-test-stage',pipe_name='monaka-software-'+uuid.uuid4().hex)
    path=out/'producer.json';path.write_text(json.dumps(config))
    value=json.loads(subprocess.check_output([args.producer],text=True))
    check('default producer inert',value['connected'] is False and value['lifecycle']=='DisabledByDefault')
    p=Process([args.producer,'--run',str(path)]);pipe=None
    try:
        check('explicit configured producer starts local pipe only',p.read()['event']=='created')
        pipe=Pipe(config['pipe_name']);pipe.write(raw('connected'));pipe.write(raw())
        authority=p.event('envelope')['envelope'];pose=p.event('envelope')['envelope']
        check('real pipe uses production source codec and canonical identity',authority['protocol']=='monaka.hmd_authority' and authority['publisher_id']=='source-publisher-A' and authority['type']=='source_authority' and pose['sequence']=='1')
        for i in range(1,4):
            pipe.write(raw(obs=i));e=p.event('envelope')['envelope']
            check('continuous original observation '+str(i),e['source']['observation_id']==str(i) and e['sequence']==str(i+1))
        for _ in range(8):
            p.command('snapshot');snapshot=p.event('snapshot')['snapshot']
            if snapshot['lifecycle']=='TemporaryNoData':break
        check('no-data observable without trust or sequence refresh',snapshot['lifecycle']=='TemporaryNoData' and snapshot['connected'] and snapshot['sequence_high_water']==4)
        p.command('restart-publication');p.event('disconnect')
        try:pipe.write(raw(obs=4));stale_rejected=False
        except AssertionError:stale_rejected=True
        check('restart fences still-open old client without first-instance collision',stale_rejected and p.p.poll() is None)
        pipe.close();pipe=Pipe(config['pipe_name']);pipe.write(raw('connected',session='5'*32));pipe.write(raw(obs=4,session='5'*32))
        fresh=p.event('envelope')['envelope'];p.event('envelope')
        check('explicit restart bootstrap contiguous and fresh',fresh['sequence']=='0' and fresh['session_id']!=authority['session_id'])
        pipe.write(raw('orderly-shutdown',session='5'*32));terminal=p.event('orderly-shutdown')
        check('source orderly shutdown distinct from process EOF',terminal['snapshot']['lifecycle']=='OrderlyShutdown' and p.p.poll() is None)
        pipe.close();pipe=Pipe(config['pipe_name']);pipe.write(raw('connected',session='2'*32));pipe.write(raw(obs=5,session='2'*32))
        next_authority=p.event('envelope')['envelope'];p.event('envelope')
        check('real reconnect fresh session stable publisher',next_authority['session_id']!=authority['session_id'] and next_authority['publisher_id']==authority['publisher_id'] and next_authority['sequence']=='0')
        bad=raw(obs=5,session='2'*32);bad['source_pose_bits'][0]=1
        pipe.write(bad);terminal=p.event('stream-failure')
        check('conflicting observation emits explicit failure and closes',terminal['snapshot']['connected'] is False and terminal['snapshot']['last_stream_failure']=='StreamFailure')
        pipe.close();pipe=None
        p.command('shutdown');check('explicit converter shutdown',p.event('orderly-shutdown')['snapshot']['lifecycle']=='OrderlyShutdown')
        p.p.wait(timeout=5);check('converter clean exit',p.p.returncode==0)
    finally:
        if pipe:pipe.close()
        p.close()
    # Real A2 control host, OFF config, durable software-only path. No activate command.
    runtime=json.loads((root/'deployment/production.activation.json').read_text())
    runtime.update(endpoint='udp://192.0.2.10:30111',owner_id='software-test-owner',world_space_id='software-test-world',source_publisher_id=config['source_publisher_id'],revision_path=str(out/'world.rev'))
    rp=out/'runtime.json';rp.write_text(json.dumps(runtime))
    cmd=[sys.executable,str(root/'scripts/trusted_live_source_adapter.py'),'--run','--producer',args.producer,'--producer-config',str(path),'--runtime',args.runtime,'--runtime-config',str(rp),'--emergency-stop-file',str(out/'STOP')]
    a=Process(cmd);pipe=None
    try:
        a.event('created');a.command('prepare');a.event('disconnect')
        pipe=Pipe(config['pipe_name']);pipe.write(raw('connected',session='3'*32));pipe.write(raw(session='3'*32))
        prepared=a.event('envelope');live=a.event('envelope')
        check('actual adapter explicit prepare exact durable receipt OFF',prepared['producer']['last_successful_a2_handoff']['result']=='armed-disabled' and prepared['runtime']['store']['receipt_matches_current_runtime'] and not prepared['runtime']['sender']['enabled'])
        check('actual continuous A2 handoff',live['producer']['last_successful_a2_handoff']['result']=='pose' and live['producer']['last_successful_a2_handoff']['sequence']=='1')
        before=live['runtime'];pipe.write(raw('disconnect',session='3'*32));a.event('disconnect');pipe.close()
        pipe=Pipe(config['pipe_name']);pipe.write(raw('connected',session='4'*32));pipe.write(raw(obs=1,session='4'*32))
        reconnect=a.event('envelope');a.event('envelope');after=reconnect['runtime']
        check('actual adapter reconnect fresh callback no implicit owner/publication',after['source_lease']!=before['source_lease'] and after['sender']['source_generation']>before['sender']['source_generation'] and not after['sender']['owner_approved'] and not after['sender']['enabled'] and not reconnect['producer']['a2_source_authorized'])
        check('durable state retained across source reconnect',after['store']['latest_durable_revision']==before['store']['latest_durable_revision'])
        a.command('source-authorize');a.event('disconnect');pipe.close()
        pipe=Pipe(config['pipe_name']);pipe.write(raw('connected',session='6'*32));pipe.write(raw(obs=2,session='6'*32))
        authorized=a.event('envelope');continued=a.event('envelope')
        check('actual explicit reauthorization durable successor remains OFF',authorized['producer']['last_successful_a2_handoff']['result']=='source-authorized-armed-disabled' and authorized['runtime']['sender']['owner_approved'] and not authorized['runtime']['sender']['enabled'] and authorized['runtime']['store']['latest_durable_revision']['world']>before['store']['latest_durable_revision']['world'])
        check('actual continuous stream resumes after explicit source approval',continued['producer']['last_successful_a2_handoff']['result']=='pose' and continued['producer']['a2_source_authorized'])
        a.command('shutdown');a.p.wait(timeout=10);check('actual adapter clean shutdown',a.p.returncode==0)
    finally:
        if pipe:pipe.close()
        a.close()
        (out/'adapter-events.json').write_text(json.dumps(a.rows,indent=2)+'\n')
    if args.alvr_frames:
        rows=[json.loads(x) for x in Path(args.alvr_frames).read_text().splitlines()]
        p=Process([args.producer,'--run',str(path)]);pipe=None
        try:
            p.event('created');pipe=Pipe(config['pipe_name']);pipe.write(rows[0]);pipe.write(rows[1])
            p.event('envelope');e=p.event('envelope')['envelope']
            check('Rust accepted tracking exporter to real converter bit-exact',e['position']==[0.0,1.25,-3.0] and e['source']['source_space']['source_authority_session_epoch']=='07'*16 and e['validity']['position_valid'])
            pipe.write(rows[2]);p.event('disconnect');pipe.close();pipe=None;p.command('shutdown');p.event('orderly-shutdown')
        finally:
            if pipe:pipe.close()
            p.close()
    (out/'results.json').write_text(json.dumps({'result':'PASS','cases':len(checks),'names':checks,'real_HIL':False,'production_activation':False},indent=2)+'\n')
    print('PASS actual live-source host',len(checks),'cases; output',out)
if __name__=='__main__':main()
