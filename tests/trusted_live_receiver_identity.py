"""Actual unchanged R2 receiver + fresh real-producer/A2/Ordered Publisher wire fixtures.
Explicit software-only tuple, isolated 127.0.0.1 UDP, no deployment endpoint.
"""
from pathlib import Path
import argparse, copy, json, os, queue, socket, subprocess, threading, time, uuid

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--receiver-root',required=True);ap.add_argument('--fixtures',required=True);ap.add_argument('--work',required=True);args=ap.parse_args()
    root=Path(args.receiver_root).resolve();out=Path(args.work).resolve()/uuid.uuid4().hex;out.mkdir(parents=True)
    a=[json.loads(x) for x in (Path(args.fixtures)/'publisher-A.jsonl').read_text().splitlines()]
    b=[json.loads(x) for x in (Path(args.fixtures)/'publisher-B.jsonl').read_text().splitlines()]
    assert all(x['source_publisher_id']=='source-publisher-A' and x['version']=={'major':2,'minor':2} for x in a)
    assert all(x['source_publisher_id']=='source-publisher-B' for x in b)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
    config=json.loads((root/'deployment/receiver.inactive.json').read_text())
    config.update(bind_ipv4='127.0.0.1',udp_port=port,association_path=str(out/'association.bin'))
    path=out/'receiver.json';path.write_text(json.dumps(config))
    java=Path(os.environ.get('JAVA_HOME',r'C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot'))/'bin/java.exe'
    cp=os.pathsep.join([str(root/'receiver/build/classes/kotlin/main'),str(root/'receiver/build/resources/main'),*(root/'receiver/build/runtime-classpath.txt').read_text().splitlines()])
    # Isolate the packaged-host stderr assertion from diagnostic-only options set
    # on the test launcher. The old R2 regression keeps its original environment.
    host_env=dict(os.environ);host_env.pop('JAVA_TOOL_OPTIONS',None)
    p=subprocess.Popen([str(java),'-cp',cp,'dev.monaka.tracking.ReceiverMainKt','--control',str(path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',creationflags=subprocess.CREATE_NO_WINDOW,env=host_env)
    q=queue.Queue(16);rows=[];checks=[]
    def pump():
        for line in p.stdout:q.put(json.loads(line),timeout=10)
    thread=threading.Thread(target=pump,daemon=True);thread.start()
    def read():
        v=q.get(timeout=10);rows.append(v);return v
    def command(operation,**kw):
        p.stdin.write(json.dumps({'operation':operation,**kw})+'\n');p.stdin.flush();return read()
    def check(name,ok):assert ok,name;checks.append(name)
    def await_count(name,n):
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            s=command('status')['snapshot']
            if s['counters'][name]>=n:return s
        raise AssertionError('counter '+name)
    def state(s):return {k:s[k] for k in ('retained_association','retained_world','retained_mapping','sequence_high_water','world_current','mapping_current','pose_current')}
    try:
        check('actual receiver starts inactive',read()['snapshot']['listening'] is False)
        w=a[0];assoc={'owner_id':w['world']['owner_id'],'world_space_id':w['world']['coordinate_space']['id'],'source_publisher_id':w['source_publisher_id']}
        check('explicit association-to-A',command('associate',**assoc)['result']=='Associated')
        check('explicit ordered publisher session',command('authorize-session',publisher_id=w['publisher_id'],session_id=w['session_id'],clock_id=w['clock_id'])['result']=='SessionAuthorized')
        check('isolated production binder',command('start')['result']=='Receiving')
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
            def send(e):sender.sendto(json.dumps(e,separators=(',',':')).encode(),('127.0.0.1',port))
            send(w);s=await_count('accepted',1);check('fresh producer A decoded and applied A',s['last_accepted_packet']['result']=='world' and s['retained_association']==assoc)
            before=state(s);send(b[0]);s=await_count('rejected',1)
            check('actual producer B wire rejected by association-to-A',s['last_rejected_packet']['reason']=='SourcePublisherMismatch' and state(s)==before)
            foreign=copy.deepcopy(w);foreign['source_publisher_id']=b[0]['source_publisher_id'];send(foreign);s=await_count('rejected',2)
            check('otherwise byte-identical fields A B distinguishable',foreign!=w and {k:v for k,v in foreign.items() if k!='source_publisher_id'}=={k:v for k,v in w.items() if k!='source_publisher_id'} and s['last_rejected_packet']['reason']=='SourcePublisherMismatch' and state(s)==before)
            send(a[1]);await_count('accepted',2);send(a[2]);s=await_count('accepted',3)
            check('actual source pose through existing Core',s['pose_current'] and s['last_accepted_packet']['result']=='pose')
        check('normal receiver shutdown',command('shutdown')['snapshot']['shutdown']);p.stdin.close();p.wait(timeout=10)
        check('clean production host exit',p.returncode==0 and not p.stderr.read())
    finally:
        if p.poll() is None:p.kill();p.wait(timeout=5)
        thread.join(timeout=5);(out/'receiver-events.json').write_text(json.dumps(rows,indent=2)+'\n')
    (out/'result.json').write_text(json.dumps({'result':'PASS','checks':len(checks),'names':checks,'source_fixtures':str(Path(args.fixtures).resolve()),'real_HIL':False,'receiver_modified':False},indent=2)+'\n')
    print('PASS live producer identity actual R2 association',len(checks),'checks;',out)
if __name__=='__main__':main()
