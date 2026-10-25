"""Non-executing finished package verification, also used by packaging tests."""
from pathlib import Path
import argparse,hashlib,json
PRODUCTS={'monaka_bridge_control.exe','monaka_bridge_calibrator.exe','monaka_bridge_config.exe','monaka_bridge_service.exe'}
MARKERS=[b'test_only_fixture_open',b'test_only_runtime_fixture',b'MONAKA_CALIBRATION_TEST_ROOT',b'test-only deliberate fault',b'MONAKA_OPENVR_SENTINEL']
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def verify(directory,expected_commit=None):
 directory=Path(directory);manifest=json.loads((directory/'control-toolset-manifest.json').read_bytes())
 if manifest['schema']!='monaka.bridge.calibration-control-toolset.v1' or expected_commit is not None and manifest['source_commit']!=expected_commit:raise RuntimeError('Package schema/source identity mismatch')
 actual={str(n.relative_to(directory)).replace('\\','/') for n in directory.rglob('*') if n.is_file()}-{'control-toolset-manifest.json'}
 if actual!=set(manifest['files_sha256']):raise RuntimeError('Package inventory drift')
 for name,digest in manifest['files_sha256'].items():
  if sha(directory/name)!=digest:raise RuntimeError('Package content drift: '+name)
 lock=json.loads((Path(__file__).resolve().parents[1]/'dependencies/calibration-openvr-sdk.json').read_bytes())
 if manifest['sdk']!=lock or sha(directory/'openvr_api.dll')!=lock['files']['bin/win64/openvr_api.dll']:raise RuntimeError('Genuine pinned SDK DLL/provenance mismatch')
 for name,digest in lock['files'].items():
  if sha(directory/'provenance/openvr'/name)!=digest:raise RuntimeError('Pinned SDK source/library artifact drift')
 identity=json.loads((directory/'provenance/build/build-identity.json').read_bytes())
 if sha(directory/'provenance/build/build-identity.json')!=manifest['build_identity_sha256'] or identity['source_commit']!=manifest['source_commit'] or identity['source_tree']!=manifest['source_tree'] or identity['git_clean'] is not True:raise RuntimeError('Package/build identity mismatch')
 if any(r['exit_code'] for r in identity['records']) or {Path(n).name for n in identity['products_sha256']}!=PRODUCTS:raise RuntimeError('Incomplete successful product build')
 if {n.name for n in directory.glob('*.exe')}!=PRODUCTS:raise RuntimeError('Unknown/missing runtime executable')
 for name,digest in identity['products_sha256'].items():
  product=directory/Path(name).name
  if sha(product)!=digest:raise RuntimeError('Product/build hash mismatch: '+name)
  raw=product.read_bytes()
  if any(m in raw or m.decode().encode('utf-16-le') in raw for m in MARKERS):raise RuntimeError('Fixture contamination: '+name)
 for key in ['actual_sdk_execution','os_visual_validation','physical_hil']:
  if manifest[key]!='NOT_RUN' or identity[key]!='NOT_RUN':raise RuntimeError('Unsupported runtime/visual/HIL claim')
 return manifest
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('directory');p.add_argument('--commit');a=p.parse_args();result=verify(a.directory,a.commit);print('PASS exact matching product package; pinned genuine SDK; no fixture; source '+result['source_commit']+'; no executable launched')
