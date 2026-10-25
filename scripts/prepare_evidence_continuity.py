"""Prepare an explicit local continuity declaration; never install/adopt it.

P0-B owns coordinate revision/generation. Use independently verified generation,
not a process/session number. Native re-acquires liveness at every approval gate.
"""
from pathlib import Path
import argparse,json,hashlib
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def prepare(a):
    if a.output.exists():raise ValueError('New review output required')
    e=read(a.evidence);policy=read(a.policy);d=read(a.discovery);w=d.get('witness',d)
    if e['schema']!='monaka.physical-evidence.v2' or policy['schema']!='monaka.physical-criteria.v2':raise ValueError('Semantic schema v2 required')
    for k,field in [('source','source'),('device','device'),('input_space','input_space'),('input_revision','input_revision')]:
        if e[k]!=w[field]:raise ValueError('Current discovery differs from saved identity: '+k)
    if sha(a.config)!=e['config_candidate_sha256'] or sha(a.policy)!=e['criteria_sha256']:raise ValueError('Exact config/policy differs from evidence')
    if not a.operator.strip() or not a.space_generation.strip() or not a.attachment_relation.strip():raise ValueError('Explicit operator/generation/attachment identity required')
    out=dict(schema='monaka.evidence-continuity.v1',operator=a.operator,continuity=a.continuity,source=w['source'],device=w['device'],input_space=w['input_space'],input_revision=w['input_revision'],
        current_session=w['session'],current_clock=w['clock'],space_generation=a.space_generation,attachment_relation=a.attachment_relation,attachment_unchanged=a.attachment_unchanged,
        config_sha256=sha(a.config),policy_sha256=sha(a.policy),evidence_sha256=sha(a.evidence),reference_serial=w['reference_serial'],reference_role=w.get('reference_role','left'),reference_standing=w['standing_witness'],reference_generation=w['reference_generation'])
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8')
    return dict(status='PREPARED_NOT_ADOPTED',path=str(a.output),sha256=sha(a.output),native_current_witness='REQUIRED_AT_REVIEW_PREPARE_COMMIT',installed=False)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for k in ['evidence','policy','discovery','config','output']:p.add_argument('--'+k,type=Path,required=True)
    for k in ['operator','space-generation','attachment-relation']:p.add_argument('--'+k,required=True)
    p.add_argument('--continuity',choices=['SAME','UNKNOWN','CHANGED'],required=True);p.add_argument('--attachment-unchanged',action='store_true')
    print(json.dumps(prepare(p.parse_args()),indent=2))
