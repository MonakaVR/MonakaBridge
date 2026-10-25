using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Windows;
using System.Windows.Controls;

namespace MonakaBridge {
 internal static class CalibrationProfileDraft {
  internal static string FileHash(string path){using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read))using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(file)).Replace("-","").ToLowerInvariant();}
  internal static Dictionary<string,object> Create(GuiLaunchOptions target,Dictionary<string,object> config,Dictionary<string,object> mapping,string name,string helper,int port,string role,bool single,bool readOnly) {
   if(config==null||mapping==null)throw new InvalidOperationException("Select an existing persisted mapping first");
   if(!single)throw new InvalidOperationException("Explicitly declare one Bridge and no relay for this mirror");
   if(role!="left"&&role!="right"&&role!="hmd")throw new InvalidOperationException("Select the reference role explicitly");
   if(port<1||port>65535)throw new InvalidOperationException("Enter the explicitly configured loopback mirror port");
   helper=GuiLaunchOptions.Absolute(helper,"calibration helper");
   return new Dictionary<string,object>{{"schema",CalibrationInstallation.Schema},{"name",name},{"config",target.ConfigFile},{"health",target.HealthFile},{"config_helper",target.ConfigHelper},{"helper_sha256",FileHash(target.ConfigHelper)},{"read_only",target.ReadOnly||readOnly},
    {"calibration_helper",helper},{"calibration_helper_sha256",FileHash(helper)},{"calibration_api",1},{"calibration_software_policy",1},{"calibration_gui_contract",1},
    {"bridge_id",config["bridge_id"]},{"installation_path",target.Root},{"association",CalibrationInstallation.Association},{"single_bridge_no_relay",true},{"mirror_address","127.0.0.1"},{"mirror_port",port},
    {"source",mapping["source_id"]},{"device",mapping["device_id"]},{"tracker",mapping["tracker_id"]},{"input_space",mapping["input_space"]},{"input_revision",mapping["input_revision"]},{"reference_role",role}};
  }
 }
 internal sealed class CalibrationProfileDialog:Window {
  readonly GuiLaunchOptions target;readonly Dictionary<string,object> config,mapping;
  readonly TextBox name=new TextBox(),helper=new TextBox(),port=new TextBox();
  readonly ComboBox role=new ComboBox{ItemsSource=new[]{"left","right","hmd"},SelectedIndex=-1};
  readonly CheckBox single=new CheckBox{Content="I declare this is the configured mirror of one Bridge, with no relay"};
  readonly CheckBox readOnly=new CheckBox{Content="Read only (measurement allowed, saving disabled)"};
  readonly TextBlock error=new TextBlock{TextWrapping=TextWrapping.Wrap};
  internal CalibrationInstallation Created {get;private set;}
  internal CalibrationProfileDialog(GuiLaunchOptions selected,Dictionary<string,object> persisted,Dictionary<string,object> binding){
   target=selected;config=persisted;mapping=binding;Title="Create calibration installation profile (Save As)";Width=700;Height=610;WindowStartupLocation=WindowStartupLocation.CenterOwner;
   var panel=new StackPanel{Margin=new Thickness(16)};Content=new ScrollViewer{Content=panel};
   panel.Children.Add(new TextBlock{Text="This creates a separate new profile. It does not change Bridge configuration or start a native worker. Select a persisted mapping, the matching calibration helper, configured mirror port and reference role. Session/clock/Standing witnesses are discovered later.",TextWrapping=TextWrapping.Wrap});
   panel.Children.Add(new TextBlock{Text="Config: "+target.ConfigFile+"\nBridge: "+config["bridge_id"]+"\nMapping: "+mapping["source_id"]+" / "+mapping["device_id"]+" / "+mapping["tracker_id"]+"\nInput space: "+mapping["input_space"]+" / "+mapping["input_revision"],TextWrapping=TextWrapping.Wrap});
   name.Text=target.InstallationName+" calibration";Field(panel,"Profile name",name);Field(panel,"Calibration helper",helper);
   string packaged=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"monaka_bridge_calibrator.exe");if(File.Exists(packaged))helper.Text=packaged;
   var browse=new Button{Content="Select calibration helper...",Margin=new Thickness(3)};browse.Click+=delegate{var dialog=new Microsoft.Win32.OpenFileDialog{Title="Select the matching product calibrator",Filter="Executable|*.exe"};if(dialog.ShowDialog(this)==true)helper.Text=dialog.FileName;};panel.Children.Add(browse);
   Field(panel,"127.0.0.1 mirror port",port);port.ToolTip="Enter the configured mirror port. The existing operator wrapper uses 29813; this form does not discover or bind it.";
   Field(panel,"Reference role",role);panel.Children.Add(single);readOnly.IsChecked=target.ReadOnly;readOnly.IsEnabled=!target.ReadOnly;panel.Children.Add(readOnly);
   panel.Children.Add(new TextBlock{Text="Mirror/process association is operator declared; process ownership is unverified. No reference role is guessed. Native helper checks its API/features before open and rechecks the exact target during discover/confirm/commit.",TextWrapping=TextWrapping.Wrap});
   var save=new Button{Content="Save As new v2 profile...",MinHeight=32,Margin=new Thickness(3)};save.Click+=delegate{Save();};panel.Children.Add(save);panel.Children.Add(error);
  }
  static void Field(Panel panel,string title,Control field){panel.Children.Add(new TextBlock{Text=title,Margin=new Thickness(0,8,0,1)});panel.Children.Add(field);}
  void Save(){try{
   int selectedPort;if(!int.TryParse(port.Text,NumberStyles.None,CultureInfo.InvariantCulture,out selectedPort))throw new InvalidOperationException("Enter an explicit decimal mirror port");
   var document=CalibrationProfileDraft.Create(target,config,mapping,name.Text,helper.Text,selectedPort,role.SelectedItem as string,single.IsChecked==true,readOnly.IsChecked==true);
   var dialog=new Microsoft.Win32.SaveFileDialog{Title="Create a separate new calibration profile",Filter="JSON profile|*.json",FileName="bridge-calibration-v2.json",OverwritePrompt=true};if(dialog.ShowDialog(this)!=true)return;
   Created=CalibrationInstallation.SaveNew(dialog.FileName,document,new[]{target.ConfigFile,target.HealthFile,target.ConfigHelper,helper.Text,target.Calibration==null?null:target.Calibration.ProfilePath});DialogResult=true;
  }catch(Exception e){error.Text=e.Message;}}
 }
}
