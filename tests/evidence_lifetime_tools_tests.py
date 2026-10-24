from pathlib import Path
import importlib.util,json,tempfile,copy,argparse
ROOT=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'scripts'/(name+'.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
p=module('hil_preflight');prepare=module('prepare_output_evidence');continuity=module('prepare_evidence_continuity')
def fails(fn,reason):
    try:fn()
    except ValueError as e:assert str(e)==reason,(str(e),reason)
    else:raise AssertionError('Expected '+reason)
for value in [{},{'not_after_utc_ms':None},{'not_after_utc_ms':101}]:assert p.time_contract({'lifetime':{'mode':'semantic',**value}},100,True)==value.get('not_after_utc_ms')
for value in ['tomorrow',True,1.5,-1,9223372036854775808]:fails(lambda:p.time_contract({'lifetime':{'mode':'semantic','not_after_utc_ms':value}},100,True),'APPROVAL_INTEGER_INVALID')
for value in ['forever',None,True,5]:fails(lambda:p.time_contract({'lifetime':{'mode':value}},100,True),'APPROVAL_LIFETIME_MODE_INVALID')
fails(lambda:p.time_contract({'lifetime':{'mode':'semantic','not_after_utc_ms':100}},100,True),'APPROVAL_TIME_POLICY_EXPIRED')
fails(lambda:p.time_contract({'lifetime':{'mode':'semantic','not_before_utc_ms':101}},100,True),'APPROVAL_TIME_POLICY_NOT_YET_VALID')
fails(lambda:p.time_contract({'lifetime':{'mode':'semantic'},'expires_at_utc_ms':None},100,True),'APPROVAL_MIXED_LIFETIME_SCHEMA')
fails(lambda:p.time_contract({'measured_at_utc_ms':1,'expires_at_utc_ms':None},100,False,True),'APPROVAL_INTEGER_INVALID')
assert p.time_contract({'valid_from_utc_ms':0,'valid_until_utc_ms':101},100,False)==101
(ROOT/'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=ROOT/'build') as directory:
    root=Path(directory)
    def write(name,value):
        f=root/name;f.write_text(json.dumps(value),encoding='utf-8');return f
    raw=write('raw.json',{'source':[],'reference':[]});pre=write('pre.json',{'mapping_revision':1});world={'rotation_xyzw':[0,0,0,1],'translation_m':[0,0,0]}
    candidate=write('config.json',{'mapping_revision':2,'mappings':[{'source_id':'source','device_id':'device','input_space':'space','input_revision':3,'world_revision':4,'world':{'rotation':world['rotation_xyzw'],'translation':world['translation_m']}}]})
    result=write('result.json',{'mode':'trajectory','config_sha256':prepare.sha(pre),'world':world,'pairs':100,'inliers':100,'result_id':'TEST_ONLY','rms_position':0,'rms_orientation':0,'max_position':0,'max_orientation':0,'all_residuals':{'rms_position':0,'rms_orientation':0,'max_position':0,'max_orientation':0},'quality':{},'measurement_options':{}})
    discovery=write('discovery.json',{'source':'source','device':'device','input_space':'space','input_revision':3,'session':'new-process','clock':'new-clock','reference_serial':'reference','standing_witness':'standing','reference_generation':1,'convention':'test','reference_role':'left'})
    policy=write('policy.json',{'schema':'monaka.physical-criteria.v2','id':'TEST_ONLY','version':'1'})
    args=argparse.Namespace(raw=raw,result=result,preimage=pre,candidate=candidate,discovery=discovery,policy=policy,tool=Path(__file__),output=root/'prepared',measured_at='2026-10-23T14:32:03.144Z',expires_at=None,semantic_identity=None,evidence_id='TEST_ONLY',capture_kind='test_capture',build_identity='TEST_ONLY')
    before={f.name:f.read_bytes() for f in root.iterdir()};receipt=prepare.prepare(args)
    e=prepare.read(args.output/'evidence.json');assert e['measured_at_utc_ms']==1792765923144 and e['lifetime']=={'mode':'semantic'} and 'expires_at_utc_ms' not in e
    assert e['semantic_identity']['space_generation'] is None and e['semantic_identity']['attachment_relation'] is None
    assert not prepare.read(args.output/'adoption.json')['enabled'] and not receipt['expiry_requires_operator_policy']
    c=argparse.Namespace(evidence=args.output/'evidence.json',policy=policy,discovery=discovery,config=candidate,output=root/'current-declaration.json',operator='TEST_ONLY',space_generation='TEST_ONLY',attachment_relation='TEST_ONLY',attachment_unchanged=False,continuity='UNKNOWN')
    r=continuity.prepare(c);assert r['installed'] is False
    d=prepare.read(c.output);assert d['current_session']=='new-process' and d['continuity']=='UNKNOWN' and d['attachment_unchanged'] is False
    assert all((root/n).read_bytes()==b for n,b in before.items())
print('PASS semantic/legacy time boundaries and malformed values; preparation preserves bytes/date, UNKNOWN semantics and disabled adoption; no live witness fabricated')
