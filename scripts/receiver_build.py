"""Offline JVM production receiver build; build/package never starts a listener."""
from pathlib import Path
from java_tools import java_home,gradle_executable
import hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(*a):return subprocess.check_output(['git','-C',str(ROOT),*a]).decode().strip()
def inputs():
    files=[p for directory in ['receiver/src','receiver/upstream','receiver/libs'] for p in (ROOT/directory).rglob('*') if p.is_file()]
    files += [ROOT/'receiver/build.gradle.kts',ROOT/'receiver/settings.gradle.kts',ROOT/'receiver/provenance.json',ROOT/'scripts/receiver_build.py']
    return hashlib.sha256(b''.join(p.relative_to(ROOT).as_posix().encode()+b'\0'+bytes.fromhex(sha(p)) for p in sorted(files))).hexdigest()
subprocess.run([sys.executable,str(ROOT/'scripts/verify_protocol_v22.py')],check=True)
properties={'commit':git('rev-parse','HEAD'),'branch':git('branch','--show-current'),'build_input_sha256':inputs(),'runtime':'JVM-17','architecture':'x64 JVM; portable bytecode','configuration':'Release JVM bytecode','required_wire':'monaka-wire-2.2','protocol_commit':'0b4a27c2e2de88705c92fb6dbf5a8d9ca752bf39','protocol_kit_sha256':'14f51d889ce665ee1f598bc67edacf0e7bc70d16ce0af9f1788c5f1efee04e85'}
build=ROOT/'Build/receiver';build.mkdir(parents=True,exist_ok=True)
(build/'build-identity.properties').write_text(''.join(k+'='+v+'\n' for k,v in properties.items()))
env=dict(os.environ);env['JAVA_HOME']=str(java_home());env['PATH']=str(Path(env['JAVA_HOME'])/'bin')+os.pathsep+env.get('PATH','')
gradle=gradle_executable()
tasks=['jar','runtimeDependencies']+(['receiverTest'] if len(sys.argv)>1 and sys.argv[1]=='test' else [])
subprocess.run([str(gradle),'-p',str(ROOT/'receiver'),'--project-cache-dir',str(build/'gradle-cache'),'--offline','--no-daemon','--console=plain',*tasks],env=env,check=True)
print(json.dumps(properties))
