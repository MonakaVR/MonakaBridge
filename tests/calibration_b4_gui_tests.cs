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
  public void Dispose(){baseline.Dispose();}
 }
 static Dictionary<string,object> Config(string path){return CalibrationJson.Object(File.ReadAllBytes(path),2*1024*1024,32768);}
 [STAThread]static int Main(string[] args){try{
  Check(args.Length==3,"native fixture/config helper/root");
  foreach(var name in new[]{"fixture","configHelper","root"})typeof(CalibrationWpfTests).GetField(name,BindingFlags.Static|BindingFlags.NonPublic).SetValue(null,Path.GetFullPath(args[name=="fixture"?0:name=="configHelper"?1:2]));Directory.CreateDirectory(args[2]);SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
  using(var r=new Run("b4-static-save",false,"b4-static")){
   r.Static();Check(((TextBox)Field(r.View,"evidence")).Text.Contains("MEASURED (NOT SAVED)"),"measurement falsely labelled saved");Check(((TextBox)Field(r.View,"evidence")).Text.Contains("NOT ESTIMATED"),"static E/orientation explanation missing");Check(Convert.ToBase64String(r.Original)==Convert.ToBase64String(File.ReadAllBytes(r.Config)),"measurement wrote config");
   Click(r.View,"prepare");Until(delegate{return Client(r.View).Snapshot().Prepared!=null&&!(bool)Field(r.View,"pending");});Check(((TextBox)Field(r.View,"evidence")).Text.Contains("PREPARED (NOT SAVED)"),"prepared falsely labelled saved");var p=Client(r.View).Snapshot().Prepared;var exact=File.ReadAllBytes(p.CandidatePath);Check(r.View.GetType().GetField("approveOutput",BindingFlags.Instance|BindingFlags.NonPublic)==null,"ordinary view contains separate output approval");((CheckBox)Field(r.View,"reviewed")).IsChecked=true;Click(r.View,"save");Until(delegate{return Client(r.View).Snapshot().ActualOutcome=="SAVED";});Check(Convert.ToBase64String(exact)==Convert.ToBase64String(File.ReadAllBytes(r.Config)),"static exact candidate mismatch");foreach(Dictionary<string,object> member in (IList)Config(r.Config)["mappings"])if(Convert.ToString(member["source_id"])=="pico")Check(!Convert.ToBoolean(member["space_approved"]),"static save approved selected group output");cases++;
  }
  using(var r=new Run("b4-static-ro",true,"b4-static")){r.Static();Check(!((Button)Field(r.View,"prepare")).IsEnabled&&!((Button)Field(r.View,"save")).IsEnabled,"readonly static save enabled");Check(Convert.ToBase64String(r.Original)==Convert.ToBase64String(File.ReadAllBytes(r.Config)),"readonly static mutated config");cases++;}
  using(var r=new Run("b4-accessibility",true,"b4-static")){foreach(var name in new[]{"mode","points","pairs","capturePoint","state","evidence","preview"}){var control=(DependencyObject)Field(r.View,name);Check(!String.IsNullOrEmpty(AutomationProperties.GetName(control)),"missing automation name: "+name);if(control is Control)Check(((Control)control).IsTabStop,"not keyboard focusable: "+name);}Check(r.View.GetType().GetField("openApproval",BindingFlags.Instance|BindingFlags.NonPublic)==null,"separate output approval still required");cases++;}
  Console.WriteLine("PASS B4 unshown normal WPF "+cases+" flows: normal static exact save/readonly, no separate output permission controls, accessibility metadata. No Show/NotifyIcon/real SDK/socket/HIL.");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}finally{Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_ROOT",null);Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MODE",null);}}
}
