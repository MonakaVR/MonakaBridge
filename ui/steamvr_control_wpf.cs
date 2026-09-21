using System;
using System.Collections.Generic;
using System.Collections;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
namespace MonakaBridge {
 public static class SteamVrControlWpf {
  public static void Run(string root){new Application().Run(new ControlWindow(root));}
  internal static string ConfigPath(string root){return Path.Combine(root,"config","bridge.json");}
  internal static string Native(string root,string name){return Path.Combine(root,"build","Release",name+".exe");}
  internal static string Quote(string s){if(s.Contains("\""))throw new ArgumentException("quote in path");return "\""+s+"\"";}
  internal static void Command(string root,string args){
   var start=new ProcessStartInfo(Native(root,"monaka_bridge_config"),Quote(ConfigPath(root))+" "+args){UseShellExecute=false,CreateNoWindow=true,RedirectStandardError=true};
   using(var p=Process.Start(start)){string error=p.StandardError.ReadToEnd();p.WaitForExit();if(p.ExitCode!=0)throw new InvalidOperationException(error);}
  }
  internal static Dictionary<string,object> Read(string path){return new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(path,Encoding.UTF8));}
  private sealed class ControlWindow:Window {
   readonly string root;readonly TextBlock status=new TextBlock{TextWrapping=TextWrapping.Wrap};
   readonly TextBlock livePose=new TextBlock{TextWrapping=TextWrapping.Wrap,Margin=new Thickness(2,4,2,8)};
   readonly TextBlock healthStatus=new TextBlock{TextWrapping=TextWrapping.Wrap};readonly HealthReader healthReader=new HealthReader();
   DateTime healthSnapshotUtc=DateTime.MinValue;bool healthAvailable;string currentPolicy="unknown",healthPublisher;object healthRevision;double poseTimeoutMs=500;
   readonly TextBlock mappingStatus=new TextBlock{TextWrapping=TextWrapping.Wrap};readonly MappingEditorState editor=new MappingEditorState();
   readonly ListBox devices=new ListBox{Height=160};readonly ListBox mappings=new ListBox{Height=130};
   readonly TextBox source=new TextBox(),device=new TextBox(),tracker=new TextBox(),space=new TextBox(),revision=new TextBox(),world=new TextBox();
   readonly ComboBox profile=new ComboBox{IsEditable=false};
   readonly TextBox x=new TextBox{Text="0"},y=new TextBox{Text="0"},z=new TextBox{Text="0"};readonly CheckBox approved=new CheckBox{Content="This input space/revision has been approved"};
   readonly ComboBox routes=new ComboBox{ItemsSource=new[]{"steamvr","monaka","both","disabled"}};
   Dictionary<string,object> config;ArrayList map;ArrayList observations=new ArrayList();readonly DispatcherTimer timer=new DispatcherTimer();
   public ControlWindow(string root){this.root=root;Title="Monaka Bridge";Width=900;Height=820;
    var panel=new StackPanel{Margin=new Thickness(16)};Content=new ScrollViewer{Content=panel};
    panel.Children.Add(new TextBlock{Text="Sources and devices",FontSize=20});panel.Children.Add(healthStatus);panel.Children.Add(devices);panel.Children.Add(livePose);
    devices.SelectionChanged+=delegate{UpdateLivePose();};
    Add(panel,"Refresh / load configuration",Reload);Add(panel,"Use selected observation",SelectObservation);
    panel.Children.Add(new TextBlock{Text="Persistent tracker mapping and profile",FontSize=18});panel.Children.Add(mappingStatus);panel.Children.Add(mappings);mappings.SelectionChanged+=delegate{if(!editor.Updating)SelectMapping();};
    Add(panel,"New mapping / clear selection",delegate{editor.Select(map,-1,RenderMapping);status.Text="No mapping selected; select a profile explicitly. Save updates the exact source/device or creates a new mapping.";});
    Field(panel,"Source",source);Field(panel,"Device",device);Field(panel,"Logical tracker",tracker);Field(panel,"Profile",profile);Field(panel,"Input map",space);Field(panel,"Input map revision",revision);Field(panel,"World map",world);panel.Children.Add(approved);
    foreach(var identityField in new[]{source,device,space,revision})identityField.TextChanged+=delegate{if(editor.IdentityEdited())approved.IsChecked=false;};
    approved.Checked+=delegate{if(!editor.Updating)editor.Approved=true;};approved.Unchecked+=delegate{if(!editor.Updating)editor.Approved=false;};
    Add(panel,"Save mapping",SaveMapping);Add(panel,"Show profile approval",delegate{var p=MappingEditor.RequireProfile(config,profile.SelectedItem as string);status.Text="Profile approved: "+p["approved"]+"\nEvidence: "+p["evidence"]+"\nInput space approval is separate.";});
    Add(panel,"Import legacy route / alignment",Migrate);
    panel.Children.Add(new TextBlock{Text="Shared alignment for the selected source map (metres)",FontSize=18});Field(panel,"X",x);Field(panel,"Y",y);Field(panel,"Z",z);
    Add(panel,"Apply shared translation",delegate{double.Parse(x.Text,CultureInfo.InvariantCulture);double.Parse(y.Text,CultureInfo.InvariantCulture);double.Parse(z.Text,CultureInfo.InvariantCulture);Command(root,"align "+Quote(tracker.Text)+" "+x.Text+" "+y.Text+" "+z.Text);Reload();});
    panel.Children.Add(routes);Add(panel,"Apply output policy",delegate{if(routes.SelectedItem==null)throw new InvalidOperationException("Select an output policy.");Command(root,"policy "+routes.SelectedItem);Reload();});
    Add(panel,"Start Bridge",delegate{Process.Start(new ProcessStartInfo(Native(root,"monaka_bridge_service"),Quote(ConfigPath(root))){UseShellExecute=false,CreateNoWindow=true});});
    Add(panel,"Stop Bridge",delegate{Process.Start(new ProcessStartInfo(Native(root,"monaka_bridge_service"),"--stop"){UseShellExecute=false,CreateNoWindow=true});});
    panel.Children.Add(status);timer.Interval=TimeSpan.FromSeconds(1);timer.Tick+=delegate{Health();};timer.Start();Closed+=delegate{timer.Stop();healthReader.Dispose();};Loaded+=delegate{Safe(Reload);};
   }
   // WPF button/error pattern migrated from Task2 SteamVrControlWpf.
   static Button MakeButton(string text,double width){return new Button{Content=text,Width=width,MinHeight=28,Margin=new Thickness(3)};}
   void Add(Panel p,string text,Action action){var b=MakeButton(text,260);b.Click+=delegate{Safe(action);};p.Children.Add(b);}
   void Safe(Action a){try{a();}catch(Exception e){status.Text=e.Message;MessageBox.Show(this,e.Message,"Monaka Bridge",MessageBoxButton.OK,MessageBoxImage.Error);}}
   static void Field(Panel p,string label,Control field){var row=new DockPanel();var text=new TextBlock{Text=label,Width=140};row.Children.Add(text);field.Margin=new Thickness(2);row.Children.Add(field);p.Children.Add(row);}
   void Reload(){Reload(editor.SelectedKey);}
   void Reload(Tuple<string,string> restoreKey){
    var persisted=Read(ConfigPath(root));var nextMap=new ArrayList((ICollection)persisted["mappings"]);
    config=persisted;map=nextMap;
    status.Text="Loaded revision "+config["mapping_revision"]+"; profile approval is required independently of space approval.";
    editor.Programmatic(delegate{
     mappings.Items.Clear();foreach(Dictionary<string,object> b in map)mappings.Items.Add(b["tracker_id"]+" | "+b["source_id"]+" / "+b["device_id"]);
     var names=new List<string>(((Dictionary<string,object>)config["profiles"]).Keys);names.Sort(StringComparer.Ordinal);profile.ItemsSource=names;
     routes.SelectedItem=Convert.ToString(config["policy"],CultureInfo.InvariantCulture);
     editor.Restore(map,restoreKey,RenderMapping);
    });
    Health();
   }
   void Health(){
    UpdateLivePose();
    healthReader.Request(ConfigPath(root)+".status.json",delegate(Action apply){Dispatcher.BeginInvoke(apply);},ApplyHealth);
   }
   void ApplyHealth(HealthReadResult result){try{
    if(result.Error!=null){healthAvailable=false;healthStatus.Text="Bridge health unavailable; previous values retained. "+result.Error;UpdateLivePose();return;}
    string selectedSource=null,selectedDevice=null,selectedPublisher=null;
    if(devices.SelectedIndex>=0&&devices.SelectedIndex<observations.Count){var selected=(Dictionary<string,object>)observations[devices.SelectedIndex];selectedSource=(string)selected["source"];selectedDevice=(string)selected["device"];selectedPublisher=selected.ContainsKey("publisher_id")?(string)selected["publisher_id"]:null;}
    var h=result.Value;var next=new ArrayList((ICollection)h["devices"]);devices.Items.Clear();observations=next;int restore=-1;int index=0;
    healthSnapshotUtc=result.SnapshotUtc;healthAvailable=true;currentPolicy=Convert.ToString(h["policy"],CultureInfo.InvariantCulture);
    healthRevision=h.ContainsKey("mapping_revision")?h["mapping_revision"]:null;healthPublisher=h.ContainsKey("publisher_id")?Convert.ToString(h["publisher_id"],CultureInfo.InvariantCulture):null;
    poseTimeoutMs=h.ContainsKey("pose_timeout_ms")?Convert.ToDouble(h["pose_timeout_ms"],CultureInfo.InvariantCulture):500;
    foreach(Dictionary<string,object> d in observations){
     var tracking=d.ContainsKey("tracking_state")?Convert.ToString(d["tracking_state"],CultureInfo.InvariantCulture):"unknown";
     var pos=d.ContainsKey("position_valid")&&Convert.ToBoolean(d["position_valid"],CultureInfo.InvariantCulture);
     var rot=d.ContainsKey("orientation_valid")&&Convert.ToBoolean(d["orientation_valid"],CultureInfo.InvariantCulture);
     devices.Items.Add(d["source"]+" / "+d["device"]+" | "+d["tracker"]+" | tracking="+tracking+" | pos="+pos+" rot="+rot+" | snapshot fresh="+d["fresh"]);
     var publisher=d.ContainsKey("publisher_id")?(string)d["publisher_id"]:null;
     if(selectedPublisher==publisher&&selectedSource==(string)d["source"]&&selectedDevice==(string)d["device"])restore=index;++index;
    }
    if(restore>=0)devices.SelectedIndex=restore;
    UpdateLivePose();
   }catch(Exception e){healthAvailable=false;healthStatus.Text="Bridge health unavailable: "+e.Message;UpdateLivePose();}}
   void UpdateLivePose(){
    bool current=healthAvailable&&HealthPresentation.Current(healthSnapshotUtc,DateTime.UtcNow);
    if(config!=null)mappingStatus.Text=HealthPresentation.MappingRevision(config["mapping_revision"],healthRevision,current,healthPublisher==Convert.ToString(config["bridge_id"],CultureInfo.InvariantCulture));
    if(healthAvailable)healthStatus.Text="Current policy (last snapshot): "+currentPolicy+" | health="+(current?"current":"unavailable / historical");
    if(devices.SelectedIndex<0||devices.SelectedIndex>=observations.Count){livePose.Text="Select a source/device to inspect live pose.";return;}
    var d=(Dictionary<string,object>)observations[devices.SelectedIndex];
    livePose.Text=HealthPresentation.Pose(d,current,(DateTime.UtcNow-healthSnapshotUtc).TotalMilliseconds,poseTimeoutMs);
   }
   void Migrate(){
    var route=new Microsoft.Win32.OpenFileDialog{Title="Select the legacy output route file"};if(route.ShowDialog(this)!=true)return;
    var alignment=new Microsoft.Win32.OpenFileDialog{Title="Select the legacy alignment JSON"};if(alignment.ShowDialog(this)!=true)return;
    string target=ConfigPath(root),candidate=target+".migration-"+Guid.NewGuid().ToString("N");
    Command(root,"migrate "+Quote(route.FileName)+" "+Quote(alignment.FileName)+" "+Quote(candidate));
    File.Replace(candidate,target,target+".backup-"+Guid.NewGuid().ToString("N"));Reload();status.Text="Legacy settings imported with backups; review each space before enabling output.";
   }
   void SelectObservation(){if(devices.SelectedIndex<0)return;var d=(Dictionary<string,object>)observations[devices.SelectedIndex];source.Text=(string)d["source"];device.Text=(string)d["device"];space.Text=(string)d["space"];revision.Text=Convert.ToString(d["revision"],CultureInfo.InvariantCulture);editor.Approved=false;approved.IsChecked=false;status.Text=(editor.SelectedIndex>=0?"Selected mapping will be rebound to this observation on Save mapping. ":"")+"Observed convention: "+d["convention"]+". Select a verified profile before approving output.";}
   void SelectMapping(){editor.Select(map,mappings.SelectedIndex,RenderMapping);}
   void RenderMapping(Dictionary<string,object> b){
    mappings.SelectedIndex=editor.SelectedIndex;
    if(b==null){foreach(var field in new[]{source,device,tracker,space,revision,world})field.Text="";profile.SelectedIndex=-1;approved.IsChecked=false;x.Text=y.Text=z.Text="0";return;}
    source.Text=(string)b["source_id"];device.Text=(string)b["device_id"];tracker.Text=(string)b["tracker_id"];space.Text=(string)b["input_space"];revision.Text=Convert.ToString(b["input_revision"],CultureInfo.InvariantCulture);world.Text=(string)b["world_space"];
    profile.SelectedIndex=-1;if(profile.Items.Contains((string)b["profile"]))profile.SelectedItem=(string)b["profile"];
    if(profile.SelectedItem==null)status.Text="Unknown profile: "+b["profile"]+". Select an existing profile before saving or viewing approval.";
    approved.IsChecked=editor.Approved;var t=(IList)((Dictionary<string,object>)b["world"])["translation"];x.Text=Convert.ToString(t[0],CultureInfo.InvariantCulture);y.Text=Convert.ToString(t[1],CultureInfo.InvariantCulture);z.Text=Convert.ToString(t[2],CultureInfo.InvariantCulture);
   }
   void SaveMapping(){
    var fresh=Read(ConfigPath(root));if(Convert.ToUInt32(fresh["mapping_revision"])!=Convert.ToUInt32(config["mapping_revision"]))throw new InvalidOperationException("Configuration changed; reload first.");
    MappingEditor.RequireProfile(config,profile.SelectedItem as string);
    var edit=new Dictionary<string,object>{{"source_id",source.Text},{"device_id",device.Text},{"tracker_id",tracker.Text},{"profile",profile.SelectedItem as string},{"input_space",space.Text},{"input_revision",UInt32.Parse(revision.Text)},{"world_space",world.Text},{"space_approved",approved.IsChecked==true}};
    var updated=MappingEditor.Candidate(config,editor.SelectedIndex,edit);
    string target=ConfigPath(root),candidate=target+".candidate-"+Guid.NewGuid().ToString("N");File.WriteAllText(candidate,new JavaScriptSerializer().Serialize(updated),new UTF8Encoding(false));
    using(var p=Process.Start(new ProcessStartInfo(Native(root,"monaka_bridge_config"),Quote(candidate)+" validate"){UseShellExecute=false,CreateNoWindow=true})){p.WaitForExit();if(p.ExitCode!=0)throw new InvalidOperationException("Invalid mapping; candidate retained for review.");}
    File.Replace(candidate,target,target+".backup-"+Guid.NewGuid().ToString("N"));Reload(MappingEditorState.Key(edit));
    status.Text="Mapping saved. Identity is (publisher, source, logical tracker); changing source changes identity. Rebinding a logical tracker to another device in the same source requires Bridge restart. Confirm the applied revision in health.";
   }
  }
 }
}
