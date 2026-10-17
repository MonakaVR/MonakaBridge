using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace MonakaBridge {
 internal sealed class CalibrationClientSnapshot {
  internal string Phase,Fault,ActualOutcome,Progress,LastNative;
  internal bool Busy,Stopping,Stopped,CommitMayHaveStarted,CanPrepare,CanCommit;
  internal CalibrationNativeResult Result;internal CalibrationPreparedCandidate Prepared;
 }
 internal sealed class CalibrationProcessClient:IDisposable {
  internal delegate CalibrationOwnedProcess Launcher(string executable,string arguments,bool version);
  sealed class Request {
   internal string Id,Op,Canonical;internal byte[] Bytes;internal long SentAt,Deadline;internal bool Written,Acknowledged;
   internal readonly TaskCompletionSource<Dictionary<string,object>> Reply=new TaskCompletionSource<Dictionary<string,object>>(TaskCreationOptions.RunContinuationsAsynchronously);
  }
  readonly CalibrationInstallation profile;readonly bool readOnly;readonly Launcher launch;readonly Func<long> clock;
  readonly Action<Stream,byte[],string> write;readonly object gate=new object(),writerGate=new object();
  readonly SemaphoreSlim ordinary=new SemaphoreSlim(1,1);readonly Dictionary<string,Request> ledger=new Dictionary<string,Request>(StringComparer.Ordinal);
  readonly Queue<string> controls=new Queue<string>();readonly AutoResetEvent wake=new AutoResetEvent(false);
  readonly TaskCompletionSource<bool> stopped=new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
  readonly MemoryStream stderr=new MemoryStream();readonly string prefix=Guid.NewGuid().ToString("N");
  CalibrationOwnedProcess child,bootstrap;Thread outputThread,errorThread,watchThread;
  Request active,cancelRequest,closeRequest;long lastNow,workerStart,versionDeadline,stopDeadline,drainDeadline,confirmDeadline,resultDeadline,reviewDeadline,phaseDeadline,cacheBytes;
  long sequence;int ordinaryCount;string session,run,confirmation,openHash,phase="NEW",fault,actualOutcome,progress,lastNative,terminalNotification;
  CalibrationNativeResult result;CalibrationPreparedCandidate prepared;
  bool haveTime,started,stopRequested,closed,stdoutDone,stderrDone,mayCommit,commitUnconfirmed,recordPhase,solvePhase,watchDone,disposed,launchPending;

  internal CalibrationProcessClient(CalibrationInstallation selected,bool effectiveReadOnly):this(selected,effectiveReadOnly,null,null,null){}
  // Explicit library seams; never populated by profile or command-line flags.
  internal CalibrationProcessClient(CalibrationInstallation selected,bool effectiveReadOnly,Launcher factory,Func<long> monotonic,Action<Stream,byte[],string> writer) {
   if(selected==null)throw new ArgumentNullException("selected");profile=selected;readOnly=selected.ReadOnly||effectiveReadOnly;
   launch=factory??delegate(string path,string args,bool version){return CalibrationOwnedProcess.Start(path,args,version,null,null);};
   var watch=Stopwatch.StartNew();clock=monotonic??delegate{return watch.ElapsedMilliseconds;};
   write=writer??delegate(Stream stream,byte[] bytes,string op){stream.Write(bytes,0,bytes.Length);stream.Flush();};
  }
  long Now(){long value=clock();if(value<0||(haveTime&&value<lastNow))throw new InvalidOperationException("MONOTONIC_CLOCK_ROLLBACK");haveTime=true;lastNow=value;return value;}
  static Thread Background(ThreadStart body){var thread=new Thread(body){IsBackground=true};thread.Start();return thread;}
  static bool Terminal(string value){return value=="SAVED"||value=="SAVED_CONFIG_EVIDENCE_STALE"||value=="CANCELLED"||value=="CLOSED"||value=="FAILED"||value=="INSPECT_ONLY"||value=="UNKNOWN";}
  static string Text(Dictionary<string,object> value,string key){return CalibrationJson.Text(value,key,4096);}
  static Dictionary<string,object> Map(Dictionary<string,object> value,string key){object item;if(!value.TryGetValue(key,out item)||!(item is Dictionary<string,object>))throw new InvalidOperationException("Native object required: "+key);return (Dictionary<string,object>)item;}
  static string Canonical(Dictionary<string,object> value){var copy=new SortedDictionary<string,object>(value,StringComparer.Ordinal);copy.Remove("event_sequence");copy.Remove("previous_emitted_sequence");copy.Remove("replayed");copy.Remove("current_phase");copy.Remove("eligible");copy.Remove("worker_lifetime_expired");return new System.Web.Script.Serialization.JavaScriptSerializer{MaxJsonLength=65536,RecursionLimit=16}.Serialize(copy);}
  void Notify(string text,bool terminal){if(terminal){if(terminalNotification!=null)throw new InvalidOperationException("Native terminal notification limit");terminalNotification=text;return;}if(controls.Count>=7)throw new InvalidOperationException("Native control notification limit");controls.Enqueue(text);}
  void QueueStopLocked(bool cancel) {
   if(session==null||child==null||closed)return;
   long now=lastNow;
   if(cancel&&!commitUnconfirmed&&cancelRequest==null){cancelRequest=Create("cancel",null,prefix+"-cancel",now,2000);Write(cancelRequest);}
   if(closeRequest==null){closeRequest=Create("close",null,prefix+"-close",now,commitUnconfirmed?15000:2000);Write(closeRequest);}
  }
  void FailLocked(string reason) {
   if(fault==null)fault=reason;stopRequested=true;prepared=null;confirmation=null;
   if(commitUnconfirmed){phase="UNKNOWN";actualOutcome=actualOutcome??"UNKNOWN";}else if(!Terminal(phase))phase="INSPECT_ONLY";
   foreach(var entry in ledger.Values)if(!entry.Acknowledged)entry.Reply.TrySetException(new InvalidOperationException(commitUnconfirmed?"UNKNOWN: "+reason:reason));
   if(stopDeadline==0)stopDeadline=lastNow+2000;
   try{QueueStopLocked(true);}catch{} // Remaining grace is bounded by the job watchdog.
  }
  void Fail(string reason){lock(gate){try{Now();}catch{}FailLocked(reason);}wake.Set();}

  internal async Task<Dictionary<string,object>> OpenAsync() {
   lock(gate){if(started)throw new InvalidOperationException("One native session per client");started=true;workerStart=Now();versionDeadline=workerStart+5000;phase="BOOTSTRAP";watchThread=Background(Watch);}
   try{
    await Task.Run((Action)Bootstrap).ConfigureAwait(false);profile.VerifyHelper();
    lock(gate){if(stopRequested)throw new InvalidOperationException("Calibration session was closed before open");phase="STARTING";phaseDeadline=Now()+5000;}
    lock(gate){launchPending=true;}
    CalibrationOwnedProcess process;try{process=launch(profile.CalibrationHelper,"--local-api",false);}catch{lock(gate){launchPending=false;}throw;}
    lock(gate){child=process;launchPending=false;if(!process.AssignedBeforeCommands){FailLocked("Owned job assignment required");throw new InvalidOperationException(fault);}if(stopRequested){stdoutDone=stderrDone=true;process.KillOwned();throw new InvalidOperationException("Calibration session closed during startup");}phase="NEW";phaseDeadline=0;}
    outputThread=Background(ReadOutput);errorThread=Background(ReadError);
    return await Command("open",profile.OpenRequest(readOnly),10000).ConfigureAwait(false);
   }catch(Exception e){Fail(e.Message);CalibrationOwnedProcess current;lock(gate){current=child??bootstrap;if(current==null){closed=true;stopped.TrySetResult(true);}}
    if(current!=null)try{current.KillOwned();}catch{}throw;
   }
  }
  void Bootstrap() {
   profile.VerifyHelper();lock(gate){launchPending=true;}
   CalibrationOwnedProcess process;try{process=launch(profile.CalibrationHelper,"--local-api-version",true);}catch{lock(gate){launchPending=false;}throw;}
   Exception errorFailure=null;
   lock(gate){bootstrap=process;launchPending=false;if(stopRequested)process.KillOwned();}
   var err=Background(delegate{try{CalibrationPipeFrames.Drain(process.Error,256*1024,null);}catch(Exception e){errorFailure=e;try{process.KillOwned();}catch{}}});
   try{
    byte[] bytes=CalibrationPipeFrames.ReadLine(process.Output,CalibrationContract.LineBytes);if(bytes==null)throw new InvalidOperationException("Calibration version unavailable");
    CalibrationContract.VerifyVersion(profile,bytes);
    if(process.Output.ReadByte()!=-1||!process.WaitForExit(1000)||process.ExitCode!=0)throw new InvalidOperationException("Invalid calibration version process outcome");
    if(!err.Join(1000)||errorFailure!=null)throw new InvalidOperationException("Calibration version stderr unavailable",errorFailure);
    lock(gate){if(stopRequested)throw new InvalidOperationException("Calibration bootstrap stopped");versionDeadline=0;}
   }finally{lock(gate){bootstrap=null;}process.Dispose();}
  }
  internal Task<Dictionary<string,object>> DiscoverAsync(){return Command("discover",null,10000);}
  internal Task<Dictionary<string,object>> ConfirmAsync(){return Command("confirm_target",null,10000);}
  internal Task<Dictionary<string,object>> BeginAsync(){Dictionary<string,object> fields;lock(gate){fields=new Dictionary<string,object>{{"confirmation_token",confirmation}};}return Command("begin",fields,30000);}
  internal Task<Dictionary<string,object>> PrepareAsync(){Dictionary<string,object> fields;lock(gate){if(readOnly)throw new InvalidOperationException("Read-only calibration cannot prepare/save");if(result==null)throw new InvalidOperationException("Native result required");fields=new Dictionary<string,object>{{"result_id",result.ResultId}};}return Command("prepare_commit",fields,10000);}
  internal Task<Dictionary<string,object>> CommitAsync(){Dictionary<string,object> fields;lock(gate){if(readOnly||prepared==null)throw new InvalidOperationException("Native writable prepared candidate required");fields=prepared.CommitFields();}return Command("commit",fields,15000);}
  void RequirePhase(string op,long now) {
   if(stopRequested||closed||fault!=null)throw new InvalidOperationException("Calibration session is inspect-only; close before a new measurement");
   bool okay=op=="open"?phase=="NEW":op=="discover"?phase=="OPEN":op=="confirm_target"?phase=="DISCOVERED":op=="begin"?phase=="CONFIRMED"&&confirmation!=null&&now<confirmDeadline:op=="prepare_commit"?(phase=="RESULT_READY"||phase=="PREPARED")&&result!=null&&now<resultDeadline:op=="commit"?phase=="PREPARED"&&prepared!=null&&now<reviewDeadline:false;
   if(!okay)throw new InvalidOperationException("Calibration action unavailable in "+phase);
  }
  async Task<Dictionary<string,object>> Command(string op,Dictionary<string,object> fields,int timeout) {
   if(!ordinary.Wait(0))throw new InvalidOperationException("Another calibration command is pending");
   try{Request entry;lock(gate){long now=Now();RequirePhase(op,now);if(ordinaryCount>=126)throw new InvalidOperationException("Calibration request limit; close and start explicitly");entry=Create(op,fields,prefix+"-"+(++ordinaryCount),now,timeout);active=entry;}
    Write(entry);return await entry.Reply.Task.ConfigureAwait(false);
   }finally{ordinary.Release();}
  }
  Request Create(string op,Dictionary<string,object> fields,string id,long now,int timeout) {
   var value=fields==null?new Dictionary<string,object>():new Dictionary<string,object>(fields);value["api_version"]=1;value["request_id"]=id;value["op"]=op;
   if(op!="open"){if(session==null||run==null)throw new InvalidOperationException("Native session identity unavailable");value["session_id"]=session;value["run_id"]=run;}
   byte[] payload=CalibrationJson.Encode(value,64*1024);if(cacheBytes+payload.Length>10*1024*1024-(op=="cancel"||op=="close"?0:2048))throw new InvalidOperationException("Calibration request/cache byte limit");
   byte[] bytes=new byte[payload.Length+1];Buffer.BlockCopy(payload,0,bytes,0,payload.Length);bytes[bytes.Length-1]=(byte)'\n';
   var entry=new Request{Id=id,Op=op,Bytes=bytes,SentAt=now,Deadline=now+timeout};ledger.Add(id,entry);cacheBytes+=payload.Length;return entry;
  }
  void Write(Request entry) {
   Task.Run(delegate{
    try{lock(writerGate){CalibrationOwnedProcess process;lock(gate){process=child;if(closed||process==null||(stopRequested&&entry.Op!="cancel"&&entry.Op!="close"))throw new InvalidOperationException("Command stopped before write");
      if(entry.Op=="commit"){mayCommit=true;commitUnconfirmed=true;}entry.Written=true;
     }write(process.Input,entry.Bytes,entry.Op);}}
    catch(Exception e){Fail("Native write failed: "+e.Message);}
   });
  }
  void ReadOutput() {
   try{while(true){byte[] bytes=CalibrationPipeFrames.ReadLine(child.Output,CalibrationContract.LineBytes);if(bytes==null)break;Receive(bytes);}
    lock(gate){if(!stopRequested&&!Terminal(phase))FailLocked("Native stdout EOF");else if(commitUnconfirmed)FailLocked("Native commit reply lost");}
   }catch(Exception e){Fail("Native output rejected: "+e.Message);}finally{lock(gate){stdoutDone=true;}wake.Set();}
  }
  void ReadError(){try{CalibrationPipeFrames.Drain(child.Error,256*1024,delegate(byte[] bytes,int count){lock(gate){stderr.Write(bytes,0,count);}});}catch(Exception e){Fail(e.Message);}finally{lock(gate){stderrDone=true;}wake.Set();}}
  void Receive(byte[] bytes) {
   var value=CalibrationJson.Object(bytes,CalibrationContract.LineBytes,32768);string status=Text(value,"status");
   lock(gate){long now=Now();if(session==null&&status=="UNAVAILABLE"&&!value.ContainsKey("event_sequence")){FailLocked("Native startup unavailable: "+Text(value,"reason"));return;}long next=CalibrationJson.Integer(value,"event_sequence",1,long.MaxValue);if(next<=sequence||CalibrationJson.Integer(value,"previous_emitted_sequence",0,long.MaxValue)!=sequence)throw new InvalidOperationException("Native event chain mismatch");sequence=next;
    object requestId;Request entry=null;
    if(value.TryGetValue("request_id",out requestId)){if(!(requestId is string)||!ledger.TryGetValue((string)requestId,out entry))throw new InvalidOperationException("Unrequested native outcome");}
    if(entry!=null&&status=="IN_PROGRESS")return;
    if(entry!=null&&status=="REJECTED"&&!value.ContainsKey("phase")){FailLocked("Native protocol rejected command: "+Text(value,"reason"));return;}
    if(session==null){if(entry==null||entry.Op!="open"||(status!="OPEN"&&status!="REJECTED"))throw new InvalidOperationException("Native open identity not established");session=Text(value,"session_id");run=Text(value,"run_id");}
    else if(Text(value,"session_id")!=session||Text(value,"run_id")!=run)throw new InvalidOperationException("Foreign native session/run");
    lastNative=CalibrationJson.Utf8.GetString(bytes);
    if(entry!=null){
     if(status=="IN_PROGRESS")return;
     string canonical=Canonical(value);
     if(entry.Acknowledged){
      if(entry.Canonical!=canonical||!CalibrationJson.Boolean(value,"replayed"))throw new InvalidOperationException("Changed cached native outcome");
      string current=Text(value,"current_phase");bool eligible=CalibrationJson.Boolean(value,"eligible");
      if(value.ContainsKey("worker_lifetime_expired")&&CalibrationJson.Boolean(value,"worker_lifetime_expired"))FailLocked("Native cached outcome reports expired worker");
      else if(Terminal(current)&&!Terminal(phase))FailLocked("Native cached outcome is no longer eligible: "+current);
      else if(current=="PREPARED"&&!eligible&&prepared!=null){prepared=null;phase="RESULT_READY";reviewDeadline=0;}
      return;
     }
     Apply(entry,value,bytes,now);entry.Canonical=canonical;entry.Acknowledged=true;cacheBytes+=bytes.Length;
     if(cacheBytes>10*1024*1024)FailLocked("Native outcome/cache byte limit");
     if(object.ReferenceEquals(active,entry)){active=null;phaseDeadline=0;}entry.Reply.TrySetResult(value);return;
    }
    if(status=="PHASE"){
     if(active==null||active.Op!="begin")throw new InvalidOperationException("Unexpected native phase");string native=Text(value,"phase");
     if(native=="RECORDING"&&!recordPhase&&!solvePhase){recordPhase=true;phase="RECORDING";phaseDeadline=now+20000;}
     else if(native=="SOLVING"&&recordPhase&&!solvePhase){solvePhase=true;phase="SOLVING";phaseDeadline=now+10000;}
     else throw new InvalidOperationException("Native phase order changed");Notify(lastNative,false);return;
    }
    if(status=="PROGRESS"){if(active==null||active.Op!="begin"||Text(value,"phase")!="RECORDING"||solvePhase)throw new InvalidOperationException("Unexpected native progress");progress=lastNative;return;}
    string terminal=Text(value,"phase");if(!Terminal(terminal)||CalibrationJson.Boolean(value,"eligible"))throw new InvalidOperationException("Unexpected unsolicited native control");
    phase=terminal;FailLocked(value.ContainsKey("reason")?Text(value,"reason"):"Native session invalidated");Notify(lastNative,true);
   }
  }
  void Apply(Request entry,Dictionary<string,object> value,byte[] bytes,long now) {
   string status=Text(value,"status"),nativePhase=value.ContainsKey("phase")?Text(value,"phase"):status;
   if(entry.Op=="open"&&status=="OPEN"){
    if(!CalibrationJson.Boolean(value,"capture_supported")||CalibrationJson.Boolean(value,"readonly")!=readOnly)throw new InvalidOperationException("Native capture/readonly contract mismatch");openHash=CalibrationJson.Hash(value,"config_sha256");phase="OPEN";
   }else if(entry.Op=="discover"&&status=="DISCOVERED"){
    var witness=Map(value,"witness");if(Text(witness,"bridge_id")!=profile.BridgeId||Text(witness,"source")!=profile.Source||Text(witness,"device")!=profile.Device||Text(witness,"tracker")!=profile.Tracker||Text(witness,"input_space")!=profile.InputSpace||CalibrationJson.Integer(witness,"input_revision",0,UInt32.MaxValue)!=profile.InputRevision||Text(witness,"reference_role")!=profile.ReferenceRole)throw new InvalidOperationException("Native witness differs from selected target");
    Text(witness,"standing_witness");Text(witness,"reference_serial");phase="DISCOVERED";
   }else if(entry.Op=="confirm_target"&&status=="CONFIRMED"){confirmation=Text(value,"confirmation_token");confirmDeadline=entry.SentAt+10000;phase="CONFIRMED";
   }else if(entry.Op=="begin"&&status=="RESULT_READY"){
    var nativeResult=new CalibrationNativeResult(bytes);if(nativeResult.ConfigSha256!=openHash)throw new InvalidOperationException("Native result config snapshot changed");result=nativeResult;prepared=null;resultDeadline=entry.SentAt+600000;confirmation=null;phase=stopRequested?"INSPECT_ONLY":"RESULT_READY";
   }else if(entry.Op=="prepare_commit"&&status=="PREPARED"){prepared=new CalibrationPreparedCandidate(result,bytes);reviewDeadline=entry.SentAt+30000;phase=stopRequested?"INSPECT_ONLY":"PREPARED";
   }else if(entry.Op=="commit"&&(status=="SAVED"||status=="SAVED_CONFIG_EVIDENCE_STALE"||status=="UNKNOWN")){
    actualOutcome=status;commitUnconfirmed=false;phase=status;prepared=null;if(stopRequested)stopDeadline=now+2000;
   }else if(entry.Op=="cancel"||entry.Op=="close"){
    if(!Terminal(status)&&status!="REJECTED")throw new InvalidOperationException("Native close/cancel outcome invalid");
    if(status=="SAVED"||status=="SAVED_CONFIG_EVIDENCE_STALE"){if(!mayCommit)throw new InvalidOperationException("Unrequested native save outcome");actualOutcome=status;commitUnconfirmed=false;phase=status;}
    else if(actualOutcome==null&&!commitUnconfirmed)phase=nativePhase;
   }else if(status=="REJECTED"){
    prepared=null;confirmation=null;if(entry.Op=="commit"){commitUnconfirmed=false;actualOutcome=nativePhase=="UNKNOWN"?"UNKNOWN":"REJECTED";}
    phase=nativePhase;if(Terminal(nativePhase)||entry.Op=="open")FailLocked(value.ContainsKey("reason")?Text(value,"reason"):"Native action rejected");
   }else throw new InvalidOperationException("Unexpected native outcome for "+entry.Op);
  }
  internal Task CloseAsync(){RequestStop(false);return stopped.Task;}
  internal Task CancelAsync(){RequestStop(true);return stopped.Task;}
  void RequestStop(bool cancel) {
   CalibrationOwnedProcess boot=null;bool closePipe=false;
   lock(gate){if(closed)return;long now;try{now=Now();}catch{now=lastNow;}stopRequested=true;prepared=null;confirmation=null;
    if(!commitUnconfirmed&&stopDeadline==0)stopDeadline=now+2000;
    if(session!=null&&child!=null){
     QueueStopLocked(cancel);
    }else{boot=bootstrap;closePipe=child!=null;}
    if(!started){closed=true;stopped.TrySetResult(true);}
   }
   if(boot!=null)try{boot.KillOwned();}catch{}if(closePipe)Task.Run(delegate{try{child.CloseInput();}catch{}});wake.Set();
  }
  void Watch() {
   try{while(true){CalibrationOwnedProcess kill=null,bootKill=null,release=null;
    lock(gate){if(closed)break;long now;try{now=Now();}catch(Exception e){now=lastNow;FailLocked(e.Message);kill=child;bootKill=bootstrap;}
     if(now-workerStart>=20*60*1000)FailLocked("Calibration worker lifetime expired");
     if(versionDeadline!=0&&now>=versionDeadline){FailLocked("Calibration bootstrap timeout");bootKill=bootstrap;}
     if(phase=="STARTING"&&phaseDeadline!=0&&now>=phaseDeadline)FailLocked("Calibration child startup timeout");
     long deadline=phaseDeadline!=0?phaseDeadline:(active==null?0:active.Deadline);
     if(active!=null&&deadline!=0&&now>=deadline)FailLocked("Native operation timeout: "+active.Op);
     if(!stopRequested&&confirmDeadline!=0&&phase=="CONFIRMED"&&now>=confirmDeadline)FailLocked("Confirmation expired; new measurement required");
     if(!stopRequested&&resultDeadline!=0&&now>=resultDeadline)FailLocked("Native result review budget expired");
     if(!stopRequested&&prepared!=null&&reviewDeadline!=0&&now>=reviewDeadline){prepared=null;phase="RESULT_READY";reviewDeadline=0;Notify("Review token expired; explicitly prepare the same native result again",false);}
     if(stopRequested&&child!=null){
      if(commitUnconfirmed&&active!=null&&active.Op=="commit"&&now>=active.Deadline){actualOutcome=actualOutcome??"UNKNOWN";phase="UNKNOWN";kill=child;}
      else if(!commitUnconfirmed&&stopDeadline!=0&&now>=stopDeadline)kill=child;
      if(child.HasExited){if(drainDeadline==0)drainDeadline=now+2000;if((stdoutDone&&stderrDone)||now>=drainDeadline){
       if(commitUnconfirmed){actualOutcome=actualOutcome??"UNKNOWN";phase="UNKNOWN";}
       foreach(var entry in ledger.Values)if(!entry.Acknowledged)entry.Reply.TrySetException(new InvalidOperationException(commitUnconfirmed?"UNKNOWN: native worker stopped before commit acknowledgement":"Native worker stopped before command acknowledgement"));
       active=null;closed=true;release=child;
      }}
     }
     if(stopRequested&&launchPending&&stopDeadline!=0&&now>=stopDeadline)stopped.TrySetException(new InvalidOperationException("Native startup termination is unconfirmed; no new worker may start"));
     if(stopRequested&&child==null&&bootstrap==null&&!launchPending){closed=true;stopped.TrySetResult(true);}
    }
    if(bootKill!=null)try{bootKill.KillOwned();}catch{}if(kill!=null)try{kill.KillOwned();}catch(Exception e){lock(gate){fault="Owned termination unconfirmed: "+e.Message;}}
    if(release!=null){release.Dispose();stopped.TrySetResult(true);break;}wake.WaitOne(25);
   }}catch(Exception e){lock(gate){FailLocked("Calibration watchdog failed: "+e.Message);}CalibrationOwnedProcess process;lock(gate){process=child??bootstrap;}if(process!=null)try{process.KillOwned();}catch{}stopped.TrySetException(e);
   }finally{lock(gate){watchDone=true;}}
  }
  internal CalibrationClientSnapshot Snapshot(){lock(gate){return new CalibrationClientSnapshot{Phase=phase,Fault=fault,ActualOutcome=actualOutcome,Progress=progress,LastNative=lastNative,Busy=active!=null,Stopping=stopRequested,Stopped=closed,CommitMayHaveStarted=mayCommit,Result=result,Prepared=prepared,CanPrepare=!readOnly&&!stopRequested&&result!=null&&(phase=="RESULT_READY"||phase=="PREPARED"),CanCommit=!readOnly&&!stopRequested&&prepared!=null&&phase=="PREPARED"};}}
  internal string[] DrainNotifications(){lock(gate){var values=new List<string>(controls);controls.Clear();if(terminalNotification!=null){values.Add(terminalNotification);terminalNotification=null;}return values.ToArray();}}
  internal byte[] StderrBytes(){lock(gate){return stderr.ToArray();}}
  public void Dispose(){if(disposed)return;disposed=true;RequestStop(false);}
 }
}
