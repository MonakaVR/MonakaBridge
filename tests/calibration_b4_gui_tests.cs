using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Threading;
using MonakaBridge;
class CalibrationB4GuiTests {
 static int cases;
 static void Check(bool okay,string reason){if(!okay)throw new Exception(reason);}
 static void Reject(Action action){bool rejected=false;try{action();}catch{rejected=true;}Check(rejected,"malformed approval review accepted");}
 static object Field(object value,string name){return value.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(value);}
 static void Until(Func<bool> ready){var watch=Stopwatch.StartNew();while(!ready()){Check(watch.ElapsedMilliseconds<22000,"B4 unshown control deadline");Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);Thread.Sleep(5);}Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);}
 static void Click(CalibrationPanel view,string name){var b=(Button)Field(view,name);Until(delegate{return b.IsEnabled;});b.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));}
 static CalibrationProcessClient Client(CalibrationPanel view){return (CalibrationProcessClient)Field(view,"client");}
 sealed class Run:IDisposable {
  readonly IDisposable baseline;internal CalibrationPanel View;internal string Config;internal byte[] Original;
  internal Run(string label,bool ro,string mode){var type=typeof(CalibrationWpfTests).GetNestedType("Run",BindingFlags.NonPublic);baseline=(IDisposable)Activator.CreateInstance(type,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{label,ro,mode},null);View=(CalibrationPanel)Field(baseline,"View");Config=(string)Field(baseline,"Config");Original=(byte[])Field(baseline,"Original");}
  internal void Step(string action,string phase){Click(View,action);Until(delegate{return Client(View).Snapshot().Phase==phase&&!(bool)Field(View,"pending");});}
  internal void DiscoverConfirm(){Step("discover","DISCOVERED");Step("confirm","CONFIRMED");}
  internal void Static(){((ComboBox)Field(View,"mode")).SelectedIndex=1;Step("open","OPEN");DiscoverConfirm();Step("begin","WAITING_POINT");for(int i=0;i<3;++i){string old=Client(View).Snapshot().PointToken;Click(View,"capturePoint");Until(delegate{return !(bool)Field(View,"pending")&&(Client(View).Snapshot().Result!=null||Client(View).Snapshot().PointToken!=old);});}Check(Client(View).Snapshot().Result!=null&&Client(View).Snapshot().Result.Static,"static result unavailable");}
  internal void Review(){Step("openApproval","OPEN");DiscoverConfirm();Step("reviewOutput","APPROVAL_REVIEW");}
  public void Dispose(){baseline.Dispose();}
 }
 static Dictionary<string,object> Config(string path){return CalibrationJson.Object(File.ReadAllBytes(path),2*1024*1024,32768);}
 [STAThread]static int Main(string[] args){try{
  Check(args.Length==3,"native fixture/config helper/root");
  foreach(var name in new[]{"fixture","configHelper","root"})typeof(CalibrationWpfTests).GetField(name,BindingFlags.Static|BindingFlags.NonPublic).SetValue(null,Path.GetFullPath(args[name=="fixture"?0:name=="configHelper"?1:2]));Directory.CreateDirectory(args[2]);SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
  using(var r=new Run("b4-static-save",false,"b4-static")){
   r.Static();Check(((TextBox)Field(r.View,"evidence")).Text.Contains("NOT ESTIMATED"),"static E/orientation explanation missing");Check(Convert.ToBase64String(r.Original)==Convert.ToBase64String(File.ReadAllBytes(r.Config)),"measurement wrote config");
   Click(r.View,"prepare");Until(delegate{return Client(r.View).Snapshot().Prepared!=null&&!(bool)Field(r.View,"pending");});var p=Client(r.View).Snapshot().Prepared;var exact=File.ReadAllBytes(p.CandidatePath);Check(!((Button)Field(r.View,"approveOutput")).IsEnabled,"world save became output approval");((CheckBox)Field(r.View,"reviewed")).IsChecked=true;Click(r.View,"save");Until(delegate{return Client(r.View).Snapshot().ActualOutcome=="SAVED";});Check(Convert.ToBase64String(exact)==Convert.ToBase64String(File.ReadAllBytes(r.Config)),"static exact candidate mismatch");foreach(Dictionary<string,object> member in (IList)Config(r.Config)["mappings"])if(Convert.ToString(member["source_id"])=="pico")Check(!Convert.ToBoolean(member["space_approved"]),"static save approved selected group output");cases++;
  }
  using(var r=new Run("b4-static-ro",true,"b4-static")){r.Static();Check(!((Button)Field(r.View,"prepare")).IsEnabled&&!((Button)Field(r.View,"save")).IsEnabled,"readonly static save enabled");Check(Convert.ToBase64String(r.Original)==Convert.ToBase64String(File.ReadAllBytes(r.Config)),"readonly static mutated config");cases++;}
  using(var r=new Run("b4-unadopted",false,"b4-unadopted")){
   r.Review();Check(((TextBox)Field(r.View,"evidence")).Text.Contains("PHYSICAL_CRITERIA_NOT_ADOPTED"),"native refusal reason not accessible");foreach(CheckBox c in (IEnumerable)Field(r.View,"memberChoices"))Check(!c.IsEnabled&&c.IsChecked==false,"unadopted member eligible");Check(!((Button)Field(r.View,"prepareOutput")).IsEnabled&&!((Button)Field(r.View,"approveOutput")).IsEnabled,"unadopted approval enabled");
   var typed=Client(r.View).Snapshot().Approval;foreach(string field in new[]{"eligible","selected"}){var wrong=typed.DisplayCopy();((Dictionary<string,object>)((IList)wrong["members"])[0])[field]=field=="eligible"?(object)"true":true;Reject(delegate{new CalibrationOutputReview(CalibrationJson.Encode(wrong,CalibrationContract.LineBytes));});}var duplicate=typed.DisplayCopy();var repeated=new ArrayList((ICollection)duplicate["members"]);repeated.Add(repeated[0]);duplicate["members"]=repeated;Reject(delegate{new CalibrationOutputReview(CalibrationJson.Encode(duplicate,CalibrationContract.LineBytes));});
   var native=Client(r.View).PrepareOutputAsync(new[]{"device"});Until(delegate{return native.IsCompleted;});Check(Convert.ToString(native.Result["reason"])=="PHYSICAL_CRITERIA_NOT_ADOPTED","UI bypass did not reach native refusal");Check(Convert.ToBase64String(r.Original)==Convert.ToBase64String(File.ReadAllBytes(r.Config)),"unadopted flow wrote config");cases++;
  }
  using(var r=new Run("b4-synthetic-approval",false,"b4-approval")){
   r.Review();Check(!((Button)Field(r.View,"prepareOutput")).IsEnabled,"approval had default selection");CheckBox selected=null;foreach(CheckBox c in (IEnumerable)Field(r.View,"memberChoices")){Check(c.IsChecked==false,"default selected member");if(c.IsEnabled)selected=c;}Check(selected!=null,"synthetic eligible member missing");selected.IsChecked=true;
   Click(r.View,"prepareOutput");Until(delegate{return Client(r.View).Snapshot().Prepared!=null&&!(bool)Field(r.View,"pending");});var p=Client(r.View).Snapshot().Prepared;Check(p.OutputApproval,"approval used measurement candidate");Check(((TextBox)Field(r.View,"preview")).Text.Contains("W/E/M unchanged"),"approval preservation diff missing");Check(!((Button)Field(r.View,"save")).IsEnabled&&!((Button)Field(r.View,"approveOutput")).IsEnabled,"unreviewed/separate approval mixed with world save");
   var exact=File.ReadAllBytes(p.CandidatePath);((CheckBox)Field(r.View,"reviewed")).IsChecked=true;Click(r.View,"approveOutput");Until(delegate{return Client(r.View).Snapshot().ActualOutcome=="SAVED";});Check(Convert.ToBase64String(exact)==Convert.ToBase64String(File.ReadAllBytes(r.Config)),"approval exact candidate mismatch");var before=CalibrationJson.Object(r.Original,2*1024*1024,32768);var after=Config(r.Config);var b=(IList)before["mappings"];var a=(IList)after["mappings"];for(int i=0;i<a.Count;++i){var m=(Dictionary<string,object>)a[i];var old=(Dictionary<string,object>)b[i];Check(CalibrationJson.Utf8.GetString(CalibrationJson.Encode((Dictionary<string,object>)m["world"],65536))==CalibrationJson.Utf8.GetString(CalibrationJson.Encode((Dictionary<string,object>)old["world"],65536)),"approval changed W");Check(Convert.ToBoolean(m["space_approved"])==(Convert.ToString(m["source_id"])=="pico"&&Convert.ToString(m["device_id"])=="device"?true:Convert.ToBoolean(old["space_approved"])),"wrong approval member");}cases++;
  }
  using(var r=new Run("b4-approval-ro",true,"b4-approval")){r.Review();foreach(CheckBox c in (IEnumerable)Field(r.View,"memberChoices"))Check(!c.IsEnabled&&c.IsChecked==false,"readonly eligibility enabled");Check(!((Button)Field(r.View,"prepareOutput")).IsEnabled,"readonly approval prepare enabled");cases++;}
  using(var r=new Run("b4-accessibility",true,"b4-unadopted")){foreach(var name in new[]{"mode","points","pairs","capturePoint","openApproval","reviewOutput","prepareOutput","approveOutput","state","evidence","preview"}){var control=(DependencyObject)Field(r.View,name);Check(!String.IsNullOrEmpty(AutomationProperties.GetName(control)),"missing automation name: "+name);if(control is Control)Check(((Control)control).IsTabStop,"not keyboard focusable: "+name);}cases++;}
  Console.WriteLine("PASS B4 unshown normal WPF "+cases+" flows plus 3 typed-review rejection cases: static exact save/readonly, separate synthetic approval and native missing-criteria refusal, explicit member selection, accessibility metadata. No Show/NotifyIcon/real SDK/socket/HIL.");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}finally{Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_ROOT",null);Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MODE",null);}}
}
