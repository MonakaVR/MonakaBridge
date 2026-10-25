"""Pinned public SDK bytes, before compiling/linking the production adapter."""
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(__file__).resolve().parents[1]
sdk=Path(sys.argv[1]);lock=json.loads((root/'dependencies/calibration-openvr-sdk.json').read_bytes())
for name,expected in lock['files'].items():
 if hashlib.sha256((sdk/name).read_bytes()).hexdigest()!=expected:raise SystemExit('calibration SDK artifact drift: '+name)
head=subprocess.check_output(['git','--no-optional-locks','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()
if head!=lock['commit']:raise SystemExit('calibration SDK commit drift')
print('PASS pinned calibration SDK/header/import library/runtime DLL/license; compile/link provenance only')
