using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using MonakaBridge;
class P0GuiTests {
 static void Check(bool x,string why){if(!x)throw new Exception(why);}
 static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
 static object Field(object host,string name){return host.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(host);}
 static void Invoke(object host,string name){host.GetType().GetMethod(name,BindingFlags.Instance|BindingFlags.NonPublic,null,Type.EmptyTypes,null).Invoke(host,null);}
 static void Until(Func<bool> ready){var watch=Stopwatch.StartNew();while(!ready()){Check(watch.ElapsedMilliseconds<20000,"GUI operation deadline");Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);Thread.Sleep(5);}}
 static Dictionary<string,object> Rigid(double x){return new Dictionary<string,object>{{"rotation",new[]{0d,0d,0d,1d}},{"translation",new[]{x,0d,0d}}};}
 static byte[] Bytes(object x){return new UTF8Encoding(false).GetBytes(Json.Serialize(x));}
 static bool IsRigid(object raw,double x){var r=(Dictionary<string,object>)raw;var p=(IList)r["translation"];var q=(IList)r["rotation"];return Convert.ToDouble(p[0])==x&&Convert.ToDouble(p[1])==0&&Convert.ToDouble(p[2])==0&&Convert.ToDouble(q[0])==0&&Convert.ToDouble(q[1])==0&&Convert.ToDouble(q[2])==0&&Math.Abs(Convert.ToDouble(q[3]))==1;}
 static Dictionary<string,object> Read(string path){return Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(path));}
 static Button Action(Window host,string caption){foreach(var child in ((StackPanel)((ScrollViewer)host.Content).Content).Children){var button=child as Button;if(button!=null&&Convert.ToString(button.Content)==caption)return button;}throw new Exception("Normal action missing: "+caption);}
 [STAThread] static int Main(string[] args){try{
  string root=Path.Combine(args[1],Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
  bool integrated=args.Length>=4;
  double worldX=args.Length>=6?Double.Parse(args[5],CultureInfo.InvariantCulture):12;
  double mountX=args.Length>=7?Double.Parse(args[6],CultureInfo.InvariantCulture):3;
  string path=integrated?Path.GetFullPath(args[2]):Path.Combine(root,"config.json");
  string healthPath=integrated?Path.GetFullPath(args[3]):Path.Combine(root,"health.json");
  if(!integrated){
   var profile=new Dictionary<string,object>{{"convention","declared-native"},{"position_axes",new[]{1,2,3}},{"quaternion_axes",new[]{1,2,3,4}},{"approved",false},{"evidence",""},{"angular_space_verified",false}};
   var seed=new Dictionary<string,object>{{"namespace","monaka.bridge"},{"version",2},{"bridge_id","p0-gui"},{"mapping_revision",1},{"policy","both"},{"profiles",new Dictionary<string,object>{{"defined",profile}}},{"mappings",new object[0]}};
   var row=new Dictionary<string,object>{{"source","opaque-source"},{"device","opaque-device"},{"session_id","00000000-0000-4000-8000-000000000001"},{"clock_id","00000000-0000-4000-8000-000000000001"},{"coordinate_space",new Dictionary<string,object>{{"id","native"},{"convention","declared-native"},{"revision",1}}},{"advertised_capabilities",new[]{"position","orientation"}},{"seen_age_ms",0},{"sample_age_ms",0},{"fresh",true},{"configured",false},{"reason","UNMAPPED"}};
   var diagnostic=new Dictionary<string,object>{{"diagnostic_only",true},{"claims_are_untrusted",true},{"capacity",32},{"source_capacity",8},{"recent_ms",3000},{"truncated",false},{"recent_configured_receipts",new object[0]},{"observations",new[]{row}}};
   var health=new Dictionary<string,object>{{"publisher_id","p0-gui"},{"mapping_revision",1},{"policy","both"},{"snapshot_unix_ms",Math.Floor((DateTime.UtcNow-new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc)).TotalMilliseconds)-1},{"devices",new object[0]},{"discovery",diagnostic}};
   File.WriteAllBytes(path,Bytes(seed));File.WriteAllBytes(healthPath,Bytes(health));
  }
  var before=Read(path);var options=GuiLaunchOptions.Parse(new[]{"--config",path,"--health",healthPath,"--config-helper",Path.GetFullPath(args[0]),"--allow-write"},root);
  var host=(Window)Activator.CreateInstance(typeof(SteamVrControlWpf).GetNestedType("ControlWindow",BindingFlags.NonPublic),new object[]{options});
  var originalConfirmation=SteamVrControlWpf.DiscoveryConfirmation;bool confirmed=false;
  try{
   // Only the user confirmation presentation is simulated; this invokes the
   // actual Click -> SaveDiscovery -> RunSave -> helper validation/CAS chain.
   SteamVrControlWpf.DiscoveryConfirmation=delegate(Window owner,string review){Check(review.Contains("Declaration:")&&review.Contains("Source:"),"exact ordinary review");confirmed=true;return true;};
   Invoke(host,"Reload");Until(delegate{return (bool)Field(host,"healthAvailable");});
   var panel=(DiscoveryPanel)Field(host,"discoveryPanel");Check(panel.Candidates.Items.Count>0,"normal discovery has no candidate");
   string wantedSource=args.Length>=8?args[7]:"",wantedDevice=args.Length>=9?args[8]:"";
   int choice=-1;for(int n=0;n<panel.Candidates.Items.Count;++n){var item=(DiscoveryObservation)panel.Candidates.Items[n];if((wantedSource==""||item.Source==wantedSource)&&(wantedDevice==""||item.Device==wantedDevice)){choice=n;break;}}
   Check(choice>=0,"explicit test-operator source/device candidate not found");
   panel.Candidates.SelectedIndex=choice;var selected=(DiscoveryObservation)panel.Candidates.SelectedItem;
   Check(panel.Contracts.Items.Count>0&&panel.Profiles.Items.Count>0,"normal source bootstrap contract/profile missing");
   panel.Contracts.SelectedIndex=0;if(args.Length>=10&&args[9]!="")panel.Profiles.SelectedItem=args[9];else panel.Profiles.SelectedIndex=0;
   Check(panel.Profiles.SelectedIndex>=0,"explicit test-operator profile not found");panel.Logical.Text=args.Length>=5?args[4]:"logical";
   panel.Create.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Until(delegate{return !(bool)Field(host,"busy");});
   Check(confirmed&&!(bool)Field(host,"needsReload"),"normal Create handler did not save");
   var saved=Read(path);Dictionary<string,object> mapping=null;int selectedIndex=-1,index=0;
   foreach(Dictionary<string,object> row in DiscoveryModel.Mappings(saved)){if((string)row["source_id"]==selected.Source&&(string)row["device_id"]==selected.Device){mapping=row;selectedIndex=index;}index++;}
   Check(mapping!=null&&mapping.ContainsKey("runtime_contract"),"normal helper save omitted exact binding/contract");
   Check(IsRigid(mapping["mount"],0),"new physical Mount must be Identity");
   if(!integrated){
    // Existing uncalibrated documents can omit optional geometry and quality
    // fields. Exercise real reload/select/Save mapping, with byte-preserving CAS.
    foreach(Dictionary<string,object> item in DiscoveryModel.Mappings(saved)){item.Remove("world");item.Remove("mount");item.Remove("space_approved");}
    foreach(var item in (Dictionary<string,object>)saved["profiles"]){var definition=(Dictionary<string,object>)item.Value;definition.Remove("approved");definition.Remove("evidence");}
    saved["unknown_extension"]=new Dictionary<string,object>{{"keep",new[]{1,2,3}}};
    byte[] omitted=Bytes(saved);File.WriteAllBytes(path,omitted);Invoke(host,"Reload");((ListBox)Field(host,"mappings")).SelectedIndex=selectedIndex;
    Action(host,"Save mapping").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Until(delegate{return !(bool)Field(host,"busy");});
    Check(!(bool)Field(host,"needsReload")&&Convert.ToBase64String(File.ReadAllBytes(path))==Convert.ToBase64String(omitted),"optional defaults GUI save lost CAS/no-op/unknown fields");
    saved=MappingEditor.WithDefaults(Read(path));mapping=(Dictionary<string,object>)DiscoveryModel.Mappings(saved)[selectedIndex];
   }
   // Explicit nonidentity Mount through the supported native config transaction.
   // There is no Mount editor in the current WPF product.
   mapping["mount"]=Rigid(mountX);saved["mapping_revision"]=Convert.ToUInt32(saved["mapping_revision"])+1;
   string candidate=Path.Combine(root,"mount-candidate.json");File.WriteAllBytes(candidate,Bytes(saved));
   new ConfigHelperClient(options).Commit(File.ReadAllBytes(path),candidate,CancellationToken.None);
   Invoke(host,"Reload");((ListBox)Field(host,"mappings")).SelectedIndex=selectedIndex;
   ((TextBox)Field(host,"x")).Text=worldX.ToString(CultureInfo.InvariantCulture);((TextBox)Field(host,"y")).Text="0";((TextBox)Field(host,"z")).Text="0";
   Action(host,"Apply shared translation").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Until(delegate{return !(bool)Field(host,"busy");});
   Check(!(bool)Field(host,"needsReload"),"normal World save failed");
   byte[] geometric=File.ReadAllBytes(path);var applied=Read(path);var kept=(Dictionary<string,object>)DiscoveryModel.Mappings(applied)[selectedIndex];
   Check(IsRigid(kept["world"],worldX)&&IsRigid(kept["mount"],mountX),"ordinary GUI World save did not preserve Mount");
   // Same normal handler again must preserve exact bytes/revision.
   Action(host,"Apply shared translation").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Until(delegate{return !(bool)Field(host,"busy");});
   Check(Convert.ToBase64String(geometric)==Convert.ToBase64String(File.ReadAllBytes(path)),"same-W GUI save changed bytes/revision");
   var receipt=new Dictionary<string,object>{{"config_path",path},{"health_path",healthPath},{"config_sha256",CalibrationInstallation.Sha256(geometric)},{"source",selected.Source},{"device",selected.Device},{"tracker",kept["tracker_id"]},{"world",kept["world"]},{"mount",kept["mount"]},{"actual_gui_handlers",new[]{"Discovery Create.Click/SaveDiscovery/RunSave","Apply shared translation.Click/SaveNativeDraft/RunSave"}},{"confirmation","simulated presentation callback"},{"input_boundary",integrated?"external actual Bridge health":"synthetic public Health fixture"},{"physical_hil","NOT_RUN"}};
   File.WriteAllBytes(Path.Combine(args[1],"latest-run.json"),Bytes(receipt));
   Console.WriteLine("PASS actual ordinary WPF handlers -> native helper/CAS; first-source static contract; explicit native Mount; GUI World save and same-W no-op. SAVED_CONFIG="+path);return 0;
  }finally{SteamVrControlWpf.DiscoveryConfirmation=originalConfirmation;host.Close();}
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
