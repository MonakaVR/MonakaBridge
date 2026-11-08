using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Threading;
using MonakaBridge;
class CalibrationB4GuiTests {
 static int cases;
 static void Check(bool okay,string reason){if(!okay)throw new Exception(reason);}
 static object Field(object value,string name){return value.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(value);}
 static void Until(Func<bool> ready){var watch=Stopwatch.StartNew();while(!ready()){Check(watch.ElapsedMilliseconds<22000,"B4 unshown control deadline");Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);Thread.Sleep(5);}Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);}
 static IEnumerable<DependencyObject> Controls(DependencyObject root){yield return root;var panel=root as Panel;if(panel!=null)foreach(DependencyObject item in panel.Children)foreach(var child in Controls(item))yield return child;var content=root as ContentControl;if(content!=null&&content.Content is DependencyObject)foreach(var child in Controls((DependencyObject)content.Content))yield return child;}
 sealed class Run:IDisposable {
  readonly IDisposable baseline;internal CalibrationPanel View;internal Window Host;internal string Config;internal byte[] Original;
  internal Run(string label,bool ro,string mode){var type=typeof(CalibrationWpfTests).GetNestedType("Run",BindingFlags.NonPublic);baseline=(IDisposable)Activator.CreateInstance(type,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{label,ro,mode},null);View=(CalibrationPanel)Field(baseline,"View");Host=(Window)Field(baseline,"Host");Config=(string)Field(baseline,"Config");Original=(byte[])Field(baseline,"Original");}
  internal void Save(){var start=(Button)Field(View,"start");Check(start.IsEnabled,"normal Start unavailable");start.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Until(delegate{return !View.Active;});Check(View.Snapshot().ActualOutcome=="SAVED","normal auto-save failed: "+View.Snapshot().Fault);}
  public void Dispose(){baseline.Dispose();}
 }
 static Dictionary<string,object> Config(string path){return CalibrationJson.Object(File.ReadAllBytes(path),2*1024*1024,32768);}
 [STAThread]static int Main(string[] args){try{
  Check(args.Length==3,"native fixture/config helper/root");
  foreach(var name in new[]{"fixture","configHelper","root"})typeof(CalibrationWpfTests).GetField(name,BindingFlags.Static|BindingFlags.NonPublic).SetValue(null,Path.GetFullPath(args[name=="fixture"?0:name=="configHelper"?1:2]));Directory.CreateDirectory(args[2]);SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
  using(var r=new Run("b4-primary",false,"")){
   int selectors=0,buttons=0;foreach(var element in Controls(r.View)){if(element is ComboBox)selectors++;if(element is Button)buttons++;Check(!(element is TextBox)&&!(element is CheckBox),"manual input/review remains in primary calibration");}
   Check(selectors==3&&buttons==2,"primary flow is not exactly three selectors + Start/Cancel");
   var reference=(ComboBox)Field(r.View,"reference");Check(reference.Items.Count==3&&Convert.ToString(reference.Items[0])=="Left controller"&&Convert.ToString(reference.Items[1])=="Right controller"&&Convert.ToString(reference.Items[2])=="HMD","reference choices incomplete");
   var method=(ComboBox)Field(r.View,"method");Check(method.Items.Count==1&&Convert.ToString(method.Items[0])=="Figure-eight","unsupported method exposed");
   foreach(var name in new[]{"open","discover","confirm","begin","capturePoint","prepare","save","fresh","reviewed","points","pairs","openApproval","approveOutput"})Check(r.View.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic)==null,"obsolete manual calibration stage exposed: "+name);
   var primary=(StackPanel)((ScrollViewer)r.Host.Content).Content;int calibrationIndex=primary.Children.IndexOf(r.View);Expander advanced=null;int advancedIndex=-1;for(int i=0;i<primary.Children.Count;i++)if(primary.Children[i] is Expander){advanced=(Expander)primary.Children[i];advancedIndex=i;}
   Check(calibrationIndex>=0&&calibrationIndex<advancedIndex&&advanced!=null&&!advanced.IsExpanded,"normal calibration not above collapsed Advanced settings");
   foreach(var name in new[]{"reference","tracker","method","start","cancel","state","progress"}){var control=(DependencyObject)Field(r.View,name);Check(!String.IsNullOrEmpty(AutomationProperties.GetName(control)),"missing automation name: "+name);if(control is ComboBox||control is Button)Check(((Control)control).IsTabStop,"selector/action not keyboard focusable: "+name);}cases++;
  }
  using(var r=new Run("b4-hmd-auto-save",false,"")){
   ((ComboBox)Field(r.View,"reference")).SelectedIndex=2;r.Save();var s=r.View.Snapshot();Check(s.Stopped&&((CalibrationWorkflow)Field(r.View,"workflow")).CandidateSha256==CalibrationInstallation.Sha256(File.ReadAllBytes(r.Config)),"HMD exact candidate not saved and closed");
   Check(((TextBlock)Field(r.View,"state")).Text.Contains("Done")&&((TextBlock)Field(r.View,"state")).Text.Contains("Runtime application is unconfirmed"),"done state overclaims runtime application or was cleared");
   foreach(Dictionary<string,object> member in (IList)Config(r.Config)["mappings"])if(Convert.ToString(member["source_id"])=="pico")Check(!Convert.ToBoolean(member["space_approved"]),"normal calibration enabled output approval");cases++;
  }
  BridgeText.Language="ja";
  using(var r=new Run("b4-readonly-japanese",true,"")){
   Check(Convert.ToString(((Button)Field(r.View,"start")).Content)=="開始"&&Convert.ToString(((Button)Field(r.View,"cancel")).Content)=="キャンセル","Japanese actions not localized");
   Check(Convert.ToString(((ComboBox)Field(r.View,"reference")).Items[0])=="左コントローラー"&&Convert.ToString(((ComboBox)Field(r.View,"reference")).Items[1])=="右コントローラー"&&Convert.ToString(((ComboBox)Field(r.View,"method")).Items[0])=="8の字","Japanese selectors not localized");
   Check(((TextBlock)Field(r.View,"instructions")).Text.Contains("読み取り専用")&&((TextBlock)Field(r.View,"instructions")).Text.Contains("8の字"),"Japanese readonly instructions missing");
   var start=(Button)Field(r.View,"start");Check(start.IsEnabled,"readonly measurement disabled");start.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Check(((TextBlock)Field(r.View,"state")).Text.Contains("開始しています"),"Japanese starting progress missing");Until(delegate{return !r.View.Active;});
   Check(r.View.Snapshot().ActualOutcome=="MEASURED_READ_ONLY"&&r.View.Snapshot().Prepared==null&&!r.View.Snapshot().CommitMayHaveStarted&&r.View.Snapshot().Result!=null,"readonly measurement/save separation failed");
   Check(((TextBlock)Field(r.View,"state")).Text.StartsWith("測定完了（読み取り専用・未保存）",StringComparison.Ordinal),"Japanese readonly result missing or falsely saved: "+((TextBlock)Field(r.View,"state")).Text+" / phase="+r.View.Snapshot().Phase+" / fault="+r.View.Snapshot().Fault);Check(Convert.ToBase64String(r.Original)==Convert.ToBase64String(File.ReadAllBytes(r.Config)),"readonly mutated config");
   foreach(var key in new[]{"Calibration","Reference","Tracker","Method","Advanced settings","Calibration cancelled.","Calibration did not finish. ","Done — calibration saved. Runtime application is unconfirmed.","Move the reference and tracker rigidly together in a figure-eight…","Save outcome unknown. Reload configuration before starting again."})Check(BridgeText.T(key)!=key,"missing Japanese normal text: "+key);cases++;
  }
  BridgeText.Language="en";
  using(var r=new Run("b4-empty-mappings",false,"")){
   var config=Config(r.Config);config["mappings"]=new ArrayList();r.View.SetConfiguration(config);Check(((ComboBox)Field(r.View,"tracker")).Items.Count==0&&!((Button)Field(r.View,"start")).IsEnabled,"empty config starts arbitrary tracker calibration");Check(((TextBlock)Field(r.View,"state")).Text.Contains("persisted tracker"),"empty mapping guidance absent");cases++;
  }
  Console.WriteLine("PASS B4 unshown normal WPF "+cases+" flows: exactly three selectors + Start/Cancel, collapsed advanced, accessibility, HMD automatic save without output approval, readonly/empty selection safeguards. No Show/real SDK/socket/HIL.");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}finally{BridgeText.Language="en";Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_ROOT",null);Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MODE",null);Environment.SetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MIRROR_PORT",null);}}
}
