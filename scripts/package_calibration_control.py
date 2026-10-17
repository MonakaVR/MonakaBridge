"""Package only matching committed product binaries; no executable is launched."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile
p=argparse.ArgumentParser()
for name in ['build','sdk','output','commit']:p.add_argument('--'+name,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1];build=Path(a.build).resolve();sdk=Path(a.sdk).resolve();out=Path(a.output).resolve()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git','--no-optional-locks','-C',str(root),*args]).decode().strip()
if git('status','--porcelain') or git('rev-parse','HEAD')!=a.commit:raise SystemExit('Expected exact clean committed source')
identity=json.loads((build/'build-identity.json').read_bytes())
if identity['source_commit']!=a.commit or identity['git_clean'] is not True or any(x['exit_code'] for x in identity['records']):raise SystemExit('Missing matching successful final build/test identity')
for n,h in identity['source_sha256'].items():
 if sha(root/n)!=h:raise SystemExit('Source/build drift: '+n)
for n,h in identity['products_sha256'].items():
 if sha(build/n)!=h:raise SystemExit('Product/build drift: '+n)
lock=json.loads((root/'dependencies/calibration-openvr-sdk.json').read_bytes())
if subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()!=lock['commit']:raise SystemExit('SDK HEAD drift')
for n,h in lock['files'].items():
 if sha(sdk/n)!=h:raise SystemExit('SDK artifact drift: '+n)
if out.exists():raise SystemExit('New output directory required')
out.mkdir(parents=True)
def copy(source,name):
 target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
for n in identity['products_sha256']:copy(build/n,Path(n).name)
copy(sdk/'bin/win64/openvr_api.dll','openvr_api.dll')
for n in lock['files']:copy(sdk/n,'provenance/openvr/'+n)
for n in ['dependencies/calibration-openvr-sdk.json','dependencies/mbip-p1.lock.json','dependencies/mbip-p1-source.tar','dependencies/calibration-gui-baseline.json','docs/calibration-gui.md','docs/native-calibration-capture.md']:copy(root/n,'provenance/'+n)
for n in ['build-identity.json','build-steps.json','full-regression.xml','ctest.stdout.txt','ctest.stderr.txt','build.stdout.txt','build.stderr.txt','configure.stdout.txt','configure.stderr.txt']:copy(build/n,'provenance/build/'+n)
for n in ['LICENSE','NOTICE']:
 if (root/n).is_file():copy(root/n,n)
subprocess.run(['git','-C',str(root),'archive','--format=zip','--output='+str(out/'provenance/bridge-source.zip'),a.commit],check=True)
helper=out/'monaka_bridge_config.exe';profile=dict(schema='monaka.bridge.installation.v1',name='Selected Bridge installation',config='config/bridge.json',health='config/bridge.json.status.json',config_helper=helper.name,helper_sha256=sha(helper),read_only=True)
(out/'installation.example.json').write_text(json.dumps(profile,indent=2)+'\n')
copy(root/'docs/calibration-gui.md','README.md')
# Known fixture/sentinel markers are a packaging failure. The exact genuine
# SDK DLL hash is mandatory; no actual product process runs in this directory.
markers=[b'test_only_fixture_open',b'test_only_runtime_fixture',b'MONAKA_CALIBRATION_TEST_ROOT',b'test-only deliberate fault',b'MONAKA_OPENVR_SENTINEL']
for product in out.glob('*.exe'):
 raw=product.read_bytes()
 if any(m in raw or m.decode().encode('utf-16-le') in raw for m in markers):raise SystemExit('Fixture contamination: '+product.name)
files={str(n.relative_to(out)).replace('\\','/'):sha(n) for n in sorted(out.rglob('*')) if n.is_file()}
manifest=dict(schema='monaka.bridge.calibration-control-toolset.v1',source_commit=a.commit,source_tree=identity['source_tree'],build_identity_sha256=sha(build/'build-identity.json'),sdk=lock,protocol=json.loads((root/'dependencies/mbip-p1.lock.json').read_bytes()),files_sha256=files,lifecycle='unsupported',actual_sdk_execution='NOT_RUN',os_visual_validation='NOT_RUN',physical_hil='NOT_RUN',pico_same_tracker_imu_authenticity='NOT VERIFIED / DEFERRED')
(out/'control-toolset-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
from verify_control_toolset import verify
verify(out,a.commit)
print(json.dumps(dict(result='PASS',output=str(out),files=len(files),source_commit=a.commit,manifest_sha256=sha(out/'control-toolset-manifest.json'))))
