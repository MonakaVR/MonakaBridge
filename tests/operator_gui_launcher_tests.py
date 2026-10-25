"""GUI entry remains available after rebind; package integrity stays mandatory."""
from pathlib import Path
import importlib.util, tempfile, json, hashlib, copy, unittest
spec=importlib.util.spec_from_file_location('operator',Path(__file__).parents[1]/'scripts/operator_gui_launcher.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class EntryTests(unittest.TestCase):
 def test_selection_drift_is_diagnostic_and_tampered_product_is_rejected(self):
  with tempfile.TemporaryDirectory(dir=Path(__file__).parents[1]/"build") as directory:
   root=Path(directory);package=root/'package';package.mkdir()
   files={'gui':'monaka_bridge_control.exe','helper':'monaka_bridge_config.exe','calibration_helper':'monaka_bridge_calibrator.exe','sdk':'openvr_api.dll'}
   profile={'candidate_revision':'H1-R1','gui_source_base_commit':'test-base','source_is_uncommitted_candidate':True,'publisher':'old-publisher','selected_mapping':{'source_id':'source','device_id':'old','tracker_id':'hip'},'operator_root':str(root)}
   hashes={}
   for key,name in files.items():
    path=package/name;path.write_bytes(('test-only '+name).encode());hashes[name]=module.sha(path);profile[key]=str(path);profile[key+'_sha256']=hashes[name]
   manifest=package/'candidate-manifest.json';manifest.write_text(json.dumps(dict(schema='monaka.bridge.hil-preview-candidate.v1',base_commit='test-base',revision='H1-R1',source_is_uncommitted_candidate=True,files_sha256=hashes)))
   profile['package_manifest']=str(manifest);profile['package_manifest_sha256']=module.sha(manifest)
   config=root/'config.json';config.write_text(json.dumps(dict(namespace='monaka.bridge',version=2,bridge_id='new-publisher',mappings=[])))
   profile['config']=str(config);profile['health']=str(root/'health.json')
   path=root/'operator-profile.json';path.write_text(json.dumps(profile))
   for mode in ['normal','diagnostic']:
    checked,args=module.checked(path,mode)
    self.assertFalse(checked['selection_matches']);self.assertIn('review',checked['selection_warning'])
    self.assertIn('--allow-write' if mode=='normal' else '--read-only',args)
   config.write_text(json.dumps(dict(namespace='monaka.bridge',version=2,bridge_id='old-publisher',mappings=[profile['selected_mapping']])))
   self.assertTrue(module.checked(path,'normal')[0]['selection_matches'])
   Path(profile['gui']).write_bytes(b'tampered')
   with self.assertRaisesRegex(ValueError,'SHA256'):module.checked(path,'normal')
if __name__=='__main__':unittest.main()
