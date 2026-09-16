"""Current wire-v2 interop entry. Historical wire-v1 fixtures are not this gate."""
import argparse
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument("--cpp", type=Path, required=True)
p.add_argument("--java-home", type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(root / "scripts/release_interop.py"),
                "--kit", str(root / "third_party/monaka-protocol-v2"),
                "--cpp", str(a.cpp), "--java-home", str(a.java_home),
                "--work", str(root / "build/interop-v2")], check=True)
