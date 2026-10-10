"""Deterministic publisher-identity vectors, never modifies historical fixtures."""
import copy,json
from pathlib import Path
from fixtures_v21 import valid
ROOT=Path(__file__).resolve().parents[1]
def vectors():
    cases={}
    for n,x in valid().items():
        if x['protocol']!='monaka.common_world':continue
        x=copy.deepcopy(x);x['protocol']='monaka.common_world_identity';x['version']['minor']=2;x['source_publisher_id']='source-publisher-A'
        cases[n]=x
    cases['publisher-B']=copy.deepcopy(cases['common-hmd-pose']);cases['publisher-B']['source_publisher_id']='source-publisher-B'
    return cases
def generate():
    root=ROOT/'fixtures/v2.2';(root/'valid').mkdir(parents=True,exist_ok=True);(root/'invalid').mkdir(exist_ok=True)
    index=[]
    for n,x in vectors().items():
        name='valid/'+n+'.json';(root/name).write_text(json.dumps(x,indent=2,ensure_ascii=False)+'\n',encoding='utf-8',newline='\n');index.append({'file':name,'error':None})
    p=vectors()['common-hmd-pose']
    invalid={}
    for n,value,error in [('empty','', 'OutOfRange'),('blank','   ','OutOfRange'),('control','a\x01b','OutOfRange'),('oversize','x'*97,'OutOfRange'),('wrong-type',42,'InvalidType'),('null',None,'InvalidType')]:
        x=copy.deepcopy(p);x['source_publisher_id']=value;invalid[n]=(json.dumps(x).encode(),error)
    x=copy.deepcopy(p);del x['source_publisher_id'];invalid['missing']=(json.dumps(x).encode(),'MissingField')
    for v in [0,1,3,65535]:
        x=copy.deepcopy(p);x['version']['minor']=v;invalid['minor-'+str(v)]=(json.dumps(x).encode(),'UnsupportedVersion')
    raw=json.dumps(p).encode();invalid['truncated']=(raw[:raw.index(b'source_publisher_id')+25],'MalformedJson')
    invalid['invalid-utf8']=(raw.replace(b'source-publisher-A',b'\xc0\xaf'),'InvalidUtf8')
    invalid['malformed-escape']=(raw.replace(b'source-publisher-A',b'\\uZZZZ'),'MalformedJson')
    for n,(data,error) in invalid.items():
        name='invalid/'+n+'.json';(root/name).write_bytes(data);index.append({'file':name,'error':error})
    (root/'index.json').write_text(json.dumps(index,indent=2)+'\n',newline='\n')
if __name__=='__main__':generate()
