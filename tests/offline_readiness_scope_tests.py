from pathlib import Path
import json,shutil,tempfile
from offline_readiness_scope import approved_offline,PIN,digest
root=Path(__file__).resolve().parents[1];names=approved_offline(root);assert names
with tempfile.TemporaryDirectory(dir=root/'build') as folder:
    scratch=Path(folder)
    for name in names:
        p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
    approved_offline(scratch,root)
    def reject():
        try:approved_offline(scratch,root)
        except RuntimeError:return
        raise AssertionError('Unreviewed source/receipt accepted')
    for name in ['src/calibration/output_approval_authority.cpp','src/calibration/calibration_production_entry.cpp','outputs/steamvr/driver/device_provider.cpp','scripts/hil_preflight.py','ui/service_lifecycle_control.cs']:
        p=scratch/name;before=p.read_bytes();p.write_bytes(before+b'\n// unexpected drift\n');reject()
        pin=scratch/PIN;original=pin.read_bytes();v=json.loads(original);v['files'][name]['sha256']=digest(p.read_bytes());pin.write_text(json.dumps(v));reject();pin.write_bytes(original);p.write_bytes(before)
print('PASS current-source admission and receipt-rehash refusal; predecessor projections do not bypass current tests')
