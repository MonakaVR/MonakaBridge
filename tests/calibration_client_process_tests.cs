using System;
using System.Collections.Generic;
using System.IO;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using MonakaBridge;
class CalibrationClientProcessTests {
 static string fixture,root,faultFixture;static int cases;
 static void Check(bool okay,string why){if(!okay)throw new Exception(why);}
 static Dictionary<string,object> Await(Task<Dictionary<string,object>> value){Check(value.Wait(20000),"native command exceeded test deadline");return value.GetAwaiter().GetResult();}
 static void Closed(CalibrationProcessClient client){var task=client.CloseAsync();Check(task.Wait(5000),"owned child close deadline");task.GetAwaiter().GetResult();Check(client.Snapshot().Stopped,"owned child not stopped");}
 static void Until(Func<bool> ready){var timer=Stopwatch.StartNew();while(!ready()){Check(timer.ElapsedMilliseconds<5000,"fixture state deadline");Thread.Sleep(5);}}
 static void Failed(Task task,string reason){try{Check(task.Wait(10000),"failure test deadline");task.GetAwaiter().GetResult();}catch(AggregateException e){Check(e.GetBaseException().Message.IndexOf(reason,StringComparison.OrdinalIgnoreCase)>=0,e.ToString());return;}catch(InvalidOperationException e){Check(e.Message.IndexOf(reason,StringComparison.OrdinalIgnoreCase)>=0,e.ToString());return;}throw new Exception("Expected failure "+reason);}
 sealed class TestClock {long value;internal long Read(){return Interlocked.Read(ref value);}internal void Advance(long amount){Interlocked.Add(ref value,amount);}}
 sealed class Run:IDisposable {
  internal string Directory,Config;internal byte[] Original;internal CalibrationInstallation Profile;internal CalibrationProcessClient Client;
  internal Run(string name,bool readOnly,string mode,Func<long> clock=null,Action<Stream,byte[],string> writer=null,string selectedHelper=null) {
   Directory=Path.Combine(root,name+"-"+Guid.NewGuid().ToString("N"));System.IO.Directory.CreateDirectory(Directory);
   var environment=new Dictionary<string,string>{{"MONAKA_CALIBRATION_TEST_ROOT",Path.Combine(Directory,"seed")}};
   Dictionary<string,object> seed;
   using(var child=CalibrationOwnedProcess.Start(fixture,"--fixture-seed",true,environment,null)){
    var line=CalibrationPipeFrames.ReadLine(child.Output,65536);Check(line!=null,"fixture seed missing");seed=(Dictionary<string,object>)CalibrationJson.Object(line,65536,32768)["test_only_fixture_open"];Check(child.WaitForExit(3000)&&child.ExitCode==0,"seed failed");
   }
   string processHelper=selectedHelper??fixture;
   Config=(string)seed["config_path"];Original=File.ReadAllBytes(Config);string helper=Path.Combine(Directory,"config-helper.fixture");File.WriteAllText(helper,"test-only hash file; never executable");
   var profile=new Dictionary<string,object>{{"schema",CalibrationInstallation.Schema},{"name",name},{"config",Config},{"health",Config+".status.json"},{"config_helper",helper},{"helper_sha256",CalibrationInstallation.Sha256(File.ReadAllBytes(helper))},{"read_only",readOnly},{"calibration_helper",processHelper},{"calibration_helper_sha256",CalibrationInstallation.Sha256(File.ReadAllBytes(processHelper))},{"calibration_api",1},{"calibration_software_policy",1},{"calibration_gui_contract",1},{"bridge_id",seed["bridge_id"]},{"installation_path",seed["installation_path"]},{"association",CalibrationInstallation.Association},{"single_bridge_no_relay",true},{"mirror_address",seed["mirror_address"]},{"mirror_port",seed["mirror_port"]},{"source",seed["source"]},{"device",seed["device"]},{"tracker",seed["tracker"]},{"input_space",seed["input_space"]},{"input_revision",seed["input_revision"]},{"reference_role",seed["reference_role"]}};
   Profile=CalibrationInstallation.SaveNew(Path.Combine(Directory,"profile.json"),profile,new[]{Config,helper,fixture});
   var env=new Dictionary<string,string>{{"MONAKA_CALIBRATION_TEST_ROOT",Path.Combine(Directory,"process")},{"MONAKA_CALIBRATION_TEST_MODE",mode}};
   Client=new CalibrationProcessClient(Profile,readOnly,delegate(string path,string arguments,bool version){return CalibrationOwnedProcess.Start(path,arguments,version,env,null);},clock,writer);
  }
  internal void Result(){Check(Await(Client.OpenAsync())["status"].Equals("OPEN"),"open");Check(Await(Client.DiscoverAsync())["status"].Equals("DISCOVERED"),"discover");Check(Await(Client.ConfirmAsync())["status"].Equals("CONFIRMED"),"confirm");var begin=Await(Client.BeginAsync());Check(begin["status"].Equals("RESULT_READY"),"begin: "+CalibrationJson.Utf8.GetString(CalibrationJson.Encode(begin,65536))+" | client: "+Client.Snapshot().Fault);}
  public void Dispose(){try{Closed(Client);}finally{Client.Dispose();}}
 }
 static int Main(string[] args){try{
  Check(args.Length==3,"fixture, fault fixture and evidence root required");fixture=Path.GetFullPath(args[0]);faultFixture=Path.GetFullPath(args[1]);root=Path.GetFullPath(args[2]);System.IO.Directory.CreateDirectory(root);
  using(var run=new Run("same-result",false,"")){
   run.Result();var result=run.Client.Snapshot().Result;var display=result.DisplayCopy();((Dictionary<string,object>)display["result"])["world"]=new[]{999,0,0};
   var prepared=Await(run.Client.PrepareAsync());Check(prepared["status"].Equals("PREPARED"),"prepare: "+CalibrationJson.Utf8.GetString(CalibrationJson.Encode(prepared,65536)));var candidate=run.Client.Snapshot().Prepared;byte[] bytes=File.ReadAllBytes(candidate.CandidatePath);bool truncated;candidate.ReadPreview(out truncated);Check(!truncated,"synthetic candidate preview");
   var saved=Await(run.Client.CommitAsync());Check(saved["status"].Equals("SAVED")&&run.Client.Snapshot().ActualOutcome=="SAVED","commit outcome");Check(Convert.ToBase64String(File.ReadAllBytes(run.Config))==Convert.ToBase64String(bytes),"same native exact candidate bytes");Check(run.Client.Snapshot().CommitMayHaveStarted,"commit prewrite latch missing");Closed(run.Client);Check(run.Client.Snapshot().ActualOutcome=="SAVED","close lost native save outcome");cases++;
  }
  using(var run=new Run("readonly",true,"")){
   run.Result();bool rejected=false;try{run.Client.PrepareAsync();}catch(InvalidOperationException){rejected=true;}Check(rejected&&!run.Client.Snapshot().CanPrepare,"readonly prepare enabled");Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"readonly changed config");cases++;
  }
  foreach(string mode in new[]{"busy-discover","busy-record","busy-solve"}){
   var clock=new TestClock();using(var run=new Run(mode,false,mode,clock.Read)){
    Await(run.Client.OpenAsync());Task pending;
    if(mode=="busy-discover")pending=run.Client.DiscoverAsync();else{Await(run.Client.DiscoverAsync());Await(run.Client.ConfirmAsync());pending=run.Client.BeginAsync();}
    Until(delegate{return File.Exists(Path.Combine(run.Directory,"process",mode+"-entered.fixture"));});
    if(mode=="busy-record")Until(delegate{return run.Client.Snapshot().Phase=="RECORDING";});if(mode=="busy-solve")Until(delegate{return run.Client.Snapshot().Phase=="SOLVING";});
    clock.Advance(mode=="busy-record"?20001:10001);Failed(pending,"timeout");var close=run.Client.CloseAsync();Check(!close.IsCompleted,"blocked native bypassed shutdown grace");clock.Advance(2001);Check(close.Wait(5000),"independent watchdog did not stop owned blocked child");Check(run.Client.Snapshot().Stopped&&!run.Client.Snapshot().CommitMayHaveStarted,"precommit watchdog outcome");Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"busy precommit changed config");cases++;
   }
  }
  foreach(string mode in new[]{"post-replace-exit","post-replace-unknown"}){
   var clock=new TestClock();using(var run=new Run(mode,false,mode,clock.Read)){
    run.Result();Await(run.Client.PrepareAsync());byte[] bytes=File.ReadAllBytes(run.Client.Snapshot().Prepared.CandidatePath);var commit=run.Client.CommitAsync();
    if(mode=="post-replace-exit")Failed(commit,"UNKNOWN");else Check(Await(commit)["status"].Equals("UNKNOWN"),"native unknown converted to another status");
    clock.Advance(15001);Until(delegate{return run.Client.Snapshot().ActualOutcome=="UNKNOWN";});Check(run.Client.Snapshot().CommitMayHaveStarted&&Convert.ToBase64String(bytes)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"post-replace outcome rollback/fake cancellation");cases++;
   }
  }
  {var clock=new TestClock();using(var run=new Run("partial-commit",false,"",clock.Read,delegate(Stream stream,byte[] bytes,string op){if(op=="commit"){stream.Write(bytes,0,12);stream.Flush();throw new IOException("test-only partial commit write");}stream.Write(bytes,0,bytes.Length);stream.Flush();})){
   run.Result();Await(run.Client.PrepareAsync());Failed(run.Client.CommitAsync(),"UNKNOWN");clock.Advance(15001);Until(delegate{return run.Client.Snapshot().Stopped;});Check(run.Client.Snapshot().CommitMayHaveStarted&&run.Client.Snapshot().ActualOutcome=="UNKNOWN"&&Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"partial write misclassified or retried");cases++;
  }}
  {var clock=new TestClock();clock.Advance(100);using(var run=new Run("clock-rollback",true,"",clock.Read)){
   Await(run.Client.OpenAsync());clock.Advance(-1);Until(delegate{return run.Client.Snapshot().Stopped;});Check(run.Client.Snapshot().Fault.Contains("MONOTONIC_CLOCK_ROLLBACK")&&!run.Client.Snapshot().CommitMayHaveStarted,"clock rollback did not close precommit");cases++;
  }}
  {var clock=new TestClock();Stream pipe=null;byte[] retry=null;using(var run=new Run("cached-review",false,"",clock.Read,delegate(Stream stream,byte[] bytes,string op){stream.Write(bytes,0,bytes.Length);stream.Flush();if(op=="prepare_commit"){pipe=stream;retry=(byte[])bytes.Clone();}})){
   run.Result();Await(run.Client.PrepareAsync());var original=run.Client.Snapshot().Prepared;clock.Advance(20000);pipe.Write(retry,0,retry.Length);pipe.Flush();Until(delegate{return run.Client.Snapshot().LastNative.Contains("\"replayed\":true");});Check(object.ReferenceEquals(original,run.Client.Snapshot().Prepared),"cached response replaced immutable prepared object: "+run.Client.Snapshot().Fault+" | "+run.Client.Snapshot().LastNative);clock.Advance(10001);Until(delegate{return !run.Client.Snapshot().CanCommit;});Check(run.Client.Snapshot().Phase=="RESULT_READY","cached replay renewed review TTL");Await(run.Client.PrepareAsync());Check(run.Client.Snapshot().CanCommit,"explicit same-result reprepare rejected");cases++;
  }}
  using(var run=new Run("request-quota",false,"")){
   run.Result();for(int i=0;i<122;i++)Await(run.Client.PrepareAsync());Failed(run.Client.PrepareAsync(),"request limit");var cancelled=run.Client.CancelAsync();Check(cancelled.Wait(5000)&&run.Client.Snapshot().Stopped,"reserved cancel/close unavailable after126 ordinary requests");Check(!run.Client.Snapshot().CommitMayHaveStarted&&Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"quota shutdown changed config");cases++;
  }
  foreach(string mode in new[]{"utf8","oversize","stderr","chain","unrequested","foreign-session","old-version","idle-invalid","hang-version"}){
   var clock=new TestClock();using(var run=new Run(mode,true,mode,clock.Read,null,faultFixture)){
    var opening=run.Client.OpenAsync();
    if(mode=="hang-version"){Thread.Sleep(100);clock.Advance(5001);Failed(opening,"version");Check(run.Client.Snapshot().Fault.Contains("bootstrap"),"bootstrap watchdog reason missing");}
    else if(mode=="foreign-session"){Await(opening);Failed(run.Client.DiscoverAsync(),"Foreign native session");}
    else if(mode=="idle-invalid"){Await(opening);Until(delegate{return run.Client.Snapshot().Fault!=null;});Check(run.Client.Snapshot().Fault.Contains("idle lifetime invalidation"),"idle terminal notification not applied");}
    else Failed(opening,mode=="old-version"?"features":mode=="chain"?"chain":mode=="unrequested"?"Unrequested":mode=="stderr"?"stderr":"output");
    clock.Advance(2001);Until(delegate{return run.Client.Snapshot().Stopped;});Check(!run.Client.Snapshot().CommitMayHaveStarted&&!run.Client.Snapshot().CanCommit,"fault enabled commit");Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"protocol fault wrote config");cases++;Console.WriteLine("PASS bounded native control fault: "+mode);
   }
  }
  Console.WriteLine("PASS actual C# client/native runtime provider "+cases+" cases; same solver/result/candidate; fake public SDK/in-memory transport only; GUI/realSDK/socket NOT_RUN");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
