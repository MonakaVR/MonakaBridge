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
 static string Json(object value){return new System.Web.Script.Serialization.JavaScriptSerializer().Serialize(Canonical(value));}
 static CalibrationProcessClient Client(CalibrationPanel view){return (CalibrationProcessClient)Field(view,"client");}
 sealed class Run:IDisposable {
  internal string Directory,Config;internal byte[] Original;internal GuiLaunchOptions Options;internal Window Host;internal CalibrationPanel View;
  internal Run(string label,bool readOnly,string mode){
   Directory=Path.Combine(root,label+Guid.NewGuid().ToString("N").Substring(0,8));System.IO.Directory.CreateDirectory(Directory);
   Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_ROOT",Path.Combine(Directory,"seed"));Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MODE","");Dictionary<string,object> seed;
   using(var process=CalibrationOwnedProcess.Start(fixture,"--fixture-seed",true,null,null)){seed=(Dictionary<string,object>)CalibrationJson.Object(CalibrationPipeFrames.ReadLine(process.Output,65536),65536,32768)["test_only_fixture_open"];Check(process.WaitForExit(3000)&&process.ExitCode==0,"seed");}
   Config=(string)seed["config_path"];
   if(mode=="recalibrate-trajectory"||mode=="recalibrate-static"){
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
   var document=new Dictionary<string,object>{{"schema",CalibrationInstallation.Schema},{"name",label},{"config",Config},{"health",Config+".status.json"},{"config_helper",configHelper},{"helper_sha256",CalibrationProfileDraft.FileHash(configHelper)},{"read_only",readOnly},{"calibration_helper",fixture},{"calibration_helper_sha256",CalibrationProfileDraft.FileHash(fixture)},{"calibration_api",1},{"calibration_software_policy",1},{"calibration_gui_contract",1},{"bridge_id",seed["bridge_id"]},{"installation_path",seed["installation_path"]},{"association",CalibrationInstallation.Association},{"single_bridge_no_relay",true},{"mirror_address",seed["mirror_address"]},{"mirror_port",seed["mirror_port"]},{"source",seed["source"]},{"device",seed["device"]},{"tracker",seed["tracker"]},{"input_space",seed["input_space"]},{"input_revision",seed["input_revision"]},{"reference_role",seed["reference_role"]}};
   var profile=CalibrationInstallation.SaveNew(Path.Combine(Directory,"profile.json"),document,new[]{Config,fixture,configHelper});Options=GuiLaunchOptions.Parse(new[]{"--installation",profile.ProfilePath},null);
   Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_ROOT",Path.Combine(Directory,"worker"));Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MODE",mode);
   Host=(Window)Activator.CreateInstance(typeof(SteamVrControlWpf).GetNestedType("ControlWindow",BindingFlags.NonPublic),new object[]{Options});View=(CalibrationPanel)Field(Host,"calibration");Invoke(Host,"Reload");
   Check(Client(View)==null,"constructor/reload started native worker");
  }
  internal void Result(bool stallUi=false,bool staticMode=false){
   if(staticMode)((ComboBox)Field(View,"mode")).SelectedIndex=1;
   Click(View,"open");Until(delegate{return Client(View).Snapshot().Phase=="OPEN"&&!(bool)Field(View,"pending");});
   foreach(Control control in (IEnumerable)Field(Host,"editControls"))Check(!control.IsEnabled,"config edit enabled during owned calibration");
   Click(View,"discover");Until(delegate{return Client(View).Snapshot().Phase=="DISCOVERED"&&!(bool)Field(View,"pending");});Check(((TextBox)Field(View,"evidence")).Text.Contains("standing_witness"),"full native witness not displayed");
   Click(View,"confirm");Until(delegate{return Client(View).Snapshot().Phase=="CONFIRMED"&&!(bool)Field(View,"pending");});Click(View,"begin");
   if(staticMode)for(int i=0;i<3;i++){
    Until(delegate{return Client(View).Snapshot().Phase=="WAITING_POINT"&&!(bool)Field(View,"pending");});
    Click(View,"capturePoint");Until(delegate{return !(bool)Field(View,"pending");});
   }
   if(stallUi){var watch=Stopwatch.StartNew();while(Client(View).Snapshot().Result==null){Check(watch.ElapsedMilliseconds<10000,"native reader depended on UI pump");Thread.Sleep(5);}}Until(delegate{return Client(View).Snapshot().Result!=null&&!(bool)Field(View,"pending");});Check(((TextBox)Field(View,"evidence")).Text.Contains("mount_existing"),"native W/E/M result not displayed");
  }
  public void Dispose(){Host.Close();Until(delegate{return !View.Active;});Pump();Host.Close();}
 }
 [STAThread]static int Main(string[] args){try{
  Check(args.Length==3,"fixture, native config helper, evidence root");fixture=Path.GetFullPath(args[0]);configHelper=Path.GetFullPath(args[1]);root=Path.GetFullPath(args[2]);System.IO.Directory.CreateDirectory(root);SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
  using(var run=new Run("same-candidate",false,"")){
   run.Result();Click(run.View,"prepare");Until(delegate{return Client(run.View).Snapshot().Prepared!=null&&!(bool)Field(run.View,"pending");});var prepared=Client(run.View).Snapshot().Prepared;byte[] candidate=File.ReadAllBytes(prepared.CandidatePath);
   Check(((TextBox)Field(run.View,"preview")).Text.Contains(prepared.CandidateSha256),"candidate hash not shown");Check(!((Button)Field(run.View,"save")).IsEnabled,"unreviewed candidate save enabled");((CheckBox)Field(run.View,"reviewed")).IsChecked=true;Click(run.View,"save");Until(delegate{return Client(run.View).Snapshot().ActualOutcome=="SAVED"&&!(bool)Field(run.View,"pending");});
   Check(Convert.ToBase64String(candidate)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"WPF did not commit same exact native candidate");Click(run.View,"cancel");Until(delegate{return !run.View.Active;});Check((bool)Field(run.Host,"needsReload"),"native save did not require config reload/review");Check(!((Button)Field(run.View,"open")).IsEnabled,"new session enabled before review");Invoke(run.Host,"Reload");Click(run.View,"fresh");Until(delegate{return Client(run.View)==null;});cases++;
  }
  foreach(string mode in new[]{"recalibrate-trajectory","recalibrate-static","space-only"})using(var run=new Run(mode,false,mode)){
   run.Result(false,mode=="recalibrate-static");
   Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"open/discover/measure changed saved W/M before Save");
   string resultId=Client(run.View).Snapshot().Result.ResultId;
   Click(run.View,"prepare");Until(delegate{return Client(run.View).Snapshot().Prepared!=null&&!(bool)Field(run.View,"pending");});
   var prepared=Client(run.View).Snapshot().Prepared;byte[] candidate=File.ReadAllBytes(prepared.CandidatePath);
   Check(Client(run.View).Snapshot().Result.ResultId==resultId,"prepare measured again");
   Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"prepare changed saved config");
   var before=Row(run.Original,"pico","device");var after=Row(candidate,"pico","device");
   Check(Json(before["mount"])==Json(after["mount"]),"recalibration changed mount");
   Check(Json(Row(run.Original,"other","device"))==Json(Row(candidate,"other","device")),"same-name other opaque source changed");
   Check((string)after["input_space"]==(mode=="space-only"?"current-frame":"native")&&Convert.ToInt64(after["input_revision"])==(mode=="space-only"?1:2),"measured World saved against wrong frame");
   Check(Json(before["world"])!=Json(after["world"]),"measured World not updated");
   Check(!((Button)Field(run.View,"save")).IsEnabled,"unreviewed save enabled");((CheckBox)Field(run.View,"reviewed")).IsChecked=true;Click(run.View,"save");
   Until(delegate{return Client(run.View).Snapshot().ActualOutcome=="SAVED"&&!(bool)Field(run.View,"pending");});
   Check(Convert.ToBase64String(candidate)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"recalibration did not save exact reviewed candidate");
   Check(Client(run.View).Snapshot().Result.ResultId==resultId,"Save remeasured result");
   Click(run.View,"cancel");Until(delegate{return !run.View.Active;});Invoke(run.Host,"Reload");Click(run.View,"fresh");Until(delegate{return Client(run.View)==null;});
   Check(run.Options.Calibration.InputRevision==1,"test rewrote installation profile frame");
   Click(run.View,"open");Until(delegate{return Client(run.View).Snapshot().Phase=="OPEN"&&!(bool)Field(run.View,"pending");});Click(run.View,"discover");Until(delegate{return Client(run.View).Snapshot().Phase=="DISCOVERED"&&!(bool)Field(run.View,"pending");});
   Check(Convert.ToBase64String(candidate)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"same-profile reopen changed saved config");
   if(mode=="recalibrate-trajectory"){
    Click(run.View,"confirm");Until(delegate{return Client(run.View).Snapshot().Phase=="CONFIRMED"&&!(bool)Field(run.View,"pending");});Click(run.View,"begin");Until(delegate{return Client(run.View).Snapshot().Result!=null&&!(bool)Field(run.View,"pending");});
    Click(run.View,"prepare");Until(delegate{return Client(run.View).Snapshot().Prepared!=null&&!(bool)Field(run.View,"pending");});
    Check(Convert.ToBase64String(candidate)==Convert.ToBase64String(File.ReadAllBytes(Client(run.View).Snapshot().Prepared.CandidatePath)),"same measured W was not an exact no-op with old-frame installation profile");
    ((CheckBox)Field(run.View,"reviewed")).IsChecked=true;Click(run.View,"save");Until(delegate{return Client(run.View).Snapshot().ActualOutcome=="SAVED"&&!(bool)Field(run.View,"pending");});
    Check(Convert.ToBase64String(candidate)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"same W Save changed exact config bytes");
   }
   Console.WriteLine("PASS current-frame WPF production handlers -> RuntimeCaptureProvider -> "+mode+" result="+resultId+" candidate="+prepared.CandidateSha256+" explicit Save");cases++;
  }
  using(var run=new Run("cancel-new-revision",false,"revision-only")){
   Click(run.View,"open");Until(delegate{return Client(run.View).Snapshot().Phase=="OPEN"&&!(bool)Field(run.View,"pending");});Click(run.View,"discover");Until(delegate{return Client(run.View).Snapshot().Phase=="DISCOVERED"&&!(bool)Field(run.View,"pending");});
   Click(run.View,"cancel");Until(delegate{return !run.View.Active;});Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"cancel modified saved config");cases++;
  }
  using(var run=new Run("mid-revision",false,"mid-revision")){
   Click(run.View,"open");Until(delegate{return Client(run.View).Snapshot().Phase=="OPEN"&&!(bool)Field(run.View,"pending");});Click(run.View,"discover");Until(delegate{return Client(run.View).Snapshot().Phase=="DISCOVERED"&&!(bool)Field(run.View,"pending");});Click(run.View,"confirm");Until(delegate{return Client(run.View).Snapshot().Phase=="CONFIRMED"&&!(bool)Field(run.View,"pending");});Click(run.View,"begin");
   Until(delegate{return !(bool)Field(run.View,"pending");});Check(Client(run.View).Snapshot().Result==null&&Client(run.View).Snapshot().Phase!="RESULT_READY","mid-measurement revision published result");Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"failed measurement changed saved config");cases++;
  }
  using(var run=new Run("readonly",true,"")){run.Result();Check(!((Button)Field(run.View,"prepare")).IsEnabled&&!((Button)Field(run.View,"save")).IsEnabled,"read-only measurement became writable");Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"read-only changed config");cases++;}
  using(var run=new Run("close-blocked-discover",false,"busy-discover")){
   Click(run.View,"open");Until(delegate{return Client(run.View).Snapshot().Phase=="OPEN"&&!(bool)Field(run.View,"pending");});Click(run.View,"discover");Until(delegate{return File.Exists(Path.Combine(run.Directory,"worker","busy-discover-entered.fixture"));});
   run.Host.Close();Check(run.View.Active,"window claimed shutdown before blocked owned worker exit");Until(delegate{return Client(run.View).Snapshot().Stopped;});Check(!Client(run.View).Snapshot().CommitMayHaveStarted,"close began commit");Check(Convert.ToBase64String(run.Original)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"shutdown changed config");cases++;
  }
  using(var run=new Run("slow-ui",true,"")){run.Result(true);Check(Client(run.View).Snapshot().Result!=null,"UI stall lost native result");cases++;}
  using(var run=new Run("switch-blocked",false,"busy-discover")){
   Click(run.View,"open");Until(delegate{return Client(run.View).Snapshot().Phase=="OPEN"&&!(bool)Field(run.View,"pending");});Click(run.View,"discover");Until(delegate{return File.Exists(Path.Combine(run.Directory,"worker","busy-discover-entered.fixture"));});var old=Client(run.View);
   var next=GuiLaunchOptions.Parse(new[]{"--config",run.Config,"--health",run.Config+".status.json","--config-helper",configHelper,"--read-only"},run.Directory);
   run.Host.GetType().GetMethod("SwitchInstallation",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(run.Host,new object[]{next});Check(object.ReferenceEquals(Field(run.Host,"options"),run.Options),"target switched before old child stopped");Until(delegate{return object.ReferenceEquals(Field(run.Host,"options"),next);});Check(old.Snapshot().Stopped&&Client(run.View)==null&&!((Button)Field(run.View,"open")).IsEnabled,"stale worker/target survived switch");cases++;
  }
  using(var run=new Run("unknown-save",false,"post-replace-unknown")){
   run.Result();Click(run.View,"prepare");Until(delegate{return Client(run.View).Snapshot().Prepared!=null&&!(bool)Field(run.View,"pending");});var candidate=File.ReadAllBytes(Client(run.View).Snapshot().Prepared.CandidatePath);((CheckBox)Field(run.View,"reviewed")).IsChecked=true;Click(run.View,"save");Until(delegate{return Client(run.View).Snapshot().ActualOutcome=="UNKNOWN"&&!(bool)Field(run.View,"pending");});Check(Client(run.View).Snapshot().CommitMayHaveStarted&&Convert.ToBase64String(candidate)==Convert.ToBase64String(File.ReadAllBytes(run.Config)),"UNKNOWN lost actual candidate/rolled back");Check(!((Button)Field(run.View,"save")).IsEnabled,"UNKNOWN automatically retried");Click(run.View,"cancel");Until(delegate{return !run.View.Active;});Check(Client(run.View).Snapshot().ActualOutcome=="UNKNOWN"&&(bool)Field(run.Host,"needsReload"),"UNKNOWN erased on close");cases++;
  }
  Console.WriteLine("PASS normal WPF unshown controls "+cases+" flows: explicit witness/result/exact-candidate save, readonly measurement, blocked discover/async owned shutdown; no Show/NotifyIcon/realSDK/socket");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}finally{Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_ROOT",null);Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MODE",null);}}
}
