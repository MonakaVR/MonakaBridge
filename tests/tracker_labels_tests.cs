using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using MonakaBridge;

// Normal unshown WPF objects, draft models and test-config native CAS; no SDK.
class TrackerLabelsTests {
 static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
 static void Check(bool value,string why){if(!value)throw new Exception(why);}
 static object Field(object host,string name){return host.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic|BindingFlags.Public).GetValue(host);}
 static void Call(object host,string name){host.GetType().GetMethod(name,BindingFlags.Instance|BindingFlags.NonPublic,null,Type.EmptyTypes,null).Invoke(host,null);}
 static Button FindAction(DependencyObject parent,string caption){var button=parent as Button;if(button!=null&&Convert.ToString(button.Content)==caption)return button;foreach(object raw in LogicalTreeHelper.GetChildren(parent)){var child=raw as DependencyObject;if(child!=null){var result=FindAction(child,caption);if(result!=null)return result;}}return null;}
 static void Click(Window host,string caption){var action=FindAction(host,caption);Check(action!=null&&action.IsEnabled,"normal action unavailable: "+caption);action.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));}
 static void Idle(Window host){var watch=Stopwatch.StartNew();while((bool)Field(host,"busy")){Check(watch.ElapsedMilliseconds<20000,"native save timeout");Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);Thread.Sleep(5);}Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);Check(!(bool)Field(host,"needsReload"),"normal save failed: "+((TextBlock)Field(host,"status")).Text);}
 static string J(object value){return Json.Serialize(value);}
 static void Reject(Action action){try{action();}catch(InvalidOperationException){return;}throw new Exception("Expected existing validation refusal");}
 static Dictionary<string,object> Rigid(){return new Dictionary<string,object>{{"rotation",new[]{0,0,0,1}},{"translation",new[]{0,0,0}}};}
 static Dictionary<string,object> Binding(string source,string device,string tracker){return new Dictionary<string,object>{{"source_id",source},{"device_id",device},{"tracker_id",tracker},{"profile","defined"},{"input_space","native"},{"input_revision",1},{"world_space","world"},{"world_revision",1},{"world",Rigid()},{"mount",Rigid()},{"space_approved",false}};}
 static DiscoveryObservation Observation(string source,string device){return new DiscoveryObservation{Source=source,Device=device,Session="session",Clock="clock",Space="native",Convention="declared-native",Revision=1,Claims=new[]{"position","orientation"},Fresh=true,SeenAge=0,Reason="UNMAPPED"};}
 static DiscoveryDraft Draft(Dictionary<string,object> config,DiscoveryObservation row,DiscoveryAction action,string typed){var snapshot=new DiscoverySnapshot();snapshot.Rows.Add(row);return DiscoveryModel.Prepare(config,snapshot,row,action,action==DiscoveryAction.Create?-1:0,typed,"defined",DiscoveryModel.DeclaredContract(row),false,false);}
 [STAThread]static int Main(string[] args){try{
  string directory=Path.Combine(Path.GetFullPath(args[1]),"labels-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(directory);
  var profile=new Dictionary<string,object>{{"convention","declared-native"},{"position_axes",new[]{1,2,3}},{"quaternion_axes",new[]{1,2,3,4}}};
  var config=new Dictionary<string,object>{{"namespace","monaka.bridge"},{"version",2},{"bridge_id","labels"},{"mapping_revision",3},{"policy","disabled"},{"profiles",new Dictionary<string,object>{{"defined",profile}}},{"mappings",new[]{Binding("source-a","physical-device","old-random-id"),Binding("source-b","physical-device","other-random-id")}}};
  string original=J(config);var rows=(IList)config["mappings"];
  Check(TrackerPresentation.MappingLabel((Dictionary<string,object>)rows[0],rows)=="physical-device [1]"&&TrackerPresentation.MappingLabel((Dictionary<string,object>)rows[1],rows)=="physical-device [2]","duplicate physical names need concise distinct labels");
  Check(TrackerPresentation.MappingLabel((Dictionary<string,object>)rows[0],new ArrayList{rows[0]})=="physical-device","unique label exposed internal IDs/marker");
  Check(TrackerPresentation.Label("source-b","physical-device",new[]{Tuple.Create("source-b","physical-device"),Tuple.Create("source-a","physical-device")})=="physical-device [2]","duplicate marker changed with order");
  Check(Draft(config,Observation("source-a","new-physical"),DiscoveryAction.Create,"").Tracker=="new-physical","blank Create default changed device ID");
  Check(Draft(config,Observation("source-a","new-physical"),DiscoveryAction.Create,"explicit-user-id").Tracker=="explicit-user-id","explicit override lost");
  Check(Observation("source-a","new-physical").ToString().StartsWith("new-physical |",StringComparison.Ordinal),"discovery physical device is not primary");
  Check(Draft(config,Observation("source-a","replacement"),DiscoveryAction.Replace,"").Tracker=="old-random-id","rebind renamed existing output identity");
  var known=Observation("source-a","physical-device");known.Configured=true;known.Revision=2;
  Check(Draft(config,known,DiscoveryAction.UpdateRevision,"ignored-new-id").Tracker=="old-random-id","revision update renamed existing ID");
  Reject(delegate{Draft(config,Observation("source-a",new string('x',129)),DiscoveryAction.Create,"");});
  Reject(delegate{Draft(config,Observation("source-a","old-random-id"),DiscoveryAction.Create,"");});
  Check(Draft(config,Observation("source-c","old-random-id"),DiscoveryAction.Create,"").Tracker=="old-random-id","foreign opaque source incorrectly collided");
  Check(J(config)==original,"presentation/default/draft mutated loaded configuration");
  string path=Path.Combine(directory,"config.json");File.WriteAllText(path,original,new UTF8Encoding(false));
  var options=GuiLaunchOptions.Parse(new[]{"--config",path,"--health",Path.Combine(directory,"health.json"),"--config-helper",Path.GetFullPath(args[0]),"--allow-write"},directory);
  var host=(Window)Activator.CreateInstance(typeof(SteamVrControlWpf).GetNestedType("ControlWindow",BindingFlags.NonPublic),new object[]{options});
  try{
   Call(host,"Reload");var mappings=(ListBox)Field(host,"mappings");var calibration=(CalibrationPanel)Field(host,"calibration");var choices=(ComboBox)Field(calibration,"tracker");
   Check(Convert.ToString(((ListBoxItem)mappings.Items[0]).Content)=="physical-device [1]"&&Convert.ToString(((ListBoxItem)mappings.Items[0]).ToolTip).Contains("Logical tracker ID: old-random-id"),"normal mapping label/details missing");
   choices.SelectedIndex=1;Check(choices.SelectedItem.ToString()=="physical-device [2]"&&Convert.ToString(choices.ToolTip).Contains("Source ID: source-b"),"duplicate calibration source selection lost");
   calibration.SetConfiguration(MappingEditor.WithDefaults(config));Check((string)Field(choices.SelectedItem,"Source")=="source-b","reload restored by display instead of source/device key");
   mappings.SelectedIndex=1;Check(((TextBox)Field(host,"source")).Text=="source-b"&&((TextBox)Field(host,"tracker")).Text=="other-random-id","normal mapping selection changed ID");
   Click(host,"New mapping / clear selection");
   var tracker=(TextBox)Field(host,"tracker");var device=(TextBox)Field(host,"device");var source=(TextBox)Field(host,"source");
   tracker.Text="";source.Text="source-a";device.Text="fresh-device";Call(host,"SuggestTrackerId");Check(tracker.Text=="fresh-device","normal New blank ID did not default");
   tracker.Text="user-choice";device.Text="second-device";Call(host,"SuggestTrackerId");Check(tracker.Text=="user-choice","normal New override lost");
   tracker.Text="";device.Text="physical-device";Call(host,"SuggestTrackerId");Check(tracker.Text=="old-random-id","existing physical mapping implicitly renamed");
   Check(File.ReadAllText(path)==original,"presentation or default wrote config bytes");
   Click(host,"New mapping / clear selection");source.Text="source-a";device.Text="fresh-device";((TextBox)Field(host,"space")).Text="native";((TextBox)Field(host,"revision")).Text="1";((TextBox)Field(host,"world")).Text="world";((ComboBox)Field(host,"profile")).SelectedItem="defined";
   Click(host,"Save mapping");Idle(host);
   var saved=Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(path));var savedRows=DiscoveryModel.Mappings(saved);Check(savedRows.Count==3&&(string)((Dictionary<string,object>)savedRows[2])["tracker_id"]=="fresh-device","actual New/Save mapping did not persist device-ID default");
   for(int i=0;i<2;i++)Check(J(savedRows[i])==J(DiscoveryModel.Mappings(MappingEditor.WithDefaults(config))[i]),"new mapping changed existing IDs/geometry");
   byte[] committed=File.ReadAllBytes(path);Click(host,"Save mapping");Idle(host);Check(Convert.ToBase64String(committed)==Convert.ToBase64String(File.ReadAllBytes(path)),"normal no-op mapping save changed bytes/revision");
   device.Text="rebound-device";Click(host,"Save mapping");Idle(host);saved=Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(path));Check((string)((Dictionary<string,object>)DiscoveryModel.Mappings(saved)[2])["tracker_id"]=="fresh-device","normal selected mapping rebind renamed logical output ID");
   Click(host,"New mapping / clear selection");source.Text="source-c";device.Text="last-device";tracker.Text="explicit-override";((TextBox)Field(host,"space")).Text="native";((TextBox)Field(host,"revision")).Text="1";((TextBox)Field(host,"world")).Text="world";((ComboBox)Field(host,"profile")).SelectedItem="defined";
   Click(host,"Save mapping");Idle(host);saved=Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(path));Check((string)((Dictionary<string,object>)DiscoveryModel.Mappings(saved)[3])["tracker_id"]=="explicit-override","actual New/Save mapping lost explicit override");
  }finally{host.Close();}
  Console.WriteLine("PASS physical labels, duplicate-key selection, unchanged IDs, blank/explicit defaults, length/collision refusal, rebind/revision retention, no presentation writes");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
