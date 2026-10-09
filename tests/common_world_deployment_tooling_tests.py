from pathlib import Path
import hashlib, json, subprocess, sys, time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from common_world_artifact import validate, digest
base=Path(sys.argv[1]).resolve()/str(time.monotonic_ns()); base.mkdir(parents=True)
cases=0
def check(ok):
    if not ok: raise AssertionError('deployment tooling expectation failed')
def artifact(name):
    root=base/name; root.mkdir()
    files={'bin/monaka_common_world_runtime.exe':b'inert test bytes', 'bin/monaka_common_world_preflight.exe':b'inert observer bytes',
           'templates/production.activation.json':(ROOT/'deployment/production.activation.json').read_bytes(),
           'templates/hil-evidence.json':(ROOT/'deployment/hil-evidence-template.json').read_bytes(), 'dependency-inventory.json':b'{}'}
    rows=[]
    for name,data in files.items():
        p=root/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(data)
        rows.append({'path':name,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()})
    m={'schema':'monaka-common-world-artifact-1','activation_default':False,'commit':'test-commit','branch':'test-only','build_input_sha256':'test-build','files':rows}
    save(root,m); return root
def save(root,m):
    (root/'artifact-manifest.json').write_text(json.dumps(m))
    (root/'artifact-manifest.sha256').write_text(digest(root/'artifact-manifest.json')+'\n')
def reject(fn):
    try: fn()
    except (ValueError,KeyError,OSError): return
    raise AssertionError('unsafe tooling input accepted')
def run(name,fn):
    global cases
    fn(); cases+=1; print('PASS '+name)
run('artifact integrity PASS',lambda:validate(artifact('valid')))
def altered():
    p=artifact('altered'); (p/'bin/monaka_common_world_runtime.exe').write_bytes(b'changed'); reject(lambda:validate(p))
run('altered binary rejected',altered)
def unknown():
    p=artifact('unknown'); (p/'world.rev').write_bytes(b'preserve'); reject(lambda:validate(p))
run('unmanifested state rejected',unknown)
def malformed():
    p=artifact('malformed'); (p/'artifact-manifest.json').write_text('{'); reject(lambda:validate(p))
run('corrupt manifest rejected',malformed)
def traversal():
    p=artifact('traversal'); m=json.loads((p/'artifact-manifest.json').read_text()); m['files'][0]['path']='../escape'; save(p,m); reject(lambda:validate(p))
run('manifest traversal rejected',traversal)
def duplicate():
    p=artifact('duplicate'); m=json.loads((p/'artifact-manifest.json').read_text()); m['files'].append(m['files'][0]); save(p,m); reject(lambda:validate(p))
run('duplicate manifest entry rejected',duplicate)
def rollback(apply):
    p=artifact('rollback-'+str(apply)); sibling=base/('state-'+str(apply)); sibling.mkdir(); rev=sibling/'world.rev'; rev.write_bytes(b'protected persistent state')
    args=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'scripts/rollback_common_world_artifact.ps1'),'-ArtifactPath',str(p)]
    if apply: args.append('-Apply')
    r=subprocess.run(args,capture_output=True,text=True)
    if r.returncode: raise AssertionError(r.stdout+'\n'+r.stderr)
    check(rev.read_bytes()==b'protected persistent state'); check(p.exists()!=apply)
run('rollback dry run preserves artifact/state',lambda:rollback(False))
run('rollback apply removes only artifact/state preserved',lambda:rollback(True))
def refuse_state():
    p=artifact('rollback-state'); rev=p/'world.rev'; rev.write_bytes(b'keep')
    r=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'scripts/rollback_common_world_artifact.ps1'),'-ArtifactPath',str(p),'-Apply'],capture_output=True,text=True)
    check(r.returncode!=0 and rev.read_bytes()==b'keep')
run('rollback refuses embedded persistent/unmanifested data',refuse_state)
def refuse_declared_state():
    p=artifact('rollback-declared-state'); rev=p/'state/world.rev'; rev.parent.mkdir(); rev.write_bytes(b'keep even if listed')
    m=json.loads((p/'artifact-manifest.json').read_text()); m['files'].append({'path':'state/world.rev','size':rev.stat().st_size,'sha256':digest(rev)}); save(p,m)
    reject(lambda:validate(p))
    r=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'scripts/rollback_common_world_artifact.ps1'),'-ArtifactPath',str(p),'-Apply'],capture_output=True,text=True)
    check(r.returncode!=0 and rev.read_bytes()==b'keep even if listed')
run('rollback refuses persistent data even when manifest lists it',refuse_declared_state)
def evidence():
    p=artifact('evidence'); dest=base/'hil.json'
    args=[sys.executable,str(ROOT/'scripts/new_common_world_hil_report.py'),'--artifact',str(p),'--output',str(dest)]
    r=subprocess.run(args,capture_output=True,text=True); check(r.returncode==0)
    m=json.loads(dest.read_text()); check(m['final_result']=='NOT_RUN' and m['activation_before']=='OFF' and m['emergency_revoke']=='NOT_RUN')
    before=dest.read_bytes(); r=subprocess.run(args,capture_output=True,text=True); check(r.returncode!=0 and dest.read_bytes()==before)
run('HIL evidence exclusive creation never claims execution',evidence)
print(f'PASS {cases} deployment tooling cases; no activation/deployment/network; persistent rollback protection')
