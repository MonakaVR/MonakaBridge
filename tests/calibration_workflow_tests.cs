using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using MonakaBridge;

class CalibrationWorkflowTests {
 static string fixture,configHelper,root;static int cases;
 static void Check(bool value,string why){if(!value)throw new Exception(why);}
 static void Await(Task task){Check(task.Wait(25000),"Workflow test deadline");task.GetAwaiter().GetResult();}
 static void Until(Func<bool> ready){var watch=Stopwatch.StartNew();while(!ready()){Check(watch.ElapsedMilliseconds<10000,"Fixture observation deadline");Thread.Sleep(5);}}
 static void Reject(Action action){try{action();}catch(InvalidOperationException){return;}throw new Exception("Expected refusal");}
 static Dictionary<string,object> Read(string path){return new JavaScriptSerializer{MaxJsonLength=2*1024*1024,RecursionLimit=16}.Deserialize<Dictionary<string,object>>(File.ReadAllText(path));}
 static Dictionary<string,object> Mapping(Dictionary<string,object> document,string source,string device){foreach(Dictionary<string,object> row in (IEnumerable)document["mappings"])if((string)row["source_id"]==source&&(string)row["device_id"]==device)return row;throw new Exception("Fixture mapping missing");}
 static bool Same(byte[] a,byte[] b){return Convert.ToBase64String(a)==Convert.ToBase64String(b);}
 sealed class Run:IDisposable {
  internal string Directory,Config,Native,ConfigTool;internal byte[] Original,Candidate;internal GuiLaunchOptions Options;internal CalibrationInstallation Profile;
  internal CalibrationWorkflow Workflow;internal CalibrationProcessClient Client;internal readonly List<string> Commands=new List<string>();internal int Launches;
  internal Run(string name,string mode="",bool readOnly=false,Action<Run,Stream,byte[],string> writing=null){
   Console.WriteLine("CASE "+name);Directory=Path.Combine(root,name+"-"+Guid.NewGuid().ToString("N").Substring(0,8));System.IO.Directory.CreateDirectory(Directory);
   var seedEnvironment=new Dictionary<string,string>{{"MONAKA_CALIBRATION_TEST_ROOT",Path.Combine(Directory,"seed")},{"MONAKA_CALIBRATION_TEST_MODE",""}};
   Dictionary<string,object> seed;
   using(var child=CalibrationOwnedProcess.Start(fixture,"--fixture-seed",true,seedEnvironment,null)){
    seed=(Dictionary<string,object>)CalibrationJson.Object(CalibrationPipeFrames.ReadLine(child.Output,65536),65536,32768)["test_only_fixture_open"];
    Check(child.WaitForExit(3000)&&child.ExitCode==0,"Fixture seed failed");
   }
   Config=(string)seed["config_path"];Original=File.ReadAllBytes(Config);
   string binaries=Path.Combine(Directory,"bin");System.IO.Directory.CreateDirectory(binaries);
   Native=Path.Combine(binaries,"monaka_bridge_calibrator.exe");ConfigTool=Path.Combine(binaries,"monaka_bridge_config.exe");
   File.Copy(fixture,Native);File.Copy(configHelper,ConfigTool);
   var args=new List<string>{(string)seed["installation_path"],"--config",Config,"--health",Config+".status.json","--config-helper",ConfigTool,"--helper-sha256",CalibrationInstallation.Sha256(File.ReadAllBytes(ConfigTool)),readOnly?"--read-only":"--allow-write"};
   Options=GuiLaunchOptions.Parse(args.ToArray(),null);
   var doc=Read(Config);Profile=CalibrationInstallation.ForSelection(Options,doc,Mapping(doc,(string)seed["source"],(string)seed["device"]),(string)seed["reference_role"]);
   Check(!File.Exists(Profile.ProfilePath)&&Profile.MirrorPort==29813&&Profile.CalibrationHelper==Native,"Selection required a persisted profile or wrong standard endpoint/helper");
   var environment=new Dictionary<string,string>{{"MONAKA_CALIBRATION_TEST_ROOT",Path.Combine(Directory,"worker")},{"MONAKA_CALIBRATION_TEST_MODE",mode},{"MONAKA_CALIBRATION_TEST_MIRROR_PORT","29813"}};
   Workflow=new CalibrationWorkflow(delegate(CalibrationInstallation selected,bool ro){
    Client=new CalibrationProcessClient(selected,ro,delegate(string path,string arguments,bool version){Interlocked.Increment(ref Launches);return CalibrationOwnedProcess.Start(path,arguments,version,environment,null);},null,delegate(Stream stream,byte[] bytes,string op){
     lock(Commands)Commands.Add(op);
     if(op=="commit"){
      var candidate=Client.Snapshot().Prepared;Check(candidate!=null,"Commit lost native candidate");Candidate=File.ReadAllBytes(candidate.CandidatePath);
      var fields=CalibrationJson.Object(bytes,65536,32768);Check((string)fields["result_id"]==candidate.ResultId&&(string)fields["review_token"]==candidate.ReviewToken&&(string)fields["candidate_sha256"]==candidate.CandidateSha256,"Automatic commit changed candidate authority");
      Check(!fields.ContainsKey("world")&&!fields.ContainsKey("mount"),"Managed workflow constructed calibration data");
     }
     if(writing!=null)writing(this,stream,bytes,op);else{stream.Write(bytes,0,bytes.Length);stream.Flush();}
    });return Client;
   });
  }
  internal bool Has(string op){lock(Commands)return Commands.Contains(op);}
  internal void Complete(){Await(Workflow.StartAsync(Profile,false));Check(!Workflow.Active&&Workflow.Snapshot().Stopped,"Automatic workflow did not close owned worker");}
  public void Dispose(){Await(Workflow.StopAsync());Workflow.Dispose();}
 }
 static int Main(string[] args){try{
  Check(args.Length==3,"Fixture, native config helper and evidence root required");fixture=Path.GetFullPath(args[0]);configHelper=Path.GetFullPath(args[1]);root=Path.GetFullPath(args[2]);System.IO.Directory.CreateDirectory(root);
  using(var run=new Run("ordered-save")){
   var document=Read(run.Config);var row=Mapping(document,run.Profile.Source,run.Profile.Device);string source=run.Profile.Source;row["source_id"]="mutated presentation copy";
   Check(run.Profile.Source==source,"In-memory installation not immutable");row["source_id"]=source;
   Reject(delegate{CalibrationInstallation.ForSelection(run.Options,document,row,"automatic");});
   var absent=new Dictionary<string,object>(row);absent["device_id"]="unmapped opaque identity";Reject(delegate{CalibrationInstallation.ForSelection(run.Options,document,absent,"left");});
   run.Complete();Check(run.Workflow.Snapshot().ActualOutcome=="SAVED"&&run.Workflow.Snapshot().Fault==null,"Automatic native save failed: "+run.Workflow.Snapshot().Fault);
   Check(run.Candidate!=null&&Same(run.Candidate,File.ReadAllBytes(run.Config)),"Save did not use exact native candidate bytes");
   lock(run.Commands)Check(string.Join(",",run.Commands.ToArray())=="open,discover,confirm_target,begin,prepare_commit,commit,close","Unexpected ordered automatic commands: "+string.Join(",",run.Commands.ToArray()));
   Check(run.Client.StderrBytes().Length>0,"Owned fixture stderr not drained");
   Check(run.Launches==2,"One Start spawned extra native workers");
   Check(!run.Workflow.Snapshot().CanCommit&&!run.Workflow.Snapshot().CanPrepare,"Workflow exposes manual stage actions");
   run.Complete();Check(run.Workflow.Snapshot().ActualOutcome=="SAVED"&&run.Launches==4,"Stopped workflow did not allow explicit next Start");
   Check(Same(run.Candidate,File.ReadAllBytes(run.Config)),"Second save changed native candidate bytes");cases++;
  }
  using(var run=new Run("readonly", "",true)){
   run.Complete();Check(run.Workflow.Snapshot().ActualOutcome=="MEASURED_READ_ONLY"&&run.Workflow.Snapshot().Result!=null&&run.Workflow.Snapshot().Fault==null,"Readonly did not measure cleanly: "+run.Workflow.Snapshot().Fault+" | "+run.Workflow.Snapshot().LastNative+" | "+CalibrationJson.Utf8.GetString(run.Client.StderrBytes()));Check(run.Has("begin")&&!run.Has("prepare_commit")&&!run.Has("commit")&&Same(run.Original,File.ReadAllBytes(run.Config)),"Readonly measurement prepared or wrote config");cases++;
  }
  using(var run=new Run("effective-readonly")){
   Await(run.Workflow.StartAsync(run.Profile,true));Check(run.Workflow.Snapshot().ActualOutcome=="MEASURED_READ_ONLY"&&!run.Workflow.Active&&run.Workflow.Snapshot().Stopped,"Effective readonly measure outcome/cleanup");Check(!run.Has("prepare_commit")&&!run.Has("commit")&&Same(run.Original,File.ReadAllBytes(run.Config)),"Effective readonly measurement wrote config");cases++;
  }
  using(var run=new Run("native-refusal","init-failure")){
   run.Complete();Check(run.Workflow.Snapshot().Fault!=null&&!run.Has("begin")&&!run.Has("commit"),"Native discovery refusal advanced pipeline");Check(Same(run.Original,File.ReadAllBytes(run.Config)),"Native refusal changed config");cases++;
  }
  using(var entered=new ManualResetEventSlim(false))using(var release=new ManualResetEventSlim(false))using(var run=new Run("cancel-before-commit","",false,delegate(Run owner,Stream stream,byte[] bytes,string op){
   if(op=="prepare_commit"){entered.Set();Check(release.Wait(5000),"Cancel fixture release deadline");}
   stream.Write(bytes,0,bytes.Length);stream.Flush();
  })){
   Task running=run.Workflow.StartAsync(run.Profile,false);Check(entered.Wait(15000),"Prepare not reached");Reject(delegate{run.Workflow.StartAsync(run.Profile,false);});
   Task stopped=run.Workflow.StopAsync();release.Set();Await(stopped);Await(running);
   Check(!run.Workflow.Active&&run.Workflow.Snapshot().Stopped&&!run.Has("commit")&&!run.Workflow.Snapshot().CommitMayHaveStarted,"Commit dispatched after cancellation");
   Check(Same(run.Original,File.ReadAllBytes(run.Config)),"Cancelled run changed config");cases++;
  }
  using(var run=new Run("cancel-recording","busy-record")){
   Task running=run.Workflow.StartAsync(run.Profile,false);Until(delegate{return run.Workflow.Snapshot().Phase=="RECORDING";});Await(run.Workflow.StopAsync());Await(running);
   Check(!run.Workflow.Active&&run.Workflow.Snapshot().Stopped&&!run.Has("commit"),"Cancellation failed owned recording cleanup");Check(Same(run.Original,File.ReadAllBytes(run.Config)),"Recording cancellation wrote config");cases++;
  }
  byte[] externalConfig=null;
  using(var run=new Run("cas-refusal","",false,delegate(Run owner,Stream stream,byte[] bytes,string op){
   if(op=="commit"){externalConfig=CalibrationJson.Utf8.GetBytes(CalibrationJson.Utf8.GetString(owner.Original)+"\n ");File.WriteAllBytes(owner.Config,externalConfig);}
   stream.Write(bytes,0,bytes.Length);stream.Flush();
  })){
   run.Complete();Check(run.Workflow.Snapshot().ActualOutcome!="SAVED"&&run.Workflow.Snapshot().Fault!=null,"CAS conflict saved");Check(externalConfig!=null&&Same(externalConfig,File.ReadAllBytes(run.Config)),"CAS failure replaced concurrent config");lock(run.Commands)Check(run.Commands.FindAll(delegate(string op){return op=="commit";}).Count==1,"CAS conflict retried automatically");cases++;
  }
  foreach(string mode in new[]{"post-replace-unknown","post-replace-exit"})using(var run=new Run(mode,mode)){
   run.Complete();var snapshot=run.Workflow.Snapshot();Check(snapshot.ActualOutcome=="UNKNOWN"&&snapshot.CommitMayHaveStarted,"Lost acknowledgement was misreported after close");Check(run.Candidate!=null&&Same(run.Candidate,File.ReadAllBytes(run.Config)),"Unknown save evidence or candidate changed");lock(run.Commands)Check(run.Commands.FindAll(delegate(string op){return op=="commit";}).Count==1,"Unknown commit retried");cases++;
  }
  using(var run=new Run("pin-mismatch")){
   File.AppendAllText(run.ConfigTool,"different helper bytes");var doc=Read(run.Config);Reject(delegate{CalibrationInstallation.ForSelection(run.Options,doc,Mapping(doc,run.Profile.Source,run.Profile.Device),"left");});Check(run.Launches==0,"Pin mismatch started helper");cases++;
  }
  Console.WriteLine("PASS automatic calibration workflow "+cases+" cases; exact native candidate, sequential commands, cancellation/cleanup, readonly, CAS and UNKNOWN; fake SDK/in-memory datagrams only; genuine SDK/socket/HIL NOT_RUN");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
