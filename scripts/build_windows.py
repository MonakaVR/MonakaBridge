"""Build using an explicitly selected installed CMake; normalize only child environment names."""
import argparse,os,subprocess,sys
from pathlib import Path
from runtime_dependencies import prepare_openvr
p=argparse.ArgumentParser();p.add_argument('--cmake',required=True);p.add_argument('--configure',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[1];env={key.upper():value for key,value in os.environ.items()}
if a.configure:
 sdk=prepare_openvr(root)
 subprocess.run([a.cmake,'-S',str(root),'-B',str(root/'Build/runtime'),'-G','Visual Studio 18 2026','-A','x64','-DOPENVR_SDK='+str(sdk),'-DPython3_EXECUTABLE='+sys.executable],env=env,check=True)
subprocess.run([a.cmake,'--build',str(root/'Build/runtime'),'--config','Release','-j','1'],env=env,check=True)
