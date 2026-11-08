using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using MonakaBridge;
class CalibrationWpfTests {
 static string fixture,configHelper,root;static int cases;
 static void Check(bool okay,string reason){if(!okay)throw new Exception(reason);}
 static object Field(object value,string name){return value.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(value);}
 static void Invoke(object value,string name){value.GetType().GetMethod(name,BindingFlags.Instance|BindingFlags.NonPublic,null,Type.EmptyTypes,null).Invoke(value,null);}
 static void Pump(){Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);}
 static void Until(Func<bool> ready){var watch=Stopwatch.StartNew();while(!ready()){Check(watch.ElapsedMilliseconds<22000,"unshown WPF callback deadline");Pump();Thread.Sleep(5);}Pump();}
 static void Click(CalibrationPanel view,string name){var button=(Button)Field(view,name);Until(delegate{return button.IsEnabled;});button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Pump();}
 static Dictionary<string,object> Row(byte[] bytes,string source,string device){
  var document=CalibrationJson.Object(bytes,65536,32768);
  foreach(Dictionary<string,object> row in (IEnumerable)document["mappings"])if((string)row["source_id"]==source&&(string)row["device_id"]==device)return row;
  throw new Exception("mapping missing");
 }
 static object Canonical(object value){
  var map=value as Dictionary<string,object>;if(map!=null){var sorted=new SortedDictionary<string,object>(StringComparer.Ordinal);foreach(var item in map)sorted.Add(item.Key,Canonical(item.Value));return sorted;}
  var list=value as IList;if(list!=null){var result=new ArrayList();foreach(var item in list)result.Add(Canonical(item));return result;}
  if(value is double||value is long||value is int)return Convert.ToDouble(value);return value;
 }
 static string Candidate(CalibrationPanel view){return ((CalibrationWorkflow)Field(view,"workflow")).CandidateSha256;}
 static string Json(object value){return new System.Web.Script.Serialization.JavaScriptSerializer().Serialize(Canonical(value));}
 sealed class Run:IDisposable {
  internal string Directory,Config;internal byte[] Original;internal GuiLaunchOptions Options;internal Window Host;internal CalibrationPanel View;
  internal Run(string label,bool readOnly,string mode){
   Directory=Path.Combine(root,label+Guid.NewGuid().ToString("N").Substring(0,8));System.IO.Directory.CreateDirectory(Directory);
   Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_ROOT",Path.Combine(Directory,"seed"));Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MODE","");Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MIRROR_PORT","29813");Dictionary<string,object> seed;
   using(var process=CalibrationOwnedProcess.Start(fixture,"--fixture-seed",true,null,null)){seed=(Dictionary<string,object>)CalibrationJson.Object(CalibrationPipeFrames.ReadLine(process.Output,65536),65536,32768)["test_only_fixture_open"];Check(process.WaitForExit(3000)&&process.ExitCode==0,"seed");}
   Config=(string)seed["config_path"];
   if(mode=="recalibrate-trajectory"){
    var calibrationDoc=CalibrationJson.Object(File.ReadAllBytes(Config),65536,32768);
    foreach(Dictionary<string,object> row in (IEnumerable)calibrationDoc["mappings"]){
     row["display_name"]="same display name";
     if((string)row["source_id"]=="other")row["tracker_id"]="logical-pico";
     else if((string)row["source_id"]=="pico"){
      row["world"]=new Dictionary<string,object>{{"rotation",new[]{0,0,0,1}},{"translation",new[]{6,7,8}}};
      row["mount"]=new Dictionary<string,object>{{"rotation",new[]{0,0,1,0}},{"translation",new[]{0,0,0}}};
     }
    }
    File.WriteAllBytes(Config,CalibrationJson.Encode(calibrationDoc,65536));
   }
   Original=File.ReadAllBytes(Config);
   string helpers=Path.Combine(Directory,"product");System.IO.Directory.CreateDirectory(helpers);
   string selectedHelper=Path.Combine(helpers,"monaka_bridge_config.exe");File.Copy(configHelper,selectedHelper);File.Copy(fixture,Path.Combine(helpers,"monaka_bridge_calibrator.exe"));
   Options=GuiLaunchOptions.Parse(new[]{(string)seed["installation_path"],"--config",Config,"--health",Config+".status.json","--config-helper",selectedHelper,readOnly?"--read-only":"--allow-write"},null);
   Check(Options.Calibration==null,"normal flow unexpectedly requires installation profile");
   Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_ROOT",Path.Combine(Directory,"worker"));Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MODE",mode);
   Host=(Window)Activator.CreateInstance(typeof(SteamVrControlWpf).GetNestedType("ControlWindow",BindingFlags.NonPublic),new object[]{Options});View=(CalibrationPanel)Field(Host,"calibration");Invoke(Host,"Reload");
   var choices=(ComboBox)Field(View,"tracker");for(int i=0;i<choices.Items.Count;i++)if(choices.Items[i].ToString()=="logical-pico | pico / device")choices.SelectedIndex=i;
   Check(choices.SelectedItem!=null&&choices.SelectedItem.ToString()=="logical-pico | pico / device","persisted identity selector missing");Check(!View.Active&&View.Snapshot().Phase=="NEW","constructor/reload started native worker");
  }
  internal void Result(bool stallUi=false){
   Click(View,"start");Check(View.Active,"Start did not take ownership");
   foreach(Control control in (IEnumerable)Field(Host,"editControls"))Check(!control.IsEnabled,"config edit enabled during owned calibration");
   if(stallUi){var watch=Stopwatch.StartNew();while(View.Snapshot().ActualOutcome!="SAVED"){Check(watch.ElapsedMilliseconds<15000,"native workflow depended on UI pump: "+View.Snapshot().Fault);Thread.Sleep(5);}}
   Until(delegate{return !View.Active&&!(bool)Field(View,"pending");});
  }
  internal void Saved(){Result();var s=View.Snapshot();Check(s.ActualOutcome!=null&&s.ActualOutcome.StartsWith("SAVED",StringComparison.Ordinal),"automatic save failed: "+s.Fault+" / "+((TextBlock)Field(View,"state")).Text);Check(s.Stopped,"Done retained a running worker");Check(Candidate(View)!=null&&CalibrationInstallation.Sha256(File.ReadAllBytes(Config))==Candidate(View),"saved bytes differ from native candidate SHA");Check(((TextBlock)Field(View,"state")).Text.Contains("Done"),"saved result not visible after cleanup/reload");Check(!(bool)Field(Host,"needsReload"),"known save did not auto reload");Check(((Button)Field(View,"start")).IsEnabled,"second Start requires hidden manual step");Check(!File.Exists(Path.Combine(Options.Root,".calibration-selection-memory.json")),"normal calibration wrote a selection profile");}
  public void Dispose(){Host.Close();Until(delegate{return !View.Active;});Pump();Host.Close();}
 }
 [STAThread]static int Main(string[] args){try{
  Check(args.Length==3,"fixture, native config helper, evidence root");fixture=Path.GetFullPath(args[0]);configHelper=Path.GetFullPath(args[1]);root=Path.GetFullPath(args[2]);System.IO.Directory.CreateDirectory(root);SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
  using(var run=new Run("normal-auto-save",false,"")){
   run.Saved();run.View.StopAsync().GetAwaiter().GetResult();Check(((TextBlock)Field(run.View,"state")).Text.Contains("Done")&&!((TextBlock)Field(run.View,"state")).Text.Contains("cancelled"),"late Cancel overrode actual native SAVED outcome");byte[] first=File.ReadAllBytes(run.Config);string resultId=run.View.Snapshot().Result.ResultId;
   Check(Json(Row(run.Original,"pico","device")["mount"])==Json(Row(first,"pico","device")["mount"]),"automatic save changed existing mount");
   run.Saved();Check(resultId!=run.View.Snapshot().Result.ResultId,"second Start reused a prior measurement");Check(Convert.ToBase64String(first)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"identical World save changed exact config bytes");cases++;
  }
  foreach(string mode in new[]{"recalibrate-trajectory","space-only"})using(var run=new Run(mode,false,mode)){
   run.Saved();byte[] after=File.ReadAllBytes(run.Config);var before=Row(run.Original,"pico","device");var changed=Row(after,"pico","device");
   Check(Json(before["mount"])==Json(changed["mount"]),"recalibration changed mount");Check(Json(Row(run.Original,"other","device"))==Json(Row(after,"other","device")),"same-name other opaque source changed");
   Check((string)changed["input_space"]==(mode=="space-only"?"current-frame":"native")&&Convert.ToInt64(changed["input_revision"])==(mode=="space-only"?1:2),"measured World saved against wrong frame");Check(Json(before["world"])!=Json(changed["world"]),"measured World not updated");
   if(mode=="recalibrate-trajectory"){run.Saved();Check(Convert.ToBase64String(after)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"same measured World was not a no-op after automatic configuration reload");}cases++;
  }
  using(var run=new Run("mid-revision",false,"mid-revision")){
   run.Result();Check(run.View.Snapshot().Result==null,"mid-measurement revision published result");Check(run.View.Snapshot().ActualOutcome==null||!run.View.Snapshot().ActualOutcome.StartsWith("SAVED"),"failed measurement labelled saved");Check(!((TextBlock)Field(run.View,"state")).Text.Contains("Done"),"failed measurement displayed Done");Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"failed measurement changed saved config");cases++;
  }
  using(var run=new Run("readonly",true,"")){Check(((Button)Field(run.View,"start")).IsEnabled,"read-only measurement disabled");run.Result();var s=run.View.Snapshot();Check(s.Result!=null&&s.ActualOutcome=="MEASURED_READ_ONLY"&&s.Prepared==null&&!s.CommitMayHaveStarted&&s.Stopped,"readonly measurement did not complete without preparing/committing");Check(((TextBlock)Field(run.View,"state")).Text.Contains("not saved")&&!((TextBlock)Field(run.View,"state")).Text.Contains("Done"),"readonly result mislabelled saved");Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"readonly mutated config");cases++;}
  using(var run=new Run("cancel-recording",false,"")){
   Click(run.View,"start");Until(delegate{return run.View.Snapshot().Phase=="RECORDING";});Click(run.View,"cancel");Until(delegate{return !run.View.Active;});Check(run.View.Snapshot().Stopped,"native cancellation did not stop owned worker");Check(((TextBlock)Field(run.View,"state")).Text.Contains("cancelled"),"native CANCELLED not displayed");Check(!run.View.Snapshot().CommitMayHaveStarted&&Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"cancel recording wrote config");cases++;
  }
  using(var run=new Run("cancel-discover",false,"busy-discover")){
   Click(run.View,"start");Until(delegate{return File.Exists(Path.Combine(run.Directory,"worker","busy-discover-entered.fixture"));});Click(run.View,"cancel");Until(delegate{return !run.View.Active;});Check(run.View.Snapshot().Stopped&&!run.View.Snapshot().CommitMayHaveStarted,"cancel worker cleanup/commit guard failed");Check(((TextBlock)Field(run.View,"state")).Text.Contains("cancelled"),"cancel outcome absent from normal status");Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"cancel changed config");cases++;
  }
  using(var run=new Run("close-blocked-discover",false,"busy-discover")){
   Click(run.View,"start");Until(delegate{return File.Exists(Path.Combine(run.Directory,"worker","busy-discover-entered.fixture"));});run.Host.Close();Check(run.View.Active,"window claimed shutdown before blocked owned worker exit");Until(delegate{return !run.View.Active;});Check(run.View.Snapshot().Stopped&&!run.View.Snapshot().CommitMayHaveStarted,"close started commit or lost worker");Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"shutdown changed config");cases++;
  }
  using(var run=new Run("slow-ui",false,"")){run.Result(true);Check(run.View.Snapshot().ActualOutcome=="SAVED","UI stall lost automatic result");Check(!(bool)Field(run.Host,"needsReload"),"UI stall lost config reload");cases++;}
  using(var run=new Run("switch-blocked",false,"busy-discover")){
   Click(run.View,"start");Until(delegate{return File.Exists(Path.Combine(run.Directory,"worker","busy-discover-entered.fixture"));});var old=(CalibrationWorkflow)Field(run.View,"workflow");
   var next=GuiLaunchOptions.Parse(new[]{"--config",run.Config,"--health",run.Config+".status.json","--config-helper",configHelper,"--read-only"},run.Directory);
   run.Host.GetType().GetMethod("SwitchInstallation",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(run.Host,new object[]{next});Check(object.ReferenceEquals(Field(run.Host,"options"),run.Options),"target switched before old worker stopped");Until(delegate{return object.ReferenceEquals(Field(run.Host,"options"),next)&&!(bool)Field(run.Host,"switching")&&((Button)Field(run.View,"start")).IsEnabled;});Check(old.Snapshot().Stopped&&run.View.Snapshot().Phase=="NEW"&&((Button)Field(run.View,"start")).IsEnabled,"stale worker/target survived switch");cases++;
  }
  using(var run=new Run("unknown-save",false,"post-replace-unknown")){
   run.Result();var s=run.View.Snapshot();Check(s.ActualOutcome=="UNKNOWN"&&s.CommitMayHaveStarted,"UNKNOWN outcome lost");Check(Candidate(run.View)!=null&&CalibrationInstallation.Sha256(File.ReadAllBytes(run.Config))==Candidate(run.View),"UNKNOWN lost actual candidate/rolled back");Check(s.Stopped&&(bool)Field(run.Host,"needsReload"),"UNKNOWN lost after cleanup or not reload guarded");Check(!((Button)Field(run.View,"start")).IsEnabled&&!((TextBlock)Field(run.View,"state")).Text.Contains("Done"),"UNKNOWN retried or displayed success");Invoke(run.Host,"Reload");Check(run.View.Snapshot().ActualOutcome=="UNKNOWN","Reload erased unknown result");cases++;
  }
  Console.WriteLine("PASS normal WPF unshown controls "+cases+" flows: Start -> measure -> exact native save -> cleanup/auto reload; repeated Start, frame changes, opaque identity isolation, readonly, cancellation/close/switch, UNKNOWN, UI stall. No Show/real SDK/socket/HIL.");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}finally{Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_ROOT",null);Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MODE",null);Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MIRROR_PORT",null);}}
}
