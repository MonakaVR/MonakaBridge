"""Immutable 5AI-S packaging/integrity only; never launches tracking or activation."""
from pathlib import Path
import argparse, hashlib, json, shutil, struct, subprocess, zipfile
from common_world_artifact import build_input, pe_imports

ROOT=Path(__file__).resolve().parents[1]
def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(root,*args):return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()
def dll_imports(path):
    b=Path(path).read_bytes();assert b[:2]==b'MZ';pe=struct.unpack_from('<I',b,60)[0]
    assert b[pe:pe+4]==b'PE\0\0' and struct.unpack_from('<H',b,pe+4)[0]==0x8664
    opt=pe+24;size=struct.unpack_from('<H',b,pe+20)[0];sections=[]
    for i in range(struct.unpack_from('<H',b,pe+6)[0]):
        n=opt+size+40*i;s,va,rs,ra=struct.unpack_from('<IIII',b,n+8);sections.append((va,max(s,rs),ra))
    def offset(rva):
        for va,s,ra in sections:
            if va<=rva<va+s:return ra+rva-va
        raise ValueError('PE RVA')
    rva=struct.unpack_from('<I',b,opt+120)[0];result=[]
    if rva:
        at=offset(rva)
        while any(b[at:at+20]):
            n=offset(struct.unpack_from('<I',b,at+12)[0]);result.append(b[n:b.index(b'\0',n)].decode().lower());at+=20
    return sorted(result)
def validate(folder):
    folder=Path(folder).resolve(strict=True);m=json.loads((folder/'producer-manifest.json').read_text())
    assert m['schema']=='monaka-trusted-live-source-artifact-1' and m['default_enabled'] is False
    assert (folder/'producer-manifest.sha256').read_text().strip()==sha(folder/'producer-manifest.json')
    names=set()
    for row in m['files']:
        name=row['path'];p=folder/name
        assert name not in names and name and ':' not in name and '\\' not in name and p.resolve().is_relative_to(folder)
        assert not any(x.is_symlink() or x.is_junction() for x in [p,*p.parents])
        assert p.stat().st_size==row['size'] and sha(p)==row['sha256'];names.add(name)
    assert {p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file()}==names|{'producer-manifest.json','producer-manifest.sha256'}
    required={'bin/monaka_trusted_live_source_producer.exe','bin/monaka_common_world_hil_runtime.exe','scripts/trusted_live_source_adapter.py','templates/trusted-live-source.inactive.json','alvr/bin/win64/driver_alvr_server.dll'}
    assert required<=names
    c=json.loads((folder/'templates/trusted-live-source.inactive.json').read_text())
    assert c['enabled'] is False and not c['source_publisher_id'] and not c['pipe_name']
    assert json.loads((folder/'templates/production.activation.json').read_text())['enabled'] is False
    return m
def package(args):
    alvr=Path(args.alvr_source).resolve();build=Path(args.build).resolve();out=Path(args.output).absolute()
    clean=not git(ROOT,'status','--porcelain') and not git(alvr,'status','--porcelain')
    assert clean or args.candidate,'final packaging requires both phase trees clean'
    assert not out.exists(),'fresh output only'
    bridge_commit=git(ROOT,'rev-parse','HEAD');alvr_commit=git(alvr,'rev-parse','HEAD')
    binaries=[build/'Release/monaka_trusted_live_source_producer.exe',build/'Release/monaka_common_world_hil_runtime.exe']
    for b in binaries:assert subprocess.check_output([str(b),'--build-identity'],text=True).strip()==bridge_commit
    source_hash=build_input();assert source_hash in (build/'deployment-generated/common_world_build_identity.hpp').read_text()
    driver_bytes=(alvr/'target/release/alvr_server_openvr.dll').read_bytes()
    assert alvr_commit.encode() in driver_bytes and b'alvr-openxr-authority-export-1' in driver_bytes,'ALVR driver must contain current source commit and real exporter'
    inputs=sorted(set(git(alvr,'ls-files').splitlines())|set(git(alvr,'ls-files','--others','--exclude-standard').splitlines()))
    ah=hashlib.sha256()
    for name in inputs:
        p=alvr/name
        if p.is_file():ah.update(name.encode()+b'\0'+bytes.fromhex(sha(p)))
        else:assert name=='openvr','unexpected missing ALVR source'
    sources={
        'bin/monaka_trusted_live_source_producer.exe':binaries[0],
        'bin/monaka_common_world_hil_runtime.exe':binaries[1],
        'scripts/trusted_live_source_adapter.py':ROOT/'scripts/trusted_live_source_adapter.py',
        'templates/trusted-live-source.inactive.json':ROOT/'deployment/trusted-live-source.inactive.json',
        'templates/production.activation.json':ROOT/'deployment/production.activation.json',
        'docs/trusted-live-source-a2-adapter-v2.1.md':ROOT/'docs/trusted-live-source-a2-adapter-v2.1.md',
        'alvr/bin/win64/driver_alvr_server.dll':alvr/'target/release/alvr_server_openvr.dll',
        'alvr/bin/win64/libvpl.dll':alvr/'deps/windows/libvpl/alvr_build/bin/libvpl.dll',
        'alvr/bin/win64/openvr_api.dll':alvr/'openvr/bin/win64/openvr_api.dll',
        'alvr/driver.vrdrivermanifest':alvr/'alvr/xtask/resources/driver.vrdrivermanifest',
        'licenses/ALVR-LICENSE':alvr/'LICENSE',
        'licenses/Valve-LICENSE':alvr/'alvr/server_openvr/LICENSE-Valve',
        'licenses/oneVPL-license':alvr/'deps/windows/libvpl/alvr_build/share/vpl/licensing/license.txt',
        'licenses/protocol-NOTICE':ROOT/'third_party/monaka-protocol-v22/NOTICE',
        'licenses/nlohmann-LICENSE.MIT':ROOT/'third_party/monaka-protocol-v22/cpp/third_party/nlohmann/LICENSE.MIT'}
    # oneVPL's existing local runtime includes its app-local MSVC redistributables.
    # Copy them into this immutable bundle; never install a system runtime.
    for p in (alvr/'deps/windows/libvpl/alvr_build/bin').glob('*.dll'):
        sources['alvr/bin/win64/'+p.name]=p
    for name,p in sources.items():
        dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
    imports={p.name:sorted(pe_imports(p)) for p in binaries}
    m={'schema':'monaka-trusted-live-source-artifact-1','default_enabled':False,'candidate':not clean,
       'bridge':{'repository':git(ROOT,'remote','get-url','origin'),'branch':git(ROOT,'branch','--show-current'),'commit':bridge_commit,'build_input_sha256':source_hash},
       'alvr':{'repository':git(alvr,'remote','get-url','origin'),'branch':git(alvr,'branch','--show-current'),'commit':alvr_commit,'build_input_sha256':ah.hexdigest(),'openvr_commit':git(alvr/'openvr','rev-parse','HEAD')},
       'configuration':'Release; native Windows x64; MSVC static CRT and Rust +crt-static',
       'runtime':'Windows x64; Python 3.13+ adapter; matching ALVR OpenXR authority client and SteamVR/OpenVR externally supplied',
       'source_type':'alvr-openxr-view-in-stage','input':'current-user remote-rejecting message pipe -> production wire2.1 source envelope -> inherited A2 control JSONL',
       'publisher_identity':'operator-registered external source_publisher_id config; blank inactive template; mapped by unchanged wire2.2 source_publisher_id path',
       'system_imports':imports,'alvr_pe_imports':{p.name:dll_imports(p) for n,p in sources.items() if n.startswith('alvr/') and p.suffix=='.dll'},
       'mutable_data':'external configs/state/logs/reports; no installation/launch during package',
       'files':[{'path':n,'size':(out/n).stat().st_size,'sha256':sha(out/n)} for n in sorted(sources)]}
    (out/'producer-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    (out/'producer-manifest.sha256').write_text(sha(out/'producer-manifest.json')+'\n')
    validate(out)
    archive=out.with_suffix('.zip');assert not archive.exists()
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():z.write(p,p.relative_to(out).as_posix())
    print(json.dumps({'result':'PASS','artifact':str(out),'archive':str(archive),'archive_sha256':sha(archive),'manifest_sha256':sha(out/'producer-manifest.json'),'template_sha256':sha(out/'templates/trusted-live-source.inactive.json'),'candidate':not clean},indent=2))
def main():
    ap=argparse.ArgumentParser(description=__doc__);sub=ap.add_subparsers(dest='command',required=True)
    p=sub.add_parser('package');p.add_argument('--build',required=True);p.add_argument('--alvr-source',required=True);p.add_argument('--output',required=True);p.add_argument('--candidate',action='store_true')
    p=sub.add_parser('verify');p.add_argument('artifact');args=ap.parse_args()
    if args.command=='package':package(args)
    else:
        m=validate(args.artifact);print(json.dumps({'result':'PASS','files':len(m['files']),'candidate':m['candidate'],'default_enabled':False}))
if __name__=='__main__':main()
