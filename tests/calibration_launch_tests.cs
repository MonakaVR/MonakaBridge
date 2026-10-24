using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;
using MonakaBridge;
class CalibrationLaunchFixture {
 static int Main(string[] args){try{
  var selected=GuiLaunchOptions.Parse(args,AppDomain.CurrentDomain.BaseDirectory);Console.OutputEncoding=new UTF8Encoding(false);
  Console.WriteLine(new JavaScriptSerializer().Serialize(new Dictionary<string,object>{{"config",selected.ConfigFile},{"health",selected.HealthFile},{"readonly",selected.ReadOnly},{"window",selected.ControlWindowOnly},{"name",selected.InstallationName},{"v2",selected.Calibration!=null},{"profile",selected.Calibration==null?null:selected.Calibration.ProfilePath},{"profile_sha256",selected.Calibration==null?null:selected.Calibration.ProfileSha256}}));return 0;
 }catch(Exception e){Console.Error.WriteLine(e.Message);return 17;}}
}
class CalibrationLaunchTests {
 static int cases;static string directory,child,config,health,helper,native;
 static readonly JavaScriptSerializer json=new JavaScriptSerializer();
 static void Check(bool okay,string why){if(!okay)throw new Exception(why);}
 static void Reject(Action action){try{action();}catch(ArgumentException){cases++;return;}catch(InvalidOperationException){cases++;return;}throw new Exception("Expected launch rejection");}
 static Dictionary<string,object> Document(bool readOnly){string hash=new string('a',64);return new Dictionary<string,object>{{"schema",CalibrationInstallation.Schema},{"name","selected 測定"},{"config",config},{"health",health},{"config_helper",helper},{"helper_sha256",hash},{"read_only",readOnly},{"calibration_helper",native},{"calibration_helper_sha256",hash},{"calibration_api",1},{"calibration_software_policy",1},{"calibration_gui_contract",1},{"bridge_id","fixture-publisher"},{"installation_path",directory},{"association",CalibrationInstallation.Association},{"single_bridge_no_relay",true},{"mirror_address","127.0.0.1"},{"mirror_port",29813},{"source","pico"},{"device","device"},{"tracker","hip"},{"input_space","native"},{"input_revision",1},{"reference_role","left"}};}
 static Dictionary<string,object> Child(GuiLaunchOptions options,int expected){using(var process=CalibrationOwnedProcess.Start(child,options.WindowArguments(),true,null,null)){
  var line=CalibrationPipeFrames.ReadLine(process.Output,65536);Check(process.WaitForExit(3000)&&process.ExitCode==expected,"child outcome");
  if(expected!=0){Check(line==null,"rejected snapshot produced a target");return null;}Check(line!=null,"child snapshot missing");return CalibrationJson.Object(line,65536,32768);
 }}
 static int Main(string[] args){try{
  Check(args.Length==2,"child and evidence root required");child=Path.GetFullPath(args[0]);directory=Path.GetFullPath(args[1]);Directory.CreateDirectory(directory);config=Path.Combine(directory,"config 測定.json");health=Path.Combine(directory,"health.json");helper=Path.Combine(directory,"config.exe");native=Path.Combine(directory,"native.exe");
  string v1=Path.Combine(directory,"installation-v1.json");byte[] old=CalibrationJson.Utf8.GetBytes(json.Serialize(new{schema="monaka.bridge.installation.v1",name="legacy 測定",config=config,health=health,config_helper=helper,read_only=false}));File.WriteAllBytes(v1,old);
  var legacy=GuiLaunchOptions.Parse(new[]{"--installation",v1},directory);Check(!legacy.ReadOnly&&legacy.Calibration==null,"v1 readonly/target changed");var legacyChild=Child(legacy,0);Check(!(bool)legacyChild["v2"]&&!(bool)legacyChild["readonly"]&&legacyChild["config"].Equals(config),"v1 child flattening changed");cases++;
  string v2=Path.Combine(directory,"installation-v2.json");byte[] raw=CalibrationJson.Encode(Document(false),65536);File.WriteAllBytes(v2,raw);var selected=GuiLaunchOptions.Parse(new[]{"--installation",v2},directory);Check(!selected.ReadOnly&&selected.Calibration!=null,"explicit writable v2 profile unavailable");var delivered=Child(selected,0);Check((bool)delivered["v2"]&&(bool)delivered["window"]&&delivered["profile"].Equals(v2)&&delivered["profile_sha256"].Equals(selected.Calibration.ProfileSha256)&&delivered["config"].Equals(config)&&!(bool)delivered["readonly"],"v2 launch snapshot drift");cases++;
  var readOnly=GuiLaunchOptions.Parse(new[]{"--installation",v2,"--read-only"},directory);Check((bool)Child(readOnly,0)["readonly"],"effective readonly lost in child");cases++;
  File.WriteAllBytes(v2,CalibrationJson.Encode(Document(true),65536));var locked=GuiLaunchOptions.Parse(new[]{"--installation",v2,"--allow-write"},directory);Check(locked.ReadOnly&&(bool)Child(locked,0)["readonly"],"CLI upgraded readonly v2");cases++;
  Check(selected.Calibration.ProfileSha256==CalibrationInstallation.Sha256(raw)&&!selected.ReadOnly,"selected snapshot mutated after file replacement");Child(selected,17);cases++;
  Reject(delegate{GuiLaunchOptions.Parse(new[]{"--installation",v1,"--profile-sha256",new string('a',64)},directory);});
  Reject(delegate{GuiLaunchOptions.Parse(new[]{"--profile-sha256",new string('a',64)},directory);});
  Reject(delegate{GuiLaunchOptions.Parse(new[]{"--installation",v2,"--config",config},directory);});
  Reject(delegate{GuiLaunchOptions.Parse(new[]{"--installation",v2,"--read-only","--allow-write"},directory);});
  Reject(delegate{GuiLaunchOptions.Parse(new[]{"--installation",v2,"--profile-sha256",new string('g',64)},directory);});
  Check(Convert.ToBase64String(old)==Convert.ToBase64String(File.ReadAllBytes(v1)),"v1 profile upgraded/written");cases++;
  Console.WriteLine("PASS v1/v2 actual child argument/snapshot "+cases+" cases; profile replacement rejects/no fallback; effective readonly preserved; no GUI/SDK/socket/config writes");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
