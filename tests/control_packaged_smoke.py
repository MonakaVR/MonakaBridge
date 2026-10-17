"""Actual packaged GUI startup/close on synthetic copies, no service or UDP."""
from pathlib import Path
import ctypes,hashlib,json,subprocess,sys,time,uuid
from ctypes import wintypes
package=Path(sys.argv[1]).resolve();work=Path(sys.argv[2]).resolve()/str(uuid.uuid4());work.mkdir(parents=True)
exe=package/'monaka_bridge_control.exe';helper=package/'monaka_bridge_config.exe'
example=Path(__file__).resolve().parents[1]/'config/bridge.example.json'
user32=ctypes.WinDLL('user32',use_last_error=True)
callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
user32.EnumWindows.argtypes=[callback,wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowTextW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
user32.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
def windows(pid):
 found=[]
 @callback
 def visit(hwnd,param):
  owner=wintypes.DWORD();user32.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
  if owner.value==pid:
   title=ctypes.create_unicode_buffer(2048);user32.GetWindowTextW(hwnd,title,len(title));found.append((hwnd,title.value))
  return True
 user32.EnumWindows(visit,0);return found
def inventory(root):return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
records=[]
for mode in ['explicit-write','profile-readonly','legacy-root']:
 root=work/(mode+' 日本語 space');(root/'config').mkdir(parents=True)
 target=root/'config'/'bridge.json';health=Path(str(target)+'.status.json')
 config=json.loads(example.read_text(encoding='utf-8-sig'));config['bridge_id']='smoke-fixture';target.write_text(json.dumps(config),encoding='utf-8')
 health.write_text(json.dumps(dict(publisher_id='smoke-fixture',mapping_revision=1,policy='steamvr',snapshot_unix_ms=int(time.time()*1000),devices=[])),encoding='utf-8')
 profile=root/'installation.json';profile.write_text(json.dumps(dict(schema='monaka.bridge.installation.v1',name='Packaged fixture 日本語',config='config/bridge.json',health='config/bridge.json.status.json',config_helper=str(helper),helper_sha256=hashlib.sha256(helper.read_bytes()).hexdigest(),read_only=True)),encoding='utf-8')
 if mode=='legacy-root':
  import shutil
  (root/'build'/'Release').mkdir(parents=True);shutil.copyfile(helper,root/'build'/'Release'/'monaka_bridge_config.exe')
  args=[str(root),'--control-window-only']
 elif mode=='profile-readonly':args=['--installation',str(profile),'--control-window-only']
 else:args=['--config',str(target),'--health',str(health),'--config-helper',str(helper),'--allow-write','--installation-name','Packaged fixture 日本語','--control-window-only']
 before=inventory(root);startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
 child=subprocess.Popen([str(exe),*args],stdout=subprocess.PIPE,stderr=subprocess.PIPE,startupinfo=startup)
 clean=False
 try:
  found=[];deadline=time.monotonic()+15
  while time.monotonic()<deadline:
   if child.poll() is not None:raise RuntimeError('GUI exited before startup: '+str(child.returncode))
   found=windows(child.pid)
   if any('Monaka Bridge |' in title for _,title in found):break
   time.sleep(.05)
  else:raise RuntimeError('Owned GUI window startup timeout')
  title=next(title for _,title in found if 'Monaka Bridge |' in title)
  if mode=='profile-readonly' and 'READ ONLY' not in title:raise RuntimeError('Readonly lost through actual Main')
  # Let Loaded, compatibility query and health timer run before close.
  time.sleep(1.25)
  for hwnd,_ in windows(child.pid):user32.PostMessageW(hwnd,0x0010,0,0) # exact newly spawned PID, WM_CLOSE
  stdout,stderr=child.communicate(timeout=10);clean=child.returncode==0
  if not clean:raise RuntimeError('GUI exit failed: '+str(child.returncode))
  if inventory(root)!=before:raise RuntimeError('Startup/close modified synthetic installation')
  records.append(dict(mode=mode,pid=child.pid,title=title,exit_code=child.returncode,startup_no_writes=True,clean_shutdown=True,stdout=stdout.decode(errors='replace'),stderr=stderr.decode(errors='replace')))
  print('PASS actual packaged executable',mode,': startup/no writes/clean close; owned PID',child.pid,flush=True)
 finally:
  if child.poll() is None:child.kill();child.communicate(timeout=5)
(work/'result.json').write_text(json.dumps(dict(result='PASS',cases=records,gui_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),helper_sha256=hashlib.sha256(helper.read_bytes()).hexdigest(),manual_visual='NOT_RUN',hardware='NOT_RUN',network='NOT_RUN'),ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS packaged executable smoke: 3 cases; manual visual/hardware/network NOT_RUN')
