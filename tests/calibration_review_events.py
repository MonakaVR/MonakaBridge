"""Parent-only observer for the test fixture's Event/QPC telemetry."""
from pathlib import Path
import ctypes,ctypes.wintypes as w,time
KEYS=('reader-paused','writer-paused','worker-stopped','writer-stop-requested','writer-failed','writer-finished','writer-deadline-expired','writer-call-ready','release-reader','release-writer','trigger-write')
class State(ctypes.Structure):
 _fields_=[('magic',w.LONG),('version',w.LONG),('count',w.LONG),('writerTid',w.LONG),('workerTid',w.LONG),('lastWriteError',w.LONG),('frequency',ctypes.c_longlong),('points',ctypes.c_longlong*11)]
assert ctypes.sizeof(State)==120 and State.points.offset==32
k=ctypes.WinDLL('kernel32',use_last_error=True)
def api(name,args,result):
 f=getattr(k,name);f.argtypes=args;f.restype=result;return f
open_event=api('OpenEventW',[w.DWORD,w.BOOL,w.LPCWSTR],w.HANDLE)
set_event=api('SetEvent',[w.HANDLE],w.BOOL)
wait_one=api('WaitForSingleObject',[w.HANDLE,w.DWORD],w.DWORD)
wait_many=api('WaitForMultipleObjects',[w.DWORD,ctypes.POINTER(w.HANDLE),w.BOOL,w.DWORD],w.DWORD)
close=api('CloseHandle',[w.HANDLE],w.BOOL)
open_map=api('OpenFileMappingW',[w.DWORD,w.BOOL,w.LPCWSTR],w.HANDLE)
map_view=api('MapViewOfFile',[w.HANDLE,w.DWORD,w.DWORD,w.DWORD,ctypes.c_size_t],ctypes.c_void_p)
unmap=api('UnmapViewOfFile',[ctypes.c_void_p],w.BOOL)
open_thread=api('OpenThread',[w.DWORD,w.BOOL,w.DWORD],w.HANDLE)
thread_pid=api('GetProcessIdOfThread',[w.HANDLE],w.DWORD)
thread_exit=api('GetExitCodeThread',[w.HANDLE,ctypes.POINTER(w.DWORD)],w.BOOL)
thread_times=api('GetThreadTimes',[w.HANDLE,*([ctypes.POINTER(w.FILETIME)]*4)],w.BOOL)
process_times=api('GetProcessTimes',[w.HANDLE,*([ctypes.POINTER(w.FILETIME)]*4)],w.BOOL)
def require(ok,why):
 if not ok:raise RuntimeError(why+' winerror='+str(ctypes.get_last_error()))
def filetime(x):return (x.dwHighDateTime<<32)|x.dwLowDateTime
def times(handle,function):
 c,e,a,b=w.FILETIME(),w.FILETIME(),w.FILETIME(),w.FILETIME();require(function(handle,ctypes.byref(c),ctypes.byref(e),ctypes.byref(a),ctypes.byref(b)),'GET_TIMES')
 return dict(creation=filetime(c),exit=filetime(e),kernel=filetime(a),user=filetime(b))
class ReviewObserver:
 def __init__(self,p,descriptor):
  self.p=p;self.events={};self.threads={};self.mapping=None;self.view=None;self.controls=[]
  require(descriptor['version']==1 and descriptor['state_bytes']==120 and set(descriptor['events'])==set(KEYS),'REVIEW_DESCRIPTOR')
  try:
   for name,path in descriptor['events'].items():
    h=open_event(0x100000|2,False,path);require(h,'OPEN_EVENT '+name);self.events[name]=h
   self.mapping=open_map(4,False,descriptor['mapping']);require(self.mapping,'OPEN_MAPPING');self.view=map_view(self.mapping,4,0,0,120);require(self.view,'MAP_VIEW');self.state=State.from_address(self.view)
   require(self.state.magic==0x42324556 and self.state.version==1 and self.state.count==11 and self.state.frequency>0,'REVIEW_STATE')
  except: self.close();raise
 def wait(self,name,seconds=6):
  handles=(w.HANDLE*2)(self.events[name],int(self.p._handle));r=wait_many(2,handles,False,int(seconds*1000))
  if r!=0:raise RuntimeError('EVENT_WAIT '+name+' result='+str(r)+' child_exit='+str(self.p.poll()))
 def signal(self,name):
  require(set_event(self.events[name]),'SET_EVENT '+name);self.controls.append(dict(name=name,observed_ns=time.perf_counter_ns()))
 def signaled(self,name):return wait_one(self.events[name],0)==0
 def bind_threads(self):
  for role,tid in [('writer',self.state.writerTid),('worker',self.state.workerTid)]:
   require(tid>0,'THREAD_ID '+role);h=open_thread(0x100000|0x800,False,tid);require(h,'OPEN_THREAD '+role)
   if thread_pid(h)!=self.p.pid:close(h);raise RuntimeError('FOREIGN_THREAD '+role)
   self.threads[role]=(tid,h)
 def snapshot(self):
  threads={}
  for role,(tid,h) in self.threads.items():
   code=w.DWORD();require(thread_exit(h,ctypes.byref(code)),'THREAD_EXIT '+role);threads[role]=dict(tid=tid,wait_zero=wait_one(h,0),exit_code=code.value,times=times(h,thread_times))
  return dict(points={name:int(self.state.points[i]) for i,name in enumerate(KEYS)},frequency=int(self.state.frequency),events={name:self.signaled(name) for name in KEYS},threads=threads,process_times=times(int(self.p._handle),process_times),ambient_write_error=int(self.state.lastWriteError),parent_controls=self.controls)
 def markers(self,root):
  result=[]
  for name in KEYS:
   if self.signaled(name):
    path=Path(root)/(name+'.fixture');path.write_text(name,encoding='utf-8');require(path.read_text(encoding='utf-8')==name,'OBSERVER_MARKER '+name);s=path.stat();result.append(dict(name=name,contents=name,bytes=s.st_size,mtime_ns=s.st_mtime_ns))
  return result
 def close(self):
  for _,h in self.threads.values():close(h)
  self.threads.clear()
  if self.view:unmap(self.view);self.view=None
  if self.mapping:close(self.mapping);self.mapping=None
  for h in self.events.values():close(h)
  self.events.clear()
