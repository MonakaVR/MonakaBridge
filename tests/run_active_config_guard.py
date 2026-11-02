"""Compile and execute the actual config-identity lease against isolated files/events."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--compiler', required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
exe = args.out.resolve() / 'active-config-tests.exe'
command = [args.compiler, '/nologo', '/target:exe', '/reference:System.dll',
           '/reference:System.Core.dll', '/out:' + str(exe),
           str(args.root / 'ui/service_lifecycle_control.cs'),
           str(args.root / 'tests/active_config_guard_tests.cs')]
print(subprocess.list2cmdline(command), flush=True)
subprocess.run(command, check=True)
subprocess.run([str(exe), str(args.out.resolve() / 'files')], check=True)
