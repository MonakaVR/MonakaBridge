"""Package/verify an inactive standalone receiver; never starts its UDP listener."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[1]
JDK=Path(r'C:\Program Files\Microsoft\jdk-17.0.20.101-hotspot')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(*a):return subprocess.check_output(['git','-C',str(ROOT),*a],text=True).strip()
def validate(folder):
    folder=Path(folder).resolve(strict=True);manifest=json.loads((folder/'receiver-manifest.json').read_text())
    assert manifest['schema']=='monaka-common-world-receiver-artifact-1' and manifest['default_listening'] is False
    assert (folder/'receiver-manifest.sha256').read_text().strip()==sha(folder/'receiver-manifest.json')
    names=set()
    for row in manifest['files']:
        name=row['path'];p=folder/name
        assert name not in names and ':' not in name and '\\' not in name and p.resolve().is_relative_to(folder)
        assert not any(x.is_symlink() or x.is_junction() for x in [p,*p.parents])
        assert p.stat().st_size==row['size'] and sha(p)==row['sha256'];names.add(name)
    assert {p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file()}==names|{'receiver-manifest.json','receiver-manifest.sha256'}
    assert {'runtime/bin/java.exe','lib/monaka-common-world-receiver.jar','receiver.cmd','templates/receiver.inactive.json'}<=names
    config=json.loads((folder/'templates/receiver.inactive.json').read_text())
    assert config['enabled'] is False and config['udp_port']==0 and not config['bind_ipv4'] and config['required_wire']=='monaka-wire-2.2'
    with zipfile.ZipFile(folder/'lib/monaka-common-world-receiver.jar') as jar:
        assert not any('ReceiverTests' in n or '/Fake' in n or '/Rig' in n for n in jar.namelist())
        props=dict(line.split('=',1) for line in jar.read('build-identity.properties').decode().splitlines())
        assert props['commit']==manifest['commit'] and props['build_input_sha256']==manifest['build_input_sha256']
    return manifest
def package(folder,candidate=False):
    clean=not git('status','--porcelain')
    assert clean or candidate,'clean committed receiver tree required'
    folder=Path(folder).resolve();assert folder.is_relative_to(ROOT/'dist') and not folder.exists()
    jar=ROOT/'receiver/build/libs/monaka-common-world-receiver.jar'
    with zipfile.ZipFile(jar) as z:props=dict(line.split('=',1) for line in z.read('build-identity.properties').decode().splitlines())
    assert props['commit']==git('rev-parse','HEAD') and props['branch']==git('branch','--show-current')
    for name in ['lib','templates','licenses']: (folder/name).mkdir(parents=True,exist_ok=True)
    shutil.copy2(jar,folder/'lib'/jar.name)
    dependencies=[]
    for line in (ROOT/'receiver/build/runtime-classpath.txt').read_text().splitlines():
        p=Path(line);assert p.suffix=='.jar' and not (folder/'lib'/p.name).exists()
        shutil.copy2(p,folder/'lib'/p.name);dependencies.append({'name':p.name,'sha256':sha(p)})
        with zipfile.ZipFile(p) as z:
            for n in z.namelist():
                if n.startswith('META-INF/') and any(s in n.upper() for s in ['LICENSE','COPYRIGHT','NOTICE']) and not n.endswith('/'):
                    (folder/'licenses'/(p.stem+'-'+Path(n).name)).write_bytes(z.read(n))
    for p in (ROOT/'third_party/monaka-protocol-v22/licenses').rglob('*'):
        if p.is_file():shutil.copy2(p,folder/'licenses'/('protocol-'+p.name))
    shutil.copy2(ROOT/'receiver/libs/LICENSE-APACHE',folder/'licenses/serialization-core-LICENSE-APACHE')
    shutil.copy2(ROOT/'receiver/upstream/LICENSE-MIT',folder/'licenses/core-LICENSE-MIT')
    shutil.copy2(ROOT/'receiver/upstream/LICENSE-APACHE',folder/'licenses/core-LICENSE-APACHE')
    shutil.copy2(ROOT/'receiver/NOTICE.md',folder/'licenses/NOTICE.md')
    shutil.copy2(ROOT/'receiver/provenance.json',folder/'source-provenance.json')
    shutil.copy2(ROOT/'docs/common-world-production-receiver-wire-v2.2.md',folder/'README.md')
    shutil.copy2(ROOT/'deployment/receiver.inactive.json',folder/'templates/receiver.inactive.json')
    (folder/'receiver.cmd').write_bytes(b'@echo off\r\n"%~dp0runtime\\bin\\java.exe" -Dfile.encoding=UTF-8 -cp "%~dp0lib\\*" dev.monaka.tracking.ReceiverMainKt %*\r\nexit /b %ERRORLEVEL%\r\n')
    subprocess.run([str(JDK/'bin/jlink.exe'),'--module-path',str(JDK/'jmods'),'--add-modules','java.base,java.logging,java.sql','--strip-debug','--no-header-files','--no-man-pages','--compress=2','--output',str(folder/'runtime')],check=True)
    (folder/'dependency-inventory.json').write_text(json.dumps({'jars':dependencies,'jdk_source':str(JDK),'jdk_release':(JDK/'release').read_text(),'jdk_jlink_sha256':sha(JDK/'bin/jlink.exe'),'modules':['java.base','java.logging','java.sql']},indent=2)+'\n')
    manifest={'schema':'monaka-common-world-receiver-artifact-1','repository':git('remote','get-url','origin'),'branch':props['branch'],'commit':props['commit'],
        'build_input_sha256':props['build_input_sha256'],'configuration':props['configuration'],'architecture':'Windows x64 private JVM','runtime':next(line.split('=',1)[1].strip('"') for line in (JDK/'release').read_text().splitlines() if line.startswith('JAVA_RUNTIME_VERSION=')),'default_listening':False,
        'required_wire_version':'monaka-wire-2.2','protocol_commit':props['protocol_commit'],'protocol_kit_sha256':props['protocol_kit_sha256'],
        'association_schema':'MCWRXA01','association_version':1,'history_schema':'MCWRXO01','history_version':1,'source_tree_clean':clean,'candidate':candidate,
        'artifact_jar_sha256':sha(folder/'lib'/jar.name),'executable_sha256':sha(folder/'runtime/bin/java.exe'),'config_template_sha256':sha(folder/'templates/receiver.inactive.json'),
        'files':[{'path':p.relative_to(folder).as_posix(),'size':p.stat().st_size,'sha256':sha(p)} for p in sorted(folder.rglob('*')) if p.is_file()]}
    (folder/'receiver-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (folder/'receiver-manifest.sha256').write_text(sha(folder/'receiver-manifest.json')+'\n')
    validate(folder)
    archive=folder.with_suffix('.zip')
    assert not archive.exists()
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for p in sorted(folder.rglob('*')):
            if p.is_file():
                info=zipfile.ZipInfo(p.relative_to(folder).as_posix(),date_time=(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o644<<16;z.writestr(info,p.read_bytes())
    print(json.dumps({'status':'CANDIDATE' if candidate else 'PASS','path':str(folder),'commit':manifest['commit'],'files':len(manifest['files']),'archive':str(archive),'archive_sha256':sha(archive)}))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('command',choices=['package','validate']);p.add_argument('path');p.add_argument('--candidate',action='store_true');a=p.parse_args()
    if a.command=='package':package(a.path,a.candidate)
    else:print(json.dumps({'status':'PASS','manifest':validate(a.path)}))
