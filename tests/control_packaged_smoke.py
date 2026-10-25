"""Actual packaged GUI startup/close on synthetic copies, no service or UDP."""
from pathlib import Path
import ctypes,hashlib,json,subprocess,sys,time,uuid
from ctypes import wintypes
package=Path(sys.argv[1]).resolve();work=Path(sys.argv[2]).resolve()/str(uuid.uuid4());work.mkdir(parents=True)
exe=package/'monaka_bridge_control.exe';helper=package/'monaka_bridge_config.exe'
example=Path(__file__).resolve().parents[1]/'config/bridge.example.json'
user32=ctypes.WinDLL('user32',use_last_error=True)
kernel32=ctypes.WinDLL('kernel32',use_last_error=True)
callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
user32.EnumDesktopWindows.argtypes=[wintypes.HANDLE,callback,wintypes.LPARAM]
user32.CreateDesktopW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p]
user32.CreateDesktopW.restype=wintypes.HANDLE
user32.CloseDesktop.argtypes=[wintypes.HANDLE]
user32.PostThreadMessageW.argtypes=[wintypes.DWORD,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowTextW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
user32.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
user32.IsWindowVisible.argtypes=[wintypes.HWND]
class StartupInfo(ctypes.Structure):
 _fields_=[('cb',wintypes.DWORD),('reserved',wintypes.LPWSTR),('desktop',wintypes.LPWSTR),('title',wintypes.LPWSTR),*[(n,wintypes.DWORD) for n in ['x','y','width','height','xchars','ychars','fill','flags']],('show',wintypes.WORD),('reserved_size',wintypes.WORD),('reserved_bytes',ctypes.c_void_p),('stdin',wintypes.HANDLE),('stdout',wintypes.HANDLE),('stderr',wintypes.HANDLE)]
class ProcessInfo(ctypes.Structure):
 _fields_=[('process',wintypes.HANDLE),('thread',wintypes.HANDLE),('pid',wintypes.DWORD),('tid',wintypes.DWORD)]
class ProcessEntry(ctypes.Structure):
 _fields_=[('size',wintypes.DWORD),('usage',wintypes.DWORD),('pid',wintypes.DWORD),('heap',ctypes.c_size_t),('module',wintypes.DWORD),('threads',wintypes.DWORD),('parent',wintypes.DWORD),('priority',wintypes.LONG),('flags',wintypes.DWORD),('exe',wintypes.WCHAR*260)]
kernel32.CreateProcessW.argtypes=[wintypes.LPCWSTR,wintypes.LPWSTR,ctypes.c_void_p,ctypes.c_void_p,wintypes.BOOL,wintypes.DWORD,ctypes.c_void_p,wintypes.LPCWSTR,ctypes.POINTER(StartupInfo),ctypes.POINTER(ProcessInfo)]
kernel32.CloseHandle.argtypes=[wintypes.HANDLE]
kernel32.GetExitCodeProcess.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)]
kernel32.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD]
kernel32.TerminateProcess.argtypes=[wintypes.HANDLE,wintypes.UINT]
kernel32.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel32.OpenProcess.restype=wintypes.HANDLE
kernel32.CreateToolhelp32Snapshot.argtypes=[wintypes.DWORD,wintypes.DWORD];kernel32.CreateToolhelp32Snapshot.restype=wintypes.HANDLE
kernel32.Process32FirstW.argtypes=[wintypes.HANDLE,ctypes.POINTER(ProcessEntry)]
kernel32.Process32NextW.argtypes=[wintypes.HANDLE,ctypes.POINTER(ProcessEntry)]
kernel32.QueryFullProcessImageNameW.argtypes=[wintypes.HANDLE,wintypes.DWORD,wintypes.LPWSTR,ctypes.POINTER(wintypes.DWORD)]
class OwnedProcess:
 def __init__(self,args,cwd,desktop):
  si=StartupInfo();si.cb=ctypes.sizeof(si);si.desktop=desktop;si.flags=1;si.show=0;pi=ProcessInfo()
  command=ctypes.create_unicode_buffer(subprocess.list2cmdline(args))
  if not kernel32.CreateProcessW(args[0],command,None,None,False,0x08000000,None,str(cwd),ctypes.byref(si),ctypes.byref(pi)):raise ctypes.WinError(ctypes.get_last_error())
  self.handle=pi.process;self.pid=pi.pid;self.tid=pi.tid;kernel32.CloseHandle(pi.thread)
 def poll(self):
  code=wintypes.DWORD()
  if not kernel32.GetExitCodeProcess(self.handle,ctypes.byref(code)):raise ctypes.WinError(ctypes.get_last_error())
  return None if code.value==259 else code.value
 def wait(self,seconds=10):
  if kernel32.WaitForSingleObject(self.handle,int(seconds*1000))!=0:raise RuntimeError('Owned process shutdown timeout')
  return self.poll()
 def close(self):
  if self.poll() is None:kernel32.TerminateProcess(self.handle,97);self.wait(5)
  kernel32.CloseHandle(self.handle)
def owned_gui_child(parent):
 snapshot=kernel32.CreateToolhelp32Snapshot(2,0)
 if snapshot in [None,ctypes.c_void_p(-1).value]:raise ctypes.WinError(ctypes.get_last_error())
 try:
  entry=ProcessEntry();entry.size=ctypes.sizeof(entry);more=kernel32.Process32FirstW(snapshot,ctypes.byref(entry))
  while more:
   if entry.parent==parent and entry.exe.casefold()==exe.name.casefold():
    handle=kernel32.OpenProcess(0x1000|0x100000|1,False,entry.pid)
    if handle:
     path=ctypes.create_unicode_buffer(32768);size=wintypes.DWORD(len(path))
     if kernel32.QueryFullProcessImageNameW(handle,0,path,ctypes.byref(size)) and Path(path.value).resolve()==exe:
      return entry.pid,handle
     kernel32.CloseHandle(handle)
   more=kernel32.Process32NextW(snapshot,ctypes.byref(entry))
 finally:kernel32.CloseHandle(snapshot)
 return None
def windows(desktop,pid):
 found=[]
 @callback
 def visit(hwnd,param):
  owner=wintypes.DWORD();user32.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
  if owner.value==pid:
   title=ctypes.create_unicode_buffer(2048);user32.GetWindowTextW(hwnd,title,len(title));found.append((hwnd,title.value))
  return True
 user32.EnumDesktopWindows(desktop,visit,0);return found
def inventory(root):return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
records=[]
for mode in ['explicit-write','profile-readonly','legacy-root','tray-explicit-write','tray-profile-readonly']:
 root=work/(mode+' 日本語 space');(root/'config').mkdir(parents=True)
 target=root/'config'/'bridge.json';health=Path(str(target)+'.status.json')
 config=json.loads(example.read_text(encoding='utf-8-sig'));config['bridge_id']='smoke-fixture';target.write_text(json.dumps(config),encoding='utf-8')
 health.write_text(json.dumps(dict(publisher_id='smoke-fixture',mapping_revision=1,policy='steamvr',snapshot_unix_ms=int(time.time()*1000),devices=[])),encoding='utf-8')
 profile=root/'installation.json';profile.write_text(json.dumps(dict(schema='monaka.bridge.installation.v1',name='Packaged fixture 日本語',config='config/bridge.json',health='config/bridge.json.status.json',config_helper=str(helper),helper_sha256=hashlib.sha256(helper.read_bytes()).hexdigest(),read_only=True)),encoding='utf-8')
 if mode=='legacy-root':
  import shutil
  (root/'build'/'Release').mkdir(parents=True);shutil.copyfile(helper,root/'build'/'Release'/'monaka_bridge_config.exe')
  args=[str(root),'--control-window-only']
 elif mode in ['profile-readonly','tray-profile-readonly']:args=['--installation',str(profile),'--control-window-only']
 else:args=['--config',str(target),'--health',str(health),'--config-helper',str(helper),'--allow-write','--installation-name','Packaged fixture 日本語','--control-window-only']
 is_tray=mode.startswith('tray-')
 if is_tray:args.remove('--control-window-only')
 before=inventory(root);desktop_name='MonakaControlSmoke-'+str(uuid.uuid4());desktop=user32.CreateDesktopW(desktop_name,None,None,0,0x01ff,None)
 if not desktop:raise ctypes.WinError(ctypes.get_last_error())
 child=None;gui_child=None;health_fixtures=[]
 try:
  child=OwnedProcess([str(exe),*args],root,desktop_name)
  found=[];deadline=time.monotonic()+15
  while time.monotonic()<deadline:
   if child.poll() is not None:raise RuntimeError('GUI exited before startup: '+str(child.poll()))
   if is_tray and gui_child is None:gui_child=owned_gui_child(child.pid)
   found=windows(desktop,gui_child[0] if gui_child else child.pid)
   if any('Monaka Bridge |' in title for _,title in found):break
   time.sleep(.05)
  else:raise RuntimeError('Owned GUI window startup timeout')
  title=next(title for _,title in found if 'Monaka Bridge |' in title)
  if 'readonly' in mode and 'READ ONLY' not in title:raise RuntimeError('Readonly lost through actual Main/tray child')
  # Let Loaded, compatibility query and health timer run before close.
  time.sleep(1.25)
  if inventory(root)!=before:raise RuntimeError('Startup modified synthetic installation')
  if is_tray:
   for fixture,value in [('missing',None),('null',None),('wrong-type',42),('unknown','unknown-policy'),('stale','both'),('recovered','monaka')]:
    snapshot=dict(publisher_id='smoke-fixture',mapping_revision=1,snapshot_unix_ms=int(time.time()*1000)-(10000 if fixture=='stale' else 0),devices=[])
    if fixture!='missing':snapshot['policy']=value
    health.write_text(json.dumps(snapshot),encoding='utf-8');before[str(health.relative_to(root))]=hashlib.sha256(health.read_bytes()).hexdigest();health_fixtures.append(fixture)
    until=time.monotonic()+1.2
    while time.monotonic()<until:
     if child.poll() is not None:raise RuntimeError('Tray exited on '+fixture)
     if any(title and user32.IsWindowVisible(hwnd) for hwnd,title in windows(desktop,child.pid)):raise RuntimeError('Tray error dialog on '+fixture)
     time.sleep(.05)
  gui_pid=gui_child[0] if gui_child else child.pid
  for hwnd,_ in windows(desktop,gui_pid):user32.PostMessageW(hwnd,0x0010,0,0) # exact owned GUI, WM_CLOSE
  if gui_child:
   if kernel32.WaitForSingleObject(gui_child[1],10000)!=0:raise RuntimeError('Owned WPF child close timeout')
   if not user32.PostThreadMessageW(child.tid,0x0012,0,0):raise ctypes.WinError(ctypes.get_last_error()) # owned tray UI thread WM_QUIT, normal Run/Dispose return
  exit_code=child.wait()
  if exit_code!=0:raise RuntimeError('GUI exit failed: '+str(exit_code))
  if inventory(root)!=before:raise RuntimeError('Startup/close modified synthetic installation')
  records.append(dict(mode=mode,pid=child.pid,gui_child_pid=gui_child[0] if gui_child else None,title=title,exit_code=exit_code,startup_no_writes=True,clean_shutdown=True,isolated_hidden_desktop=True,health_fixtures=health_fixtures))
  print('PASS actual packaged executable',mode,': startup/no writes/clean close; owned PID',child.pid,flush=True)
 finally:
  if gui_child:
   if kernel32.WaitForSingleObject(gui_child[1],0)!=0:kernel32.TerminateProcess(gui_child[1],98);kernel32.WaitForSingleObject(gui_child[1],5000)
   kernel32.CloseHandle(gui_child[1])
  if child:child.close()
  user32.CloseDesktop(desktop)
(work/'result.json').write_text(json.dumps(dict(result='PASS',cases=records,gui_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),helper_sha256=hashlib.sha256(helper.read_bytes()).hexdigest(),manual_visual='NOT_RUN',hardware='NOT_RUN',network='NOT_RUN'),ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS packaged executable smoke: 5 cases including actual tray + WPF child; manual visual/hardware/network NOT_RUN')
