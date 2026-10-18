from pathlib import Path
import tempfile,shutil,json
from responsibility_scope import approved_responsibility,FILES,PIN
root=Path(__file__).resolve().parents[1]
approved=approved_responsibility(root)
with tempfile.TemporaryDirectory(dir=root/'build') as directory:
 scratch=Path(directory)
 for name in approved:
  target=scratch/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,target)
 approved_responsibility(scratch,root)
 count=0
 for name in sorted(approved-{PIN}):
  target=scratch/name;before=target.read_bytes();target.write_bytes(before+b'\n// unexpected mutation\n')
  try:approved_responsibility(scratch,root)
  except (RuntimeError,ValueError):count+=1
  else:raise AssertionError('Accepted unexpected change: '+name)
  target.write_bytes(before)
 # A rehashed UI approval bypass must still be refused.
 target=scratch/'ui/control_models.cs';before=target.read_bytes();target.write_bytes(before.replace(b'&&!sameApproval',b'&&false'))
 p=json.loads((scratch/PIN).read_bytes())
 from responsibility_scope import digest
 p['files']['ui/control_models.cs']['sha256']=digest(target.read_bytes());(scratch/PIN).write_text(json.dumps(p))
 try:approved_responsibility(scratch,root)
 except RuntimeError:count+=1
 else:raise AssertionError('Accepted rehashed approval bypass')
print('PASS responsibility scope:',count,'negative cases; frozen native surfaces and receipts unchanged')
