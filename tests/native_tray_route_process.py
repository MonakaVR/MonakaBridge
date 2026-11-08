"""Current native WM_COMMAND/menu/resources + isolated public-contract UDP; no HIL."""
from pathlib import Path
import argparse,copy,ctypes,hashlib,json,os,select,socket,subprocess,time,uuid
from ctypes import wintypes as w

p=argparse.ArgumentParser()
for name in ['service','gui','helper','out']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1]
out=a.out.resolve()/uuid.uuid4().hex;out.mkdir(parents=True)
service,gui,helper=[getattr(a,name).resolve() for name in ['service','gui','helper']]
u=ctypes.WinDLL('user32',use_last_error=True);k=ctypes.WinDLL('kernel32',use_last_error=True);shell=ctypes.WinDLL('shell32',use_last_error=True)
callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
u.EnumWindows.argtypes=[callback,w.LPARAM];u.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
u.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int];u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=w.LPARAM
u.GetSubMenu.argtypes=[w.HMENU,ctypes.c_int];u.GetSubMenu.restype=w.HMENU
u.GetMenuItemCount.argtypes=[w.HMENU];u.GetMenuItemID.argtypes=[w.HMENU,ctypes.c_int];u.GetMenuItemID.restype=w.UINT
u.GetMenuState.argtypes=[w.HMENU,w.UINT,w.UINT];u.GetMenuState.restype=w.UINT
u.GetMenuStringW.argtypes=[w.HMENU,w.UINT,w.LPWSTR,ctypes.c_int,w.UINT]
u.EndMenu.argtypes=[];u.RegisterWindowMessageW.argtypes=[w.LPCWSTR];u.RegisterWindowMessageW.restype=w.UINT
k.LoadLibraryExW.argtypes=[w.LPCWSTR,w.HANDLE,w.DWORD];k.LoadLibraryExW.restype=w.HMODULE;k.FreeLibrary.argtypes=[w.HMODULE]
u.LoadImageW.argtypes=[w.HINSTANCE,w.LPCWSTR,w.UINT,ctypes.c_int,ctypes.c_int,w.UINT];u.LoadImageW.restype=w.HANDLE;u.DestroyIcon.argtypes=[w.HICON]
k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE];k.CreateFileW.restype=w.HANDLE;k.CloseHandle.argtypes=[w.HANDLE]
class ProcessEntry(ctypes.Structure):_fields_=[('size',w.DWORD),('usage',w.DWORD),('pid',w.DWORD),('heap',ctypes.c_size_t),('module',w.DWORD),('threads',w.DWORD),('parent',w.DWORD),('priority',w.LONG),('flags',w.DWORD),('exe',w.WCHAR*260)]
k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE;k.Process32FirstW.argtypes=[w.HANDLE,ctypes.POINTER(ProcessEntry)];k.Process32NextW.argtypes=[w.HANDLE,ctypes.POINTER(ProcessEntry)]
class IconId(ctypes.Structure):_fields_=[('cbSize',w.DWORD),('hWnd',w.HWND),('uID',w.UINT),('guidItem',ctypes.c_byte*16)]
shell.Shell_NotifyIconGetRect.argtypes=[ctypes.POINTER(IconId),ctypes.POINTER(w.RECT)];shell.Shell_NotifyIconGetRect.restype=ctypes.c_long
class Notify(ctypes.Structure):
 _fields_=[('cbSize',w.DWORD),('hWnd',w.HWND),('uID',w.UINT),('uFlags',w.UINT),('uCallbackMessage',w.UINT),('hIcon',w.HICON),('szTip',w.WCHAR*128),('dwState',w.DWORD),('dwStateMask',w.DWORD),('szInfo',w.WCHAR*256),('uVersion',w.UINT),('szInfoTitle',w.WCHAR*64),('dwInfoFlags',w.DWORD),('guidItem',ctypes.c_byte*16),('hBalloonIcon',w.HICON)]
shell.Shell_NotifyIconW.argtypes=[w.DWORD,ctypes.POINTER(Notify)]
records=[];processes=[];streams=[];sockets=[]
def record(name,**data):records.append(dict(name=name,**data));(out/'events.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
def until(fn,seconds=10):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  value=fn()
  if value:return value
  time.sleep(.03)
 raise AssertionError('Timed out: '+str(fn))
def windows(pid,klass):
 found=[]
 @callback
 def each(hwnd,_):
  owner=w.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(owner));name=ctypes.create_unicode_buffer(256);u.GetClassNameW(hwnd,name,256)
  if (pid is None or owner.value==pid) and name.value.startswith(klass):found.append(hwnd)
  return True
 u.EnumWindows(each,0);return found
def command(config,op):
 r=subprocess.run([str(service),'--owner-command',str(config),op],capture_output=True,timeout=20,creationflags=subprocess.CREATE_NO_WINDOW)
 assert r.returncode==0,(r.stdout,r.stderr)
 return r.stdout.decode(errors='replace')
def owned_gui_window(pid):
 snapshot=k.CreateToolhelp32Snapshot(2,0);assert snapshot not in [None,ctypes.c_void_p(-1).value]
 try:
  entry=ProcessEntry();entry.size=ctypes.sizeof(entry);more=k.Process32FirstW(snapshot,ctypes.byref(entry))
  while more:
   if entry.parent==pid and entry.exe.lower()==gui.name.lower():
    found=windows(entry.pid,'HwndWrapper')
    if found:return found
   more=k.Process32NextW(snapshot,ctypes.byref(entry))
 finally:k.CloseHandle(snapshot)
def listener():
 s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1);s.bind(('127.0.0.1',0));s.setblocking(False);sockets.append(s);return s
def start(name,policy='disabled',revision=10,readonly=False,occupied=False):
 folder=out/name;folder.mkdir();config=folder/'configuration 日本語.json'
 doc=dict(namespace='monaka.bridge',version=2,bridge_id='native-tray-'+name,mapping_revision=revision,policy=policy,unknown_extension={'preserve':[1,2,'opaque']},profiles={'explicit':dict(convention='rh_y_up_neg_z_forward',position_axes=[1,2,3],quaternion_axes=[1,2,3,4],opaque_profile=['retained'])},mappings=[])
 for source in ['synthetic-alpha','unrecognized-beta']:
  doc['mappings'].append(dict(source_id=source,device_id='opaque-device',tracker_id='logical-'+source,profile='explicit',input_space='synthetic-space',input_revision=1,world_space='synthetic-world',world_revision=0,unknown_mapping={'keep':True},runtime_contract=dict(id='explicit-synthetic',provenance='Software public-contract fixture; no physical approval',input_policy='STRICT',output_policy='STRICT',input_declaration=['position','orientation'],output_declaration=['position','orientation'])))
 config.write_text(json.dumps(doc),encoding='utf-8');held=listener();ingress=held.getsockname()[1]
 if not occupied:held.close()
 outputs=[listener() for _ in range(3)];ports=[ingress]+[s.getsockname()[1] for s in outputs]
 log=(folder/'stdout.log').open('wb');err=(folder/'stderr.log').open('wb');streams.extend([log,err])
 child=subprocess.Popen([str(service),'--native-owner',str(config),str(gui),str(helper),*map(str,ports)]+(['--read-only'] if readonly else []),stdout=log,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
 processes.append((child,config));hwnd=until(lambda:windows(child.pid,'MonakaBridgeNativeOwnerV1'))[0]
 until(lambda:'RUNNING' in command(config,'Status') if not occupied else 'UNCONFIRMED' in command(config,'Status'))
 return child,config,hwnd,outputs,ingress
def send(hwnd,id):assert u.SendMessageW(hwnd,0x111,id,0)==0
def menu(child,hwnd):
 assert u.PostMessageW(hwnd,0x8000+43,0,0x205)
 popup=until(lambda:windows(child.pid,'#32768'))[0];handle=u.SendMessageW(popup,0x1e1,0,0);assert handle
 result=[]
 try:
  for i in range(u.GetMenuItemCount(handle)):
   submenu=u.GetSubMenu(handle,i)
   if not submenu:continue
   rows=[]
   for j in range(u.GetMenuItemCount(submenu)):
    text=ctypes.create_unicode_buffer(512);u.GetMenuStringW(submenu,j,text,512,0x400);rows.append(dict(id=u.GetMenuItemID(submenu,j),text=text.value,state=u.GetMenuState(submenu,j,0x400)))
   if any(x['id']==21 for x in rows):result=rows;break
  assert result
 finally:
  # End only the owned process's active popup via the owning window.
  u.PostMessageW(hwnd,0x1f,0,0)
  until(lambda:not windows(child.pid,'#32768'))
 return result
def icon_present(hwnd):return shell.Shell_NotifyIconGetRect(ctypes.byref(IconId(ctypes.sizeof(IconId),hwnd,1)),ctypes.byref(w.RECT()))==0
def health(config,policy,revision):
 try:
  j=json.loads(Path(str(config)+'.status.json').read_bytes());return j if j['policy']==policy and j['mapping_revision']==revision else False
 except (OSError,ValueError):return False
def collect(outputs,seconds=.2):
 rows=[[] for _ in outputs];end=time.monotonic()+seconds
 while time.monotonic()<end:
  for sock in select.select(outputs,[],[],max(0,end-time.monotonic()))[0]:
   try:rows[outputs.index(sock)].append(json.loads(sock.recv(65536)))
   except BlockingIOError:pass
 return rows
status='FAIL'
try:
 module=k.LoadLibraryExW(str(service),None,2|0x20);assert module
 try:
  for resource in [201,202]:
   icon=u.LoadImageW(module,ctypes.cast(ctypes.c_void_p(resource),w.LPCWSTR),1,16,16,0);assert icon;assert u.DestroyIcon(icon)
 finally:k.FreeLibrary(module)
 record('PASS_actual_embedded_black_white_resources_load')
 child,config,hwnd,outputs,ingress=start('normal','both');initial=json.loads(config.read_bytes());initial_bytes=config.read_bytes()
 rows=menu(child,hwnd);assert [x['text'] for x in rows if x['id'] in [21,22,23]]==['MonakaVR','SteamVR','Disabled'];assert not any(x['state']&8 for x in rows);assert any('Legacy Both' in x['text'] for x in rows);assert config.read_bytes()==initial_bytes
 record('PASS_actual_menu_legacy_both_no_selection',rows=rows)
 until(lambda:icon_present(hwnd));taskbar=u.RegisterWindowMessageW('TaskbarCreated')
 notify=Notify();notify.cbSize=ctypes.sizeof(notify);notify.hWnd=hwnd;notify.uID=1;assert shell.Shell_NotifyIconW(2,ctypes.byref(notify));assert not icon_present(hwnd);u.SendMessageW(hwnd,taskbar,0,0);until(lambda:icon_present(hwnd));assert child.poll() is None
 record('PASS_TaskbarCreated_missing_shell_icon_recovery_same_owner',pid=child.pid)
 fixture=json.loads((root/'third_party/monaka-protocol-v2/fixtures/v2/observation.json').read_bytes());sender=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sockets.append(sender)
 fixture.update(capabilities=['position','orientation'],position=[1,2,3],orientation=[0,0,0,1],linear_velocity=None,angular_velocity=None,linear_acceleration=None,coordinate_space=dict(id='synthetic-space',convention='rh_y_up_neg_z_forward',revision=1),timestamp_ns='1000',sent_at_ns='1000')
 for seq,(id,policy) in enumerate([(21,'monaka'),(22,'steamvr'),(23,'disabled')],1):
  before=json.loads(config.read_bytes());send(hwnd,id);after=json.loads(config.read_bytes());expected=copy.deepcopy(before);expected.update(policy=policy,mapping_revision=before['mapping_revision']+1);assert after==expected
  until(lambda:health(config,policy,after['mapping_revision']));collect(outputs)
  for source in ['synthetic-alpha','unrecognized-beta']:
   row=copy.deepcopy(fixture);row.update(source_id=source,device_id='opaque-device',sequence=str(seq));sender.sendto(json.dumps(row).encode(),('127.0.0.1',ingress))
  received=collect(outputs);poses=[[r for r in rows if r['type']=='pose'] for rows in received]
  assert len(poses[0])==(2 if policy=='monaka' else 0),received;assert len(poses[1])==(2 if policy=='steamvr' else 0),received;assert len(poses[2])==2,received
  active=poses[0] or poses[1]
  if active:assert active[0]['position']==active[1]['position']==[1,2,3] and active[0]['orientation']==active[1]['orientation']==[0,0,0,1]
  rows=menu(child,hwnd);assert [x['id'] for x in rows if x['state']&8]==[id]
  assert child.poll() is None and 'runtime reload unconfirmed' in command(config,'Status')
  record('PASS_actual_WM_COMMAND_save_reload_route_and_opaque_identity_equivalence',policy=policy,revision=after['mapping_revision'],pid=child.pid,poses=[len(x) for x in poses])
 # An actual open WPF keeps its stale draft; the product client's CAS rejects
 # that snapshot after the tray selection, without closing/discarding the GUI.
 stale=out/'stale-gui-snapshot.json';stale.write_bytes(config.read_bytes());command(config,'Open');until(lambda:owned_gui_window(child.pid))
 send(hwnd,22);assert json.loads(config.read_bytes())['policy']=='steamvr'
 client=out/'bridge_ui_tests.exe';local_gui=out/'monaka_bridge_control.exe';local_gui.write_bytes(gui.read_bytes())
 compiler=Path(os.environ['WINDIR'])/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
 built=subprocess.run([str(compiler),'/nologo','/utf8output','/target:exe','/out:'+str(client),'/reference:System.dll','/reference:System.Core.dll','/reference:System.Web.Extensions.dll','/reference:'+str(local_gui),str(root/'tests/native_tray_stale_gui_tests.cs')],capture_output=True,timeout=30,creationflags=subprocess.CREATE_NO_WINDOW);assert built.returncode==0,(built.stdout,built.stderr)
 checked=subprocess.run([str(client),str(config),str(helper),str(stale)],capture_output=True,timeout=30,creationflags=subprocess.CREATE_NO_WINDOW);assert checked.returncode==0,(checked.stdout,checked.stderr)
 command(config,'Close');record('PASS_live_GUI_route_and_actual_stale_ConfigHelperClient_rejected',stdout=checked.stdout.decode(errors='replace'))
 status='PASS'
finally:
 for child,config in processes:
  if child.poll() is None:
   try:command(config,'Exit');child.wait(10)
   except Exception as e:record('FAIL_cleanup',pid=child.pid,error=str(e));child.terminate();child.wait(10)
 for stream in streams:stream.close()
 for sock in sockets:sock.close()
 result=dict(status=status,verification_level='ACTUAL_NATIVE_WM_COMMAND_AND_ISOLATED_PUBLIC_UDP',physical_hil='NOT_RUN',cutover='NOT_RUN',source_head=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [service,gui,helper]},events=records)
 (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(dict(status=status,evidence=str(out))))
