from pathlib import Path
import argparse,copy,json,subprocess,tempfile,time
p=argparse.ArgumentParser();p.add_argument('--tool',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1]
a.out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=a.out) as folder:
    d=Path(folder);config=json.loads((root/'tests/fixtures/world-approval/approved-groups.json').read_text(encoding='utf-8'));b=config['mappings'][0];config['policy']='steamvr'
    health=dict(snapshot_unix_ms=0,publisher_id=config['bridge_id'],publisher_session='00000000-0000-4000-8000-000000000003',mapping_revision=config['mapping_revision'],devices=[dict(source=b['source_id'],device=b['device_id'],space=b['input_space'],revision=b['input_revision'],convention=config['profiles'][b['profile']]['convention'],fresh=True,position_valid=True,orientation_valid=True,collision=False,input_session='00000000-0000-4000-8000-000000000001')])
    def case(name,edit=None,okay=False,model=None):
        c=copy.deepcopy(config);h=copy.deepcopy(health);h['snapshot_unix_ms']=int(time.time()*1000)
        if edit:edit(c,h)
        cp=d/'config.json';hp=d/'health.json';cp.write_text(json.dumps(c));hp.write_text(json.dumps(h));before=cp.read_bytes();out=d/name
        r=subprocess.run([str(a.tool.resolve()),str(cp),str(hp),b['source_id'],b['device_id'],str(out)]+([model] if model else []),capture_output=True,timeout=10)
        assert (r.returncode==0)==okay,(name,r.stderr)
        assert cp.read_bytes()==before
        if okay:
            v=json.loads((out/'selection.json').read_text());assert v['assignments'][0]['model_id']==(model or 'pico_motion_tracker');assert v['publisher_session']==h['publisher_session'] and v['assignments'][0]['input_session']==h['devices'][0]['input_session'];assert (out/'bridge-config.json').read_bytes()==before
    case('normal',okay=True)
    case('identification',okay=True,model='pico_tracker_rev3_identification')
    case('diagnostic',okay=True,model='pico_tracker_rev3_axis_debug')
    case('unknown-model',model='../outside')
    case('stale',lambda c,h:h.update(snapshot_unix_ms=1))
    case('config-revision',lambda c,h:h.update(mapping_revision=c['mapping_revision']+1))
    case('unknown-publisher',lambda c,h:h.update(publisher_id='foreign'))
    case('unapproved',lambda c,h:c['mappings'][0].update(space_approved=False))
    case('wrong-input',lambda c,h:h['devices'][0].update(revision=b['input_revision']+1))
    case('lost',lambda c,h:h['devices'][0].update(fresh=False))
    case('collision',lambda c,h:h['devices'][0].update(collision=True))
print('PASS native presentation preparation: exact snapshot and seven refusals; no registrations/runtime/config changes')
