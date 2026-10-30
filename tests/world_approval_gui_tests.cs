using System;
using System.IO;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using System.Web.Script.Serialization;
using MonakaBridge;
class WorldApprovalGuiTests {
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static object Field(object value,string name){return value.GetType().GetField(name,BindingFlags.NonPublic|BindingFlags.Instance).GetValue(value);}
 static void Until(Func<bool> ready){var watch=Stopwatch.StartNew();while(!ready()){Check(watch.ElapsedMilliseconds<20000,"WPF fixture deadline");Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);Thread.Sleep(5);}Dispatcher.CurrentDispatcher.Invoke((Action)delegate{},DispatcherPriority.Background);}
 static void Align(string helper,string target,string x,string y,string z){using(var p=Process.Start(new ProcessStartInfo(helper,GuiLaunchOptions.Quote(target)+" align logical-pico-device-1 "+x+" "+y+" "+z){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true})){string output=p.StandardOutput.ReadToEnd(),error=p.StandardError.ReadToEnd();p.WaitForExit();Check(p.ExitCode==0,output+error);}}
 [STAThread]static int Main(string[] args){try{
  Check(args.Length==3,"fixture/helper/output");var type=typeof(SteamVrControlWpf).GetNestedType("ControlWindow",BindingFlags.NonPublic);int cases=0;
  foreach(bool same in new[]{false,true}){
   string root=Path.Combine(args[2],Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);string target=Path.Combine(root,"config.json"),direct=Path.Combine(root,"direct.json");File.Copy(args[0],target);File.Copy(args[0],direct);
   string x=same?"1":"2",y=same?"0":"3",z=same?"0":"4";Align(args[1],direct,x,y,z);
   var options=GuiLaunchOptions.Parse(new[]{root,"--config",target,"--health",Path.Combine(root,"absent-health.json"),"--config-helper",Path.GetFullPath(args[1]),"--allow-write"},root);
   var window=(Window)Activator.CreateInstance(type,new object[]{options});
   try{
    type.GetMethod("Reload",BindingFlags.NonPublic|BindingFlags.Instance,null,Type.EmptyTypes,null).Invoke(window,null);((ListBox)Field(window,"mappings")).SelectedIndex=0;
    ((TextBox)Field(window,"x")).Text=x;((TextBox)Field(window,"y")).Text=y;((TextBox)Field(window,"z")).Text=z;
    var panel=(StackPanel)((ScrollViewer)window.Content).Content;Button action=null;foreach(var child in panel.Children){var button=child as Button;if(button!=null&&Convert.ToString(button.Content)=="Apply shared translation")action=button;}
    Check(action!=null&&action.IsEnabled,"normal GUI action unavailable");action.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Until(delegate{return !(bool)Field(window,"busy");});
    Check(Convert.ToBase64String(File.ReadAllBytes(target))==Convert.ToBase64String(File.ReadAllBytes(direct)),"GUI native candidate differs from actual align result");
    var value=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(target));int invalidated=0,other=0;
    foreach(Dictionary<string,object> row in (IList)value["mappings"]){bool group=(string)row["source_id"]=="pico"&&(string)row["input_space"]=="native"&&Convert.ToInt32(row["input_revision"])==1;if(group){Check((bool)row["space_approved"],"GUI altered diagnostic approval");++invalidated;}else{Check((bool)row["space_approved"],"GUI invalidated unrelated group");++other;}}
    Check(invalidated==4&&other==3&&Convert.ToInt32(value["mapping_revision"])==(same?10:11),"wrong group/revision");Check(((TextBlock)Field(window,"status")).Text.Contains("Runtime application unconfirmed"),"save implied runtime application");cases++;
   }finally{window.Close();}
  }
  Console.WriteLine("PASS World approval normal WPF "+cases+" unshown flows: actual Apply shared translation button, native exact candidate/CAS, changed W updates four and same W is no-op, other groups preserved, runtime unconfirmed; no Show/SDK/socket/HIL");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
