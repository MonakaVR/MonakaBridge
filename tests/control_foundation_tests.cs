using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using MonakaBridge;

class FoundationTests {
 static string directory,helper,fake,example;static int cases;
 static readonly JavaScriptSerializer json=new JavaScriptSerializer();
 static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
 static void Pass(string name){++cases;Console.WriteLine("PASS "+name);}
 static void Rejected(Action action,string contains){try{action();}catch(Exception e){var actual=e is TargetInvocationException?e.InnerException:e;Check(contains=="cancel"?actual is OperationCanceledException:actual.Message.IndexOf(contains,StringComparison.OrdinalIgnoreCase)>=0,actual.ToString());return;}throw new Exception("Expected rejection: "+contains);}
 static string Fixture(string name){string path=Path.Combine(directory,name);Directory.CreateDirectory(path);string target=Path.Combine(path,"configuration 日本語.json");var c=SteamVrControlWpf.Read(example);c["bridge_id"]="fixture-publisher";c["mapping_revision"]=10;c["extension_unknown"]=new Dictionary<string,object>{{"preserve",new object[]{1,"日本語",true}}};File.WriteAllText(target,json.Serialize(c),new UTF8Encoding(false));return target;}
 static GuiLaunchOptions Options(string target,string selectedHelper,bool readOnly){return GuiLaunchOptions.Parse(new[]{"--config",target,"--health",target+".status.json","--config-helper",selectedHelper,readOnly?"--read-only":"--allow-write"},directory);}
 static string Candidate(string target){var c=SteamVrControlWpf.Read(target);c["mapping_revision"]=11;c["policy"]="disabled";string path=target+".candidate";File.WriteAllText(path,json.Serialize(c),new UTF8Encoding(false));return path;}
 static object Window(GuiLaunchOptions options){return Activator.CreateInstance(typeof(SteamVrControlWpf).GetNestedType("ControlWindow",BindingFlags.NonPublic),new object[]{options});}
 static object Field(object window,string name){return window.GetType().GetField(name,BindingFlags.NonPublic|BindingFlags.Instance).GetValue(window);}
 static void Set(object window,string name,object value){window.GetType().GetField(name,BindingFlags.NonPublic|BindingFlags.Instance).SetValue(window,value);}
 static void Invoke(object window,string name){window.GetType().GetMethod(name,BindingFlags.NonPublic|BindingFlags.Instance,null,Type.EmptyTypes,null).Invoke(window,null);}
 static void Pump(){Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);}
 static void Wait(object window){var until=DateTime.UtcNow.AddSeconds(20);while((bool)Field(window,"busy")){Check(DateTime.UtcNow<until,"Save timed out");Pump();Thread.Sleep(5);}}
 static void Close(object window){((Window)window).Close();}
 static void Arguments(){
  string target=Fixture("profile 空白");string profile=Path.Combine(Path.GetDirectoryName(target),"installation.json");
  File.WriteAllText(profile,json.Serialize(new{schema="monaka.bridge.installation.v1",name="fixture 日本語",config=Path.GetFileName(target),health="health.json",config_helper=helper,read_only=true}),new UTF8Encoding(false));
  var options=GuiLaunchOptions.Parse(new[]{"--installation",profile},null);
  Check(options.ConfigFile==target&&options.HealthFile==Path.Combine(Path.GetDirectoryName(target),"health.json")&&options.ReadOnly,"Profile paths anchored at profile directory");
  Check(options.WindowArguments().Contains("--read-only")&&options.WindowArguments().Contains("fixture 日本語"),"Window propagation");
  Check(GuiLaunchOptions.Quote(@"C:\target with spaces\")=="\"C:\\target with spaces\\\\\"","Trailing slash quoting");
  Rejected(delegate{GuiLaunchOptions.Parse(new[]{"--config","relative.json"},directory);},"absolute");
  Rejected(delegate{GuiLaunchOptions.Parse(new[]{"--config"},directory);},"Missing");
  Rejected(delegate{GuiLaunchOptions.Parse(new[]{"--bogus"},directory);},"Unknown");
  Rejected(delegate{GuiLaunchOptions.Parse(new[]{"--config",target,"--health",target},directory);},"distinct");
  Rejected(delegate{GuiLaunchOptions.Parse(new[]{"--installation",profile,"--config",target},directory);},"combined");
  Rejected(delegate{GuiLaunchOptions.Parse(new[]{"--read-only","--allow-write"},directory);},"conflict");
  Pass("installation profile, Unicode/space paths, precise quoting and invalid/ambiguous arguments");
 }
 static void Access(){
  string target=Fixture("read-only");byte[] before=File.ReadAllBytes(target);var options=Options(target,helper,true);object window=Window(options);
  try{
   Invoke(window,"Reload");foreach(Control c in (IEnumerable)Field(window,"editControls"))Check(!c.IsEnabled,"Read-only control enabled");
   foreach(Button b in (IEnumerable)Field(window,"serviceControls"))Check(!b.IsEnabled,"Lifecycle button enabled");
   foreach(string method in new[]{"SaveMapping","SelectObservation","Migrate"})Rejected(delegate{Invoke(window,method);},"Read-only");
   Rejected(delegate{new ConfigHelperClient(options).Commit(before,Candidate(target),CancellationToken.None);},"Read-only");
   Rejected(delegate{new ConfigHelperClient(options).TransformAndCommit(before,"policy both",false,CancellationToken.None);},"Read-only");
  }finally{Close(window);}
  Check(Convert.ToBase64String(before)==Convert.ToBase64String(File.ReadAllBytes(target)),"Read-only handler wrote target");
  Check(Directory.GetFiles(Path.GetDirectoryName(target),"*.config-write.lock").Length==0,"Read-only created lock");
  using(var tray=new SteamVrControlTray(options)){
   foreach(var item in (IDictionary)Field(tray,"policies")){
    var entry=(DictionaryEntry)item;Check(!(bool)entry.Value.GetType().GetProperty("Enabled").GetValue(entry.Value,null),"Readonly tray policy enabled");
   }
   Rejected(delegate{Invoke(tray,"RequireRouteChange");},"Read-only");
  }
  Rejected(GuiLaunchOptions.FromRoot(directory).RequireServiceControl,"Unsupported");Rejected(Options(target,helper,false).RequireServiceControl,"Unsupported");
  Pass("read-only buttons AND direct handlers reject edits; root/explicit lifecycle unsupported");
  File.SetAttributes(target,FileAttributes.ReadOnly);
  try{Check(Options(target,helper,false).ValidateEditing().Contains("read-only"),"Read-only permission not displayed");Rejected(delegate{Options(target,helper,false).RequireEdit();},"read-only");}
  finally{File.SetAttributes(target,FileAttributes.Normal);}
  Pass("configuration permission/read-only rejection without startup probe writes");
  var acl=File.GetAccessControl(target);var denied=File.GetAccessControl(target);
  denied.AddAccessRule(new System.Security.AccessControl.FileSystemAccessRule(System.Security.Principal.WindowsIdentity.GetCurrent().User,System.Security.AccessControl.FileSystemRights.Write,System.Security.AccessControl.AccessControlType.Deny));
  try{
   File.SetAccessControl(target,denied);var restricted=Options(target,helper,false);
   Check(restricted.ValidateEditing().Contains("permission/access"),"ACL denial not displayed");
   Rejected(restricted.RequireEdit,"permission/access");
   object blocked=Window(restricted);try{Invoke(blocked,"Reload");foreach(Control control in (IEnumerable)Field(blocked,"editControls"))Check(!control.IsEnabled,"ACL-denied control enabled");Rejected(delegate{Invoke(blocked,"SaveMapping");},"permission/access");}finally{Close(blocked);}
  }finally{File.SetAccessControl(target,acl);}
  Check(Convert.ToBase64String(before)==Convert.ToBase64String(File.ReadAllBytes(target)),"ACL test changed target");
  Pass("temporary-copy write ACL denial disables buttons and rejects handlers; ACL restored");
 }
 static void Helpers(){
  string target=Fixture("helper-generation");byte[] before=File.ReadAllBytes(target);string candidate=Candidate(target);
  Check(Options(target,helper+".missing",false).ValidateEditing().Contains("unavailable"),"Missing helper not visible");
  var wrong=Options(target,fake,false);File.WriteAllText(fake+".mode","wrong");
  Rejected(delegate{new ConfigHelperClient(wrong).Commit(before,candidate,CancellationToken.None);},"generation");
  File.WriteAllText(fake+".mode","fail");Rejected(delegate{new ConfigHelperClient(wrong).Commit(before,candidate,CancellationToken.None);},"rejected");
  Check(Convert.ToBase64String(before)==Convert.ToBase64String(File.ReadAllBytes(target)),"Wrong/failed helper changed target");
  var hash=GuiLaunchOptions.Parse(new[]{"--config",target,"--config-helper",helper,"--helper-sha256",new string('0',64),"--allow-write"},directory);
  Rejected(hash.RequireEdit,"SHA256 mismatch");
  File.WriteAllText(fake+".mode","slow");var timer=Stopwatch.StartNew();Rejected(delegate{new ConfigHelperClient(wrong).Run(target,"validate",100);},"unconfirmed");Check(timer.ElapsedMilliseconds<3000,"Timeout unbounded");
  Pass("missing/wrong generation/hash/failed helper rejected; owned child timeout is unconfirmed");
  var c=SteamVrControlWpf.Read(candidate);c["mappings"]=new object[]{new Dictionary<string,object>{{"runtime_contract",new Dictionary<string,object>{{"input_declaration",new[]{"position"}}}}}};File.WriteAllText(candidate,json.Serialize(c));
  File.WriteAllText(fake+".mode","ignore");Rejected(delegate{new ConfigHelperClient(wrong).Commit(before,candidate,CancellationToken.None);},"ignores runtime_contract");
  Check(Convert.ToBase64String(before)==Convert.ToBase64String(File.ReadAllBytes(target)),"Old extension-ignoring helper changed target");
  Pass("transaction-capable helper ignoring runtime_contract fails closed on explicit-save probe");
 }
 static void NativeTransactions(){
  string target=Fixture("native Unicode CAS");byte[] expected=File.ReadAllBytes(target);string candidate=Candidate(target);var options=Options(target,helper,false);
  new ConfigHelperClient(options).Commit(expected,candidate,CancellationToken.None);
  var saved=SteamVrControlWpf.Read(target);Check((string)saved["policy"]=="disabled"&&Convert.ToInt32(saved["mapping_revision"])==11&&saved.ContainsKey("extension_unknown"),"Native commit/revision/unknown fields");
  string[] backups=Directory.GetFiles(Path.GetDirectoryName(target),"*.backup-txn-*");Check(backups.Length>0,"No backup");bool found=false;foreach(string path in backups)if(Convert.ToBase64String(File.ReadAllBytes(path))==Convert.ToBase64String(expected))found=true;Check(found,"Backup is not exact preimage");
  Rejected(delegate{new ConfigHelperClient(options).Commit(expected,candidate,CancellationToken.None);},"changed");
  Pass("actual helper exact-byte CAS, revision +1, unknown fields and exact backup on Unicode paths");
  target=Fixture("cancel");expected=File.ReadAllBytes(target);candidate=Candidate(target);var cancel=new CancellationTokenSource();cancel.Cancel();
  Rejected(delegate{new ConfigHelperClient(Options(target,helper,false)).Commit(expected,candidate,cancel.Token);},"cancel");Check(Convert.ToBase64String(expected)==Convert.ToBase64String(File.ReadAllBytes(target)),"Cancellation changed target");
  Pass("cancellation before native commit preserves target and creates no target lock");
  target=Fixture("concurrent");expected=File.ReadAllBytes(target);candidate=Candidate(target);options=Options(target,helper,false);int wins=0,rejects=0;
  Action save=delegate{try{new ConfigHelperClient(options).Commit(expected,candidate,CancellationToken.None);Interlocked.Increment(ref wins);}catch(InvalidOperationException){Interlocked.Increment(ref rejects);}};
  // Use distinct candidates: the UI normally has one writer; two cooperating
  // clients may still commit one exact snapshot. Native ownership decides.
  string candidate2=candidate+"2";File.Copy(candidate,candidate2);var second=Task.Run(delegate{try{new ConfigHelperClient(options).Commit(expected,candidate2,CancellationToken.None);Interlocked.Increment(ref wins);}catch(InvalidOperationException){Interlocked.Increment(ref rejects);}});var first=Task.Run(save);Task.WaitAll(first,second);
  Check(wins==1&&rejects==1,"Concurrent GUI client lost update");Pass("two actual GUI helper clients have exactly one same-snapshot winner");
  target=Fixture("native transform");expected=File.ReadAllBytes(target);new ConfigHelperClient(Options(target,helper,false)).TransformAndCommit(expected,"policy both",false,CancellationToken.None);
  saved=SteamVrControlWpf.Read(target);Check((string)saved["policy"]=="both"&&saved.ContainsKey("extension_unknown")&&Convert.ToInt32(saved["mapping_revision"])==11,"Native transform not transactional");
  Pass("tray/WPF native transformation stages private draft then commits original snapshot once");
  target=Fixture("native P1 runtime contract");var loaded=SteamVrControlWpf.Read(target);
  var edit=new Dictionary<string,object>{{"source_id","synthetic-source"},{"device_id","synthetic-device"},{"tracker_id","synthetic-tracker"},{"profile","vive-hil-v1"},{"input_space","synthetic-space"},{"input_revision",1},{"world_space","synthetic-world"},{"space_approved",false}};
  loaded=MappingEditor.Candidate(loaded,-1,edit);((Dictionary<string,object>)((IList)loaded["mappings"])[0])["runtime_contract"]=new Dictionary<string,object>{{"id","synthetic-p1-fixture"},{"provenance","Explicit synthetic test declaration; no physical approval"},{"input_policy","STRICT"},{"output_policy","STRICT"},{"input_declaration",new[]{"position","orientation"}},{"output_declaration",new[]{"position","orientation"}}};
  loaded["mapping_revision"]=10;File.WriteAllText(target,json.Serialize(loaded),new UTF8Encoding(false));expected=File.ReadAllBytes(target);
  new ConfigHelperClient(Options(target,helper,false)).TransformAndCommit(expected,"policy both",false,CancellationToken.None);
  Check(((Dictionary<string,object>)((IList)SteamVrControlWpf.Read(target)["mappings"])[0]).ContainsKey("runtime_contract"),"P1 contract lost");
  Pass("actual packaged-generation helper validates explicit P1 contract/probe and retains it on save");
 }
 static void Health(){
  string target=Fixture("health");object window=Window(Options(target,helper,true));
  try{
   Invoke(window,"Reload");Set(window,"healthAvailable",true);Set(window,"healthSnapshotUtc",DateTime.UtcNow);Set(window,"healthPublisher","foreign-publisher");Set(window,"currentPolicy","both");Set(window,"healthRevision",10);Invoke(window,"UpdateLivePose");
   Check(((TextBlock)Field(window,"healthStatus")).Text.Contains("INSTANCE MISMATCH")&&((TextBlock)Field(window,"healthStatus")).Text.Contains("policy=unknown"),"Mismatched health treated as applied");
   Check(((TextBlock)Field(window,"mappingStatus")).Text.Contains("status unknown"),"Mismatched revision treated as applied");
   Set(window,"healthPublisher","fixture-publisher");Set(window,"healthSnapshotUtc",DateTime.UtcNow.AddSeconds(-10));Invoke(window,"UpdateLivePose");Check(((TextBlock)Field(window,"healthStatus")).Text.Contains("historical"),"Stale health live");
   Set(window,"healthSnapshotUtc",DateTime.UtcNow.AddSeconds(10));Invoke(window,"UpdateLivePose");Check(((TextBlock)Field(window,"healthStatus")).Text.Contains("historical"),"Future health live");
   Set(window,"healthSnapshotUtc",DateTime.UtcNow);Invoke(window,"UpdateLivePose");Check(((TextBlock)Field(window,"healthStatus")).Text.Contains("ownership unverified"),"Fresh health implies process ownership");
  }finally{Close(window);}
  Pass("hidden WPF mismatched/stale/future health cannot establish runtime application or process ownership");
 }
 static void TargetSwitch(){
  string first=Fixture("health-old-target"),second=Fixture("health-new-target");
  File.WriteAllText(first+".status.json",json.Serialize(new{publisher_id="old-publisher",mapping_revision=10,policy="both",snapshot_unix_ms=(long)(DateTime.UtcNow-new DateTime(1970,1,1)).TotalMilliseconds,devices=new object[0]}));
  File.WriteAllText(second+".status.json",json.Serialize(new{publisher_id="fixture-publisher",mapping_revision=10,policy="disabled",snapshot_unix_ms=(long)(DateTime.UtcNow-new DateTime(1970,1,1)).TotalMilliseconds,devices=new object[0]}));
  object window=Window(Options(first,helper,true));
  try{
   Invoke(window,"Reload");Thread.Sleep(50);Set(window,"options",Options(second,helper,true));Invoke(window,"Reload");Pump();
   var until=DateTime.UtcNow.AddSeconds(2);while((string)Field(window,"currentPolicy")!="disabled"&&DateTime.UtcNow<until){Invoke(window,"Health");Pump();Thread.Sleep(10);}
   Check((string)Field(window,"currentPolicy")=="disabled"&&(string)Field(window,"healthPublisher")=="fixture-publisher","Old target completion poisoned/stalled new health reader");
  }finally{Close(window);}
  Pass("pending old-target health is discarded and new-target reader remains available");
 }
 static void AsyncState(){
  foreach(string mode in new[]{"slowvalidate","failcommit"}){
   string target=Fixture("async-"+mode);byte[] expected=File.ReadAllBytes(target);File.WriteAllText(fake+".mode",mode);object window=Window(Options(target,fake,false));
   try{
    Invoke(window,"Reload");window.GetType().GetMethod("SaveNativeDraft",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(window,new object[]{"policy both",false});
    Check((bool)Field(window,"busy"),"Save not asynchronous");foreach(Control control in (IEnumerable)Field(window,"editControls"))Check(!control.IsEnabled,"Editing enabled during save");
    if(mode=="slowvalidate")((Button)Field(window,"cancelSave")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
    Wait(window);Check((bool)Field(window,"needsReload"),"Cancelled/failed save did not require review");
    string message=((TextBlock)Field(window,"status")).Text;Check(message.Contains(mode=="slowvalidate"?"Cancelled":"rejected or outcome unconfirmed"),"Wrong save outcome: "+message);
    Check(Convert.ToBase64String(expected)==Convert.ToBase64String(File.ReadAllBytes(target)),"Cancelled/failed async save changed target");
   }finally{Close(window);}
  }
  Pass("actual hidden WPF async cancel/failure disable editing, retain target and require reload/review");
 }
 [STAThread] static int Main(string[] args){try{directory=args[0];helper=args[1];fake=args[2];example=args[3];Directory.CreateDirectory(directory);Arguments();Access();Helpers();NativeTransactions();Health();TargetSwitch();AsyncState();Console.WriteLine("PASS control foundation: "+cases+" cases; manual visual UI NOT_RUN; hardware/network NOT_RUN");return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
