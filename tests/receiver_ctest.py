"""Run only the isolated receiver software gate; no future production endpoints."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(root/'scripts/receiver_build.py'),'test'],cwd=root,check=True)
