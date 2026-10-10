"""Deterministic software coordination; production adapter class, no real source/process."""
from pathlib import Path
import io
import json
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from trusted_live_source_adapter import ContinuousA2Adapter, BoundedReader, strict_json

cases=0
def check(name, body):
    global cases
    body()
    cases+=1
    print('PASS '+name)

class Rig:
    def __init__(self):
        self.calls=[]
        self.restarts=0
        self.generation=1
        self.accept=True
        self.adapter=ContinuousA2Adapter('source-publisher-A',self.request,self.restart)
    def request(self,op,envelope):
        self.calls.append((op,envelope))
        if op=='source-reconnect':self.generation+=1
        return {'accepted':self.accept,'result':op,'after':{'runtime_instance':'test-runtime','source_lease':'lease-'+str(self.generation),'sender':{'source_generation':self.generation}}}
    def restart(self):self.restarts+=1
    def envelope(self,seq=0,session='test-session-1',publisher='source-publisher-A'):
        return {'event':'envelope','envelope':{'publisher_id':publisher,'session_id':session,'type':'source_authority' if seq==0 else 'source_pose','sequence':str(seq)},
                'snapshot':{'build_commit':'test-commit','source_generation':self.generation,'source_session':session}}
    def prepare(self):
        self.adapter.operator('prepare')
        self.adapter.event({'event':'disconnect'})
        self.adapter.event(self.envelope())

def inert():
    r=Rig();r.adapter.event(r.envelope());r.adapter.event(r.envelope(1))
    assert not r.calls and not r.adapter.authorized and r.adapter.dropped==2
check('stream cannot implicitly prepare or authorize',inert)
def explicit():
    r=Rig();r.prepare();r.adapter.event(r.envelope(1))
    assert [c[0] for c in r.calls]==['prepare','input']
    assert r.adapter.last_handoff['publisher_id']=='source-publisher-A'
check('explicit prepare then continuous current input',explicit)
def restart_barrier():
    r=Rig();r.adapter.operator('prepare');r.adapter.event(r.envelope())
    assert not r.calls
    r.adapter.event({'event':'disconnect'});r.adapter.event(r.envelope(0,'test-session-2'))
    assert r.calls[0][0]=='prepare'
check('pre-restart queued bootstrap cannot gain authorization',restart_barrier)
def reconnect():
    r=Rig();r.prepare();r.adapter.event({'event':'disconnect'});r.adapter.event(r.envelope(0,'test-session-2'))
    assert [c[0] for c in r.calls]==['prepare','source-disconnect','source-reconnect']
    assert not r.adapter.authorized and r.adapter.last_handoff['session_id']=='test-session-1'
check('reconnect fresh A2 generation without owner authorization',reconnect)
def authorize():
    r=Rig();r.prepare();r.adapter.operator('source-authorize');r.adapter.event({'event':'disconnect'});r.adapter.event(r.envelope(0,'test-session-2'))
    assert r.calls[-1][0]=='source-authorize' and r.adapter.authorized
    assert r.adapter.last_handoff['source_generation']==2
check('explicit source authorize consumes only fresh bootstrap',authorize)
def mismatch():
    r=Rig()
    try:r.adapter.event(r.envelope(publisher='source-publisher-B'));assert False
    except ValueError:pass
    assert r.adapter.rejected==1 and not r.adapter.authorized
check('canonical publisher mismatch fail closed',mismatch)
def rejected():
    r=Rig();r.prepare();r.accept=False;r.adapter.event(r.envelope(1))
    assert not r.adapter.authorized and r.adapter.rejected==1 and r.restarts==2
check('A2 rejection closes source instead of implicit repair',rejected)
def no_data():
    r=Rig();r.prepare();old=list(r.calls);r.adapter.event({'event':'temporary-no-data'})
    assert r.calls==old and r.adapter.authorized
check('temporary no data preserves lifecycle',no_data)
def failure():
    r=Rig();r.prepare();r.adapter.event({'event':'stream-failure'})
    assert r.adapter.last_failure=='stream-failure' and not r.adapter.authorized
check('stream failure observable separately',failure)
def bounded():
    reader=BoundedReader(io.StringIO('{}\n{}\n{}\n'),2,32);reader.thread.join(5)
    assert not reader.thread.is_alive() and reader.failed.is_set() and reader.items.qsize()==2
check('bounded queue overflow fails without unbounded buffering',bounded)
def eof():
    reader=BoundedReader(io.StringIO('{}\n'),2,32);reader.thread.join(5)
    assert reader.eof.is_set() and not reader.failed.is_set()
check('orderly EOF distinct from failure',eof)
def framing():
    reader=BoundedReader(io.StringIO('{}'),2,32);reader.thread.join(5)
    assert reader.failed.is_set() and not reader.eof.is_set()
check('truncated stream is failure',framing)
def duplicates():
    try:strict_json('{"a":1,"a":2}');assert False
    except ValueError:pass
check('duplicate control fields reject',duplicates)
print(f'PASS continuous adapter {cases} cases')
