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
  internal static Dictionary<string,object> ReadHealth(string path){
   IOException last=null;
   for(int i=0;i<4;++i){try{return Read(path);}catch(IOException e){last=e;if(i<3)System.Threading.Thread.Sleep(25);}}
   throw last;
  }
  private sealed class ControlWindow:Window {
   readonly string root;readonly TextBlock status=new TextBlock{TextWrapping=TextWrapping.Wrap};
   readonly ListBox devices=new ListBox{Height=160};readonly ListBox mappings=new ListBox{Height=130};
   readonly TextBox source=new TextBox(),device=new TextBox(),tracker=new TextBox(),profile=new TextBox(),space=new TextBox(),revision=new TextBox(),world=new TextBox();
   readonly TextBox x=new TextBox{Text="0"},y=new TextBox{Text="0"},z=new TextBox{Text="0"};readonly CheckBox approved=new CheckBox{Content="This input space/revision has been approved"};
   readonly ComboBox routes=new ComboBox{ItemsSource=new[]{"steamvr","monaka","both","disabled"}};
   Dictionary<string,object> config;ArrayList map;ArrayList observations=new ArrayList();readonly DispatcherTimer timer=new DispatcherTimer();int editMappingIndex=-1;
   public ControlWindow(string root){this.root=root;Title="Monaka Bridge";Width=900;Height=820;
    var panel=new StackPanel{Margin=new Thickness(16)};Content=new ScrollViewer{Content=panel};
    panel.Children.Add(new TextBlock{Text="Sources and devices",FontSize=20});panel.Children.Add(devices);
    Add(panel,"Refresh / load configuration",Reload);Add(panel,"Use selected observation",SelectObservation);
    panel.Children.Add(new TextBlock{Text="Persistent tracker mapping and profile",FontSize=18});panel.Children.Add(mappings);mappings.SelectionChanged+=delegate{SelectMapping();};
    Field(panel,"Source",source);Field(panel,"Device",device);Field(panel,"Logical tracker",tracker);Field(panel,"Profile",profile);Field(panel,"Input map",space);Field(panel,"Input map revision",revision);Field(panel,"World map",world);panel.Children.Add(approved);
    Add(panel,"Save mapping",SaveMapping);Add(panel,"Show profile approval",delegate{var p=(Dictionary<string,object>)((Dictionary<string,object>)config["profiles"])[profile.Text];status.Text="Approved: "+p["approved"]+"\nEvidence: "+p["evidence"];});
    Add(panel,"Import legacy route / alignment",Migrate);
    panel.Children.Add(new TextBlock{Text="Shared alignment for the selected source map (metres)",FontSize=18});Field(panel,"X",x);Field(panel,"Y",y);Field(panel,"Z",z);
    Add(panel,"Apply shared translation",delegate{double.Parse(x.Text,CultureInfo.InvariantCulture);double.Parse(y.Text,CultureInfo.InvariantCulture);double.Parse(z.Text,CultureInfo.InvariantCulture);Command(root,"align "+Quote(tracker.Text)+" "+x.Text+" "+y.Text+" "+z.Text);Reload();});
    panel.Children.Add(routes);Add(panel,"Apply output policy",delegate{if(routes.SelectedItem==null)throw new InvalidOperationException("Select an output policy.");Command(root,"policy "+routes.SelectedItem);Reload();});
    Add(panel,"Start Bridge",delegate{Process.Start(new ProcessStartInfo(Native(root,"monaka_bridge_service"),Quote(ConfigPath(root))){UseShellExecute=false,CreateNoWindow=true});});
    Add(panel,"Stop Bridge",delegate{Process.Start(new ProcessStartInfo(Native(root,"monaka_bridge_service"),"--stop"){UseShellExecute=false,CreateNoWindow=true});});
    panel.Children.Add(status);timer.Interval=TimeSpan.FromSeconds(1);timer.Tick+=delegate{Health();};timer.Start();Closed+=delegate{timer.Stop();};Loaded+=delegate{Safe(Reload);};
   }
   // WPF button/error pattern migrated from Task2 SteamVrControlWpf.
   static Button MakeButton(string text,double width){return new Button{Content=text,Width=width,MinHeight=28,Margin=new Thickness(3)};}
   void Add(Panel p,string text,Action action){var b=MakeButton(text,260);b.Click+=delegate{Safe(action);};p.Children.Add(b);}
   void Safe(Action a){try{a();}catch(Exception e){status.Text=e.Message;MessageBox.Show(this,e.Message,"Monaka Bridge",MessageBoxButton.OK,MessageBoxImage.Error);}}
   static void Field(Panel p,string label,TextBox field){var row=new DockPanel();var text=new TextBlock{Text=label,Width=140};row.Children.Add(text);field.Margin=new Thickness(2);row.Children.Add(field);p.Children.Add(row);}
   void Reload(){config=Read(ConfigPath(root));map=new ArrayList((ICollection)config["mappings"]);mappings.Items.Clear();editMappingIndex=-1;foreach(Dictionary<string,object> b in map)mappings.Items.Add(b["tracker_id"]+" | "+b["source_id"]+" / "+b["device_id"]);routes.SelectedItem=Convert.ToString(config["policy"],CultureInfo.InvariantCulture);status.Text="Loaded revision "+config["mapping_revision"]+"; profile approval is required independently of space approval.";Health();}
   void Health(){try{
    string selectedSource=null,selectedDevice=null;
    if(devices.SelectedIndex>=0&&devices.SelectedIndex<observations.Count){var selected=(Dictionary<string,object>)observations[devices.SelectedIndex];selectedSource=(string)selected["source"];selectedDevice=(string)selected["device"];}
    var path=ConfigPath(root)+".status.json";var h=ReadHealth(path);var next=new ArrayList((ICollection)h["devices"]);devices.Items.Clear();observations=next;int restore=-1;int index=0;
    foreach(Dictionary<string,object> d in observations){devices.Items.Add(d["source"]+" / "+d["device"]+" | "+d["tracker"]+" | fresh="+d["fresh"]);if(selectedSource==(string)d["source"]&&selectedDevice==(string)d["device"])restore=index;++index;}
    if(restore>=0)devices.SelectedIndex=restore;
    if(DateTime.UtcNow-File.GetLastWriteTimeUtc(path)>TimeSpan.FromSeconds(3))status.Text="Bridge offline; displayed observations are historical.";
   }catch(Exception e){status.Text="Bridge health unavailable: "+e.Message;}}
   void Migrate(){
    var route=new Microsoft.Win32.OpenFileDialog{Title="Select the legacy output route file"};if(route.ShowDialog(this)!=true)return;
    var alignment=new Microsoft.Win32.OpenFileDialog{Title="Select the legacy alignment JSON"};if(alignment.ShowDialog(this)!=true)return;
    string target=ConfigPath(root),candidate=target+".migration-"+Guid.NewGuid().ToString("N");
    Command(root,"migrate "+Quote(route.FileName)+" "+Quote(alignment.FileName)+" "+Quote(candidate));
    File.Replace(candidate,target,target+".backup-"+Guid.NewGuid().ToString("N"));Reload();status.Text="Legacy settings imported with backups; review each space before enabling output.";
   }
   void SelectObservation(){if(devices.SelectedIndex<0)return;var d=(Dictionary<string,object>)observations[devices.SelectedIndex];source.Text=(string)d["source"];device.Text=(string)d["device"];space.Text=(string)d["space"];revision.Text=Convert.ToString(d["revision"],CultureInfo.InvariantCulture);approved.IsChecked=false;status.Text=(editMappingIndex>=0?"Selected mapping will be rebound to this observation on Save mapping. ":"")+"Observed convention: "+d["convention"]+". Select a verified profile before approving output.";}
   void SelectMapping(){if(map==null||mappings.SelectedIndex<0)return;editMappingIndex=mappings.SelectedIndex;var b=(Dictionary<string,object>)map[editMappingIndex];source.Text=(string)b["source_id"];device.Text=(string)b["device_id"];tracker.Text=(string)b["tracker_id"];profile.Text=(string)b["profile"];space.Text=(string)b["input_space"];revision.Text=Convert.ToString(b["input_revision"],CultureInfo.InvariantCulture);world.Text=(string)b["world_space"];approved.IsChecked=(bool)b["space_approved"];var t=(IList)((Dictionary<string,object>)b["world"])["translation"];x.Text=Convert.ToString(t[0],CultureInfo.InvariantCulture);y.Text=Convert.ToString(t[1],CultureInfo.InvariantCulture);z.Text=Convert.ToString(t[2],CultureInfo.InvariantCulture);}
   static Dictionary<string,object> Identity(){return new Dictionary<string,object>{{"rotation",new double[]{0,0,0,1}},{"translation",new double[]{0,0,0}}};}
   void SaveMapping(){
    var fresh=Read(ConfigPath(root));if(Convert.ToUInt32(fresh["mapping_revision"])!=Convert.ToUInt32(config["mapping_revision"]))throw new InvalidOperationException("Configuration changed; reload first.");
    Dictionary<string,object> selected=null;
    if(editMappingIndex>=0&&editMappingIndex<map.Count)selected=(Dictionary<string,object>)map[editMappingIndex];
    if(selected==null)foreach(Dictionary<string,object>b in map)if((string)b["source_id"]==source.Text&&(string)b["device_id"]==device.Text)selected=b;
    if(selected==null){selected=new Dictionary<string,object>{{"world",Identity()},{"mount",Identity()},{"world_revision",0}};map.Add(selected);}
    selected["source_id"]=source.Text;selected["device_id"]=device.Text;selected["tracker_id"]=tracker.Text;selected["profile"]=profile.Text;selected["input_space"]=space.Text;selected["input_revision"]=UInt32.Parse(revision.Text);selected["world_space"]=world.Text;selected["space_approved"]=approved.IsChecked==true;
    config["mappings"]=map.ToArray();config["mapping_revision"]=checked(Convert.ToUInt32(config["mapping_revision"])+1);
    string target=ConfigPath(root),candidate=target+".candidate-"+Guid.NewGuid().ToString("N");File.WriteAllText(candidate,new JavaScriptSerializer().Serialize(config),new UTF8Encoding(false));
    using(var p=Process.Start(new ProcessStartInfo(Native(root,"monaka_bridge_config"),Quote(candidate)+" validate"){UseShellExecute=false,CreateNoWindow=true})){p.WaitForExit();if(p.ExitCode!=0)throw new InvalidOperationException("Invalid mapping; candidate retained for review.");}
    File.Replace(candidate,target,target+".backup-"+Guid.NewGuid().ToString("N"));Reload();
   }
  }
 }
}
