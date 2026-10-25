from pathlib import Path
import json,shutil,tempfile
from preflight_remediation_scope import approved,PIN,digest
root=Path(__file__).resolve().parents[1];names=approved(root);assert names
with tempfile.TemporaryDirectory(dir=root/'build') as folder:
    scratch=Path(folder)
    for name in names:
        p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,p)
    approved(scratch,root)
    def reject():
        try:approved(scratch,root)
        except RuntimeError:return
        raise AssertionError('Unreviewed remediation accepted')
    for name in ('scripts/hil_preflight.py','tests/hil_preflight_public_input_tests.py','tests/final_integration_scope.py'):
        p=scratch/name;before=p.read_bytes();p.write_bytes(before+b'\n# unreviewed change\n');reject()
        pin=scratch/PIN;original=pin.read_bytes();v=json.loads(original);v['files'][name]['sha256']=digest(p.read_bytes());pin.write_text(json.dumps(v));reject()
        pin.write_bytes(original);p.write_bytes(before)
print('PASS remediation source mutation and receipt rehash refusal; predecessor receipts remain immutable')
