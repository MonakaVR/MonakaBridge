"""Build using an explicitly selected installed CMake; normalize only child environment names."""
import argparse,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cmake',required=True);p.add_argument('--configure',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[1];env={key.upper():value for key,value in os.environ.items()}
if a.configure:subprocess.run([a.cmake,'-S',str(root),'-B',str(root/'build'),'-G','Visual Studio 18 2026','-A','x64'],env=env,check=True)
subprocess.run([a.cmake,'--build',str(root/'build'),'--config','Release','-j','1'],env=env,check=True)
