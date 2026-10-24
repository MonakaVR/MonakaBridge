"""Package native measurement bytes for operator review. Adoption stays disabled.

This tool does not calculate transforms/residuals, certify physical accuracy,
refresh old timestamps, change configuration, or approve output.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,math,shutil
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def prepare(a):
    if a.output.exists():raise ValueError('A new output directory is required')
    native=read(a.result);pre=read(a.preimage);candidate=read(a.candidate);discovery=read(a.discovery);w=discovery.get('witness',discovery);policy=read(a.policy)
    members=[b for b in candidate['mappings'] if b['source_id']==w['source'] and b['device_id']==w['device']]
    if len(members)!=1:raise ValueError('Exact evidence target is missing/ambiguous')
    b=members[0]
    if native['mode']!='trajectory' or native['config_sha256']!=sha(a.preimage):raise ValueError('Native mode/preimage mismatch')
    if candidate['mapping_revision']!=pre['mapping_revision']+1 or b['input_revision']!=w['input_revision'] or b['input_space']!=w['input_space']:raise ValueError('Saved candidate identity/revision mismatch')
    if b['world']!={'rotation':native['world']['rotation_xyzw'],'translation':native['world']['translation_m']}:raise ValueError('Native World differs from saved candidate')
    if native.get('raw_sha256',sha(a.raw))!=sha(a.raw):raise ValueError('Native replay raw hash mismatch')
    for row in [native,native['all_residuals']]:
        for k in ['rms_position','rms_orientation','max_position','max_orientation']:
            if not isinstance(row[k],(int,float)) or not math.isfinite(row[k]) or row[k]<0:raise ValueError('Native residual missing/nonfinite: '+k)
    if not 0<native['inliers']<=native['pairs']:raise ValueError('Native counts invalid')
    def stamp(value):
        date=datetime.datetime.fromisoformat(value.replace('Z','+00:00'))
        if date.tzinfo is None:raise ValueError('Original timestamp requires UTC offset')
        return int(date.timestamp()*1000)
    measured=stamp(a.measured_at)
    expires=None if a.expires_at is None else stamp(a.expires_at)
    semantic=policy.get('schema')=='monaka.physical-criteria.v2'
    if semantic and ('quality' not in native or 'measurement_options' not in native):raise ValueError('Native quality/options required; replay with the current Native tool')
    if expires is not None and expires<=measured:raise ValueError('Expiry must follow original measurement time')
    a.output.mkdir(parents=True)
    def pin(path,name):
        shutil.copyfile(path,a.output/name);return dict(file=name,sha256=sha(a.output/name))
    p=pin(a.policy,'policy.json');prepin=pin(a.preimage,'preimage.json');r=pin(a.result,'native-result.json');raw=pin(a.raw,'raw.json')
    e=dict(schema='monaka.physical-evidence.v1',id=a.evidence_id,evidence_kind=a.capture_kind,criteria_id=policy['id'],criteria_version=policy['version'],criteria_sha256=p['sha256'],
        source=b['source_id'],device=b['device_id'],input_space=b['input_space'],input_revision=b['input_revision'],world_revision=b['world_revision'],mapping_revision=candidate['mapping_revision'],world=native['world'],
        measurement_result_id=native['result_id'],config_preimage=prepin,config_preimage_sha256=prepin['sha256'],config_candidate_sha256=sha(a.candidate),native_result=r,raw=raw,
        source_session=w['session'],source_clock=w['clock'],reference_serial=w['reference_serial'],reference_standing=w['standing_witness'],reference_generation=w['reference_generation'],measurement_mode='trajectory',
        measured_at_utc_ms=measured,expires_at_utc_ms=expires,tool_sha256=sha(a.tool),build_identity=a.build_identity)
    if semantic:
        e['schema']='monaka.physical-evidence.v2';e.pop('expires_at_utc_ms');e['lifetime']={'mode':'semantic'}
        if expires is not None:e['lifetime']['not_after_utc_ms']=expires
        identity=getattr(a,'semantic_identity',None)
        e['semantic_identity']=read(identity) if identity else dict(space_generation=None,attachment_relation=None,input_convention=w['convention'],reference_role=w.get('reference_role','left'))
    def save(name,value):(a.output/name).write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    save('evidence.json',e)
    save('adoption.json',dict(schema='monaka.output-adoption.v1',enabled=False,id=a.evidence_id,policy=p,evidence=[dict(file='evidence.json',sha256=sha(a.output/'evidence.json'))]))
    save('PREPARATION_RECEIPT.json',dict(status='PREPARED_NOT_ADOPTED',capture_kind=a.capture_kind,native_metrics_copied=True,criteria_adopted=False,configuration_changed=False,output_approved=False,source_files={str(v.resolve()):sha(v) for v in [a.raw,a.result,a.preimage,a.candidate,a.discovery,a.policy,a.tool]}))
    return dict(status='PREPARED_NOT_ADOPTED',output=str(a.output),expiry_requires_operator_policy=expires is None and not semantic,semantic_declaration_required=semantic,live_witness='NOT_ACQUIRED')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['raw','result','preimage','candidate','discovery','policy','tool','output']:p.add_argument('--'+name,type=Path,required=True)
    for name in ['build-identity','evidence-id','measured-at']:p.add_argument('--'+name,required=True)
    p.add_argument('--capture-kind',choices=['physical_capture','test_capture'],required=True);p.add_argument('--expires-at')
    p.add_argument('--semantic-identity',type=Path,help='Reviewed saved space-generation/attachment identity; absent stays UNKNOWN')
    print(json.dumps(prepare(p.parse_args()),indent=2))
if __name__=='__main__':main()
