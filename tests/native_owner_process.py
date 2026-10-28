"""Actual native owner/runtime/Win32 tray/WPF Job. Synthetic configs; no hardware."""
from pathlib import Path
import argparse,ctypes,hashlib,json,os,socket,subprocess,time,uuid
from ctypes import wintypes
p=argparse.ArgumentParser();p.add_argument('--service',type=Path,required=True);p.add_argument('--gui',type=Path,required=True);p.add_argument('--helper',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve()/uuid.uuid4().hex;out.mkdir(parents=True);root=Path(__file__).resolve().parents[1]
service=a.service.resolve();gui=a.gui.resolve();helper=a.helper.resolve();records=[];processes=[];streams=[]
def record(name,**data):records.append(dict(name=name,**data));(out/'events.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
def ports():
    sockets=[]
    try:
        for _ in range(4):
            s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('127.0.0.1',0));sockets.append(s)
        return [s.getsockname()[1] for s in sockets]
    finally:
        for s in sockets:s.close()
def free(port):
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
        s.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
        try:s.bind(('127.0.0.1',port));return True
        except OSError:return False
def until(predicate,seconds=10):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        result=predicate()
        if result:return result
        time.sleep(.05)
    raise AssertionError('Timed out: '+str(predicate))
class Entry(ctypes.Structure):
    _fields_=[('size',wintypes.DWORD),('usage',wintypes.DWORD),('pid',wintypes.DWORD),('heap',ctypes.c_size_t),('module',wintypes.DWORD),('threads',wintypes.DWORD),('parent',wintypes.DWORD),('priority',wintypes.LONG),('flags',wintypes.DWORD),('exe',wintypes.WCHAR*260)]
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.CreateToolhelp32Snapshot.argtypes=[wintypes.DWORD,wintypes.DWORD];k.CreateToolhelp32Snapshot.restype=wintypes.HANDLE
k.Process32FirstW.argtypes=[wintypes.HANDLE,ctypes.POINTER(Entry)];k.Process32NextW.argtypes=[wintypes.HANDLE,ctypes.POINTER(Entry)]
k.CloseHandle.argtypes=[wintypes.HANDLE]
k.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];k.OpenProcess.restype=wintypes.HANDLE
k.TerminateProcess.argtypes=[wintypes.HANDLE,wintypes.UINT];k.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD]
def retained_gui(owner):
    found=until(lambda:children(owner.pid));assert len(found)==1 and found[0]['name'].lower()==gui.name.lower(),found
    handle=k.OpenProcess(0x100001,False,found[0]['pid']);assert handle
    return found,handle
def gone(handle):return k.WaitForSingleObject(handle,0)==0
class ThreadEntry(ctypes.Structure):
    _fields_=[('size',wintypes.DWORD),('usage',wintypes.DWORD),('tid',wintypes.DWORD),('owner',wintypes.DWORD),('base',wintypes.LONG),('delta',wintypes.LONG),('flags',wintypes.DWORD)]
k.Thread32First.argtypes=[wintypes.HANDLE,ctypes.POINTER(ThreadEntry)];k.Thread32Next.argtypes=[wintypes.HANDLE,ctypes.POINTER(ThreadEntry)]
k.OpenThread.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];k.OpenThread.restype=wintypes.HANDLE
k.GetThreadTimes.argtypes=[wintypes.HANDLE,*([ctypes.POINTER(wintypes.FILETIME)]*4)]
k.GetProcessIdOfThread.argtypes=[wintypes.HANDLE];k.SuspendThread.argtypes=[wintypes.HANDLE];k.SuspendThread.restype=wintypes.DWORD
def suspend_owned_main(child):
    # Process fault injection only: retain earliest native thread by exact owned
    # PID, verify its process again, then suspend it. No external window/UI action.
    snapshot=k.CreateToolhelp32Snapshot(4,0);threads=[]
    try:
        entry=ThreadEntry();entry.size=ctypes.sizeof(entry);more=k.Thread32First(snapshot,ctypes.byref(entry))
        while more:
            if entry.owner==child.pid:
                handle=k.OpenThread(0x42,False,entry.tid)
                if handle:
                    dates=[wintypes.FILETIME() for _ in range(4)];assert k.GetThreadTimes(handle,*map(ctypes.byref,dates));threads.append((dates[0].dwHighDateTime<<32|dates[0].dwLowDateTime,entry.tid,handle))
            more=k.Thread32Next(snapshot,ctypes.byref(entry))
        assert threads and child.poll() is None
        _,tid,handle=min(threads);assert k.GetProcessIdOfThread(handle)==child.pid;assert k.SuspendThread(handle)!=0xffffffff
        record('inject_owned_native_main_thread_hang',pid=child.pid,tid=tid)
    finally:
        k.CloseHandle(snapshot)
        for _,_,handle in threads:k.CloseHandle(handle)
def children(pid):
    handle=k.CreateToolhelp32Snapshot(2,0);assert handle not in [None,ctypes.c_void_p(-1).value]
    try:
        entry=Entry();entry.size=ctypes.sizeof(entry);more=k.Process32FirstW(handle,ctypes.byref(entry));result=[]
        while more:
            if entry.parent==pid:result.append(dict(pid=entry.pid,name=entry.exe))
            more=k.Process32NextW(handle,ctypes.byref(entry))
        return result
    finally:k.CloseHandle(handle)
def command(config,op,ok=True):
    cmd=[str(service),'--owner-command',str(config),op];r=subprocess.run(cmd,capture_output=True,timeout=25,creationflags=subprocess.CREATE_NO_WINDOW)
    record('command',command=cmd,exit=r.returncode,stdout=r.stdout.decode(errors='replace'),stderr=r.stderr.decode(errors='replace'))
    assert (r.returncode==0)==ok,records[-1]
    return r.stdout.decode(errors='replace')
def start(label):
    folder=out/label;folder.mkdir();config=folder/'configuration 日本語.json';document=json.loads((root/'config/bridge.example.json').read_text(encoding='utf-8-sig'));document.update(policy='disabled',bridge_id='native-owner-'+label,mapping_revision=10);config.write_text(json.dumps(document),encoding='utf-8');allocated=ports()
    env=os.environ.copy()
    for key in ['LOCALAPPDATA','APPDATA','TEMP','TMP']:
        isolated=folder/key.lower();isolated.mkdir();env[key]=str(isolated)
    stdout=(folder/'stdout.log').open('wb');stderr=(folder/'stderr.log').open('wb');streams.extend([stdout,stderr]);args=[str(service),'--native-owner',str(config),str(gui),str(helper),*map(str,allocated),'--tray-only'];child=subprocess.Popen(args,stdout=stdout,stderr=stderr,env=env,creationflags=subprocess.CREATE_NO_WINDOW);processes.append((child,config));record('launch',pid=child.pid,args=args)
    until(lambda:child.poll() is not None or not free(allocated[0]));assert child.poll() is None,(folder/'stderr.log').read_text(errors='replace');return child,config,allocated,args
status='FAIL'
try:
    first,config,pp,args=start('one');second,other,qq,_=start('two');firstChildren=children(first.pid);secondChildren=children(second.pid);record('initial_process_tree',first=first.pid,first_children=firstChildren,second=second.pid,second_children=secondChildren);assert firstChildren==[] and secondChildren==[];record('PASS_single_native_resident_per_installation',first=first.pid,second=second.pid)
    assert 'RUNNING' in command(config,'Start');assert children(first.pid)==[]
    assert 'STOPPED' in command(config,'Stop');assert free(pp[0]) and not free(qq[0]) and first.poll() is None and second.poll() is None;record('PASS_stop_release_peer_preservation')
    assert 'RUNNING' in command(config,'Start');assert not free(pp[0]);assert 'RUNNING' in command(config,'Restart');record('PASS_restart_same_native_owner',pid=first.pid)
    client=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(root/'tests/run_native_owner_client.ps1'),'-Gui',str(gui),'-Service',str(service),'-Config',str(config),'-OutputDirectory',str(out/'managed-client')],capture_output=True,timeout=45,creationflags=subprocess.CREATE_NO_WINDOW)
    record('product_WPF_client',exit=client.returncode,stdout=client.stdout.decode(errors='replace'),stderr=client.stderr.decode(errors='replace'));assert client.returncode==0,records[-1]
    command(config,'Bogus',ok=False);absent=out/'unowned.json';absent.write_bytes(config.read_bytes());command(absent,'Stop',ok=False);assert not free(pp[0]);record('PASS_invalid_and_unowned_target_fail_closed')
    standalone=subprocess.run([str(gui),'--native-owner-window','--control-window-only','--config',str(config),'--read-only'],capture_output=True,timeout=10,creationflags=subprocess.CREATE_NO_WINDOW);assert standalone.returncode==1;record('PASS_native_GUI_without_owner_handshake_refused')
    command(config,'Open');guiChildren,guiHandle=retained_gui(first);initialGui=guiChildren[0]['pid'];time.sleep(1)
    duplicate=subprocess.run(args,capture_output=True,timeout=10,creationflags=subprocess.CREATE_NO_WINDOW);assert duplicate.returncode==0,duplicate.stderr;time.sleep(.25);assert children(first.pid)==guiChildren;record('PASS_duplicate_launch_reopens_same_owned_WPF',gui_pid=initialGui)
    command(config,'Close');until(lambda:gone(guiHandle));k.CloseHandle(guiHandle);until(lambda:children(first.pid)==[]);assert first.poll() is None and not free(pp[0]);record('PASS_owned_WPF_graceful_close_keeps_runtime')
    command(config,'Open');_,guiHandle=retained_gui(first);assert k.TerminateProcess(guiHandle,82);until(lambda:gone(guiHandle));k.CloseHandle(guiHandle);assert first.poll() is None and not free(pp[0]);record('PASS_owned_GUI_crash_keeps_native_runtime')
    command(config,'Open');_,guiHandle=retained_gui(first);time.sleep(.5)
    command(config,'Exit');assert first.wait(10)==0;assert free(pp[0]);assert second.poll() is None and not free(qq[0]);record('PASS_exit_owned_WPF_job_and_runtime_peer_preserved')
    assert gone(guiHandle);k.CloseHandle(guiHandle)
    command(other,'Open');_,guiHandle=retained_gui(second);time.sleep(.5);second.terminate();second.wait(10);until(lambda:gone(guiHandle));k.CloseHandle(guiHandle);assert free(qq[0]);record('PASS_native_crash_reaps_owned_GUI_job_and_udp')
    command(other,'Start',ok=False);record('PASS_exit_releases_identity_and_udp')
    hung,hungConfig,hungPorts,_=start('watchdog');command(hungConfig,'Open');_,guiHandle=retained_gui(hung);time.sleep(.5);suspend_owned_main(hung);assert hung.wait(15)==71;until(lambda:gone(guiHandle));k.CloseHandle(guiHandle);assert free(hungPorts[0]);record('PASS_real_native_main_thread_hang_watchdog_reaps_GUI_and_udp')
    status='PASS'
finally:
    for child,config in processes:
        if child.poll() is None:
            try:command(config,'Exit');child.wait(10)
            except Exception as error:record('FAIL_cleanup',error=str(error),pid=child.pid);child.terminate();child.wait(10)
    for stream in streams:stream.close()
    result={'status':status,'verification_level':'REAL_OS_NO_HARDWARE','source_sha':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),'source_dirty':bool(subprocess.check_output(['git','-C',str(root),'status','--porcelain=v1'])),'physical_hil':'NOT_RUN','evidence':str(out),'hashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [service,gui,helper]},'events':records}
    (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({k:result[k] for k in ['status','evidence','source_dirty']}))
