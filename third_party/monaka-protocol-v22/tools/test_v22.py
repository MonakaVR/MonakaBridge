"""Actual production C++ / JVM compatibility, field, size and frozen-vector gates."""
import argparse,copy,hashlib,json,os,subprocess
from pathlib import Path
from fixtures_v22 import vectors
R=Path(__file__).resolve().parents[1]
LEGACY='ea1e02c15c20693070ce69cfbc54941ee7ae8e7d'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,why):
    if not ok:raise RuntimeError(why)
def git(*a):return subprocess.check_output(['git',*a],cwd=R).decode().strip()
def commands(root):
    cp=os.pathsep.join(str(root/p) for p in ['jvm/build/classes/kotlin/test','jvm/build/libs/monaka-protocol-jvm-0.1.0.jar','jvm/libs/gson-2.11.0.jar','jvm/libs/kotlin-stdlib-2.3.10.jar'])
    return [[str(root/'build/cpp/Release/monaka_codec_runner_v2.exe')],['java','-Dfile.encoding=UTF-8','-cp',cp,'dev.monaka.protocol.v2.Runner']]
def run(c,p):return subprocess.check_output(c+[str(p)],encoding='utf-8').strip()
def main():
    p=argparse.ArgumentParser();p.add_argument('--old-v21',type=Path,required=True);a=p.parse_args()
    require(subprocess.check_output(['git','-C',str(a.old_v21),'rev-parse','HEAD']).decode().strip()==LEGACY,'old 2.1 decoder provenance')
    require(not subprocess.check_output(['git','-C',str(a.old_v21),'status','--porcelain']).strip(),'old 2.1 tree must remain clean')
    new,old=commands(R),commands(a.old_v21);scratch=R/'build/cross-v22';scratch.mkdir(parents=True,exist_ok=True)
    names=git('ls-tree','-r','--name-only',LEGACY,'fixtures/v2.1','docs/C2.1.md','schema/v2/trusted-hmd-authority.schema.json').splitlines()
    frozen={}
    for n in names:
        require((R/n).read_bytes()==subprocess.check_output(['git','show',LEGACY+':'+n],cwd=R),'changed 2.1 vector '+n);frozen[n]=sha(R/n)
    cross=0;rejections=[0,0];legacy_pass=[0,0];hashes={};cases=[]
    for row in json.loads((R/'fixtures/v2.2/index.json').read_text()):
        path=R/'fixtures/v2.2'/row['file'];outputs=[]
        for i,c in enumerate(new):
            out=run(c,path)
            if row['error']:require(out=='ERROR:'+row['error'],row['file']+': '+out)
            else:
                x=json.loads(path.read_text());require(json.loads(out)==x,'2.2 exact semantic roundtrip')
                require(run(c,path)==out,'nondeterministic encoding')
                tmp=scratch/f'cross-{i}.json';tmp.write_text(out,encoding='utf-8');require(json.loads(run(new[1-i],tmp))==x,'cross-language roundtrip');cross+=1
                require(run(old[i],tmp)=='ERROR:UnsupportedMessage','old decoder accepted new identity namespace');rejections[i]+=1
                hashes[row['file']+':'+str(i)]=hashlib.sha256(out.encode()).hexdigest()
            outputs.append(out)
        cases.append(row['file'])
    # ALL valid historical 2.1 canonical bytes compared with actual old codecs.
    for row in json.loads((R/'fixtures/v2.1/index.json').read_text()):
        if row['error']:continue
        for i in range(2):
            path=R/'fixtures/v2.1'/row['file'];out=run(new[i],path);require(out==run(old[i],path),'2.1 canonical bytes changed')
            require('source_publisher_id' not in json.loads(out),'legacy identity fabricated');legacy_pass[i]+=1
    x=vectors()['common-hmd-pose'];y=vectors()['publisher-B']
    require({k:v for k,v in x.items() if k!='source_publisher_id'}=={k:v for k,v in y.items() if k!='source_publisher_id'},'pair differs outside publisher')
    require(hashes['valid/common-hmd-pose.json:0']!=hashes['valid/publisher-B.json:0'],'publisher identities collapsed')
    sizes=[]
    for id in ['x'*96,'é'*48,'\\'*96,'Mixed.Case-Publisher']:
        z=copy.deepcopy(x);z['source_publisher_id']=id;tmp=scratch/'boundary.json';tmp.write_text(json.dumps(z),encoding='utf-8')
        for c in new:
            out=run(c,tmp);require(json.loads(out)==z,'canonical identity changed');sizes.append(len(out.encode()))
    # Legacy unknown optional fields remain ignored, never trusted identities.
    z=copy.deepcopy(x);z['protocol']='monaka.common_world';z['version']['minor']=1
    tmp=scratch/'legacy-spoof.json';tmp.write_text(json.dumps(z))
    for c in new:require('source_publisher_id' not in json.loads(run(c,tmp)),'legacy spoof supplied trusted identity')
    # Escaped IDs at every maximum width + worst finite binary64 / U63 widths.
    def maximum(x,key=''):
        if isinstance(x,dict):return {k:maximum(v,k) for k,v in x.items()}
        if isinstance(x,list):return x
        if key in ['publisher_id','source_publisher_id','owner_id','world_epoch','calibration_epoch','source_id','source_authority_session_epoch','source_space_id','source_time_domain_id','id']:return '\\'*96
        if key in ['sequence','timestamp_ns','sent_at_ns','observation_id','source_space_generation']:return '9223372036854775807'
        if key=='source_locate_time_ns':return '-9223372036854775808'
        if key in ['revision','mapping_revision']:return 4294967295
        return x
    z=maximum(x)
    for k in ['source_position','common_position']:z[k]=[1.7976931348623157e308,-1.7976931348623157e308,1.7976931348623157e308]
    for k in ['source_orientation','common_orientation']:z[k]=[0.5773502691896257,0.5773502691896257,0.5773502691896257,2.2250738585072014e-308]
    tmp=scratch/'maximum.json';tmp.write_text(json.dumps(z,separators=(',',':')));maxsizes=[]
    for c in new:
        out=run(c,tmp);require(json.loads(out)==z,'numeric / maximum fields changed');maxsizes.append(len(out.encode()));require(len(out.encode())<=4096,'size exceeds UDP bound')
    # Padding and malformed identity cannot bypass the whole-datagram limit.
    z['padding']='x'*4096;tmp.write_text(json.dumps(z))
    for c in new:require(run(c,tmp)=='ERROR:TooLarge','oversize accepted')
    sources={p.relative_to(R).as_posix():sha(p) for f in ['cpp/include','cpp/src','cpp/tests','jvm/src','schema','fixtures','docs','tools'] for p in (R/f).rglob('*') if p.is_file() and '__pycache__' not in p.parts}
    binaries={str(Path(new[0][0]).relative_to(R)).replace('\\','/'):sha(Path(new[0][0])),'jvm/build/libs/monaka-protocol-jvm-0.1.0.jar':sha(R/'jvm/build/libs/monaka-protocol-jvm-0.1.0.jar')}
    report={'status':'PASS','source_commit':git('rev-parse','HEAD'),'cases':len(cases)+8,'cross_language_directions':cross,'old_decoder_new_rejections':rejections,'new_decoder_old_passes':legacy_pass,'max_escaped_fields_encoded_bytes':maxsizes,'frozen_v21_sha256':frozen,'canonical_v22_sha256':hashes,'source_sha256':sources,'binary_sha256':binaries,'old_decoder_source':LEGACY}
    (R/'build/test-v22-results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['status','cases','cross_language_directions','old_decoder_new_rejections','new_decoder_old_passes','max_escaped_fields_encoded_bytes']}))
if __name__=='__main__':main()
