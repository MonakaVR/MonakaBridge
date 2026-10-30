using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using MonakaBridge;
class CalibrationProfileTests {
 static int cases;static string directory,path,config,native,helper;static byte[] original;
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool CreateHardLink(string link,string target,IntPtr attributes);
 static void Check(bool ok){if(!ok)throw new Exception("profile assertion");}
 static void Reject(Action action){try{action();}catch(Exception e){if(!(e is InvalidOperationException)&&!(e is ArgumentException)&&!(e is IOException))throw;cases++;return;}throw new Exception("Expected profile rejection");}
 static Dictionary<string,object> Document(){return new Dictionary<string,object>{
  {"schema",CalibrationInstallation.Schema},{"name","profile 測定"},{"config","config 測定.json"},{"health","health.json"},{"config_helper","config-helper.fixture"},
  {"helper_sha256",CalibrationInstallation.Sha256(File.ReadAllBytes(helper))},{"read_only",true},{"calibration_helper","native-helper.fixture"},
  {"calibration_helper_sha256",CalibrationInstallation.Sha256(File.ReadAllBytes(native))},{"calibration_api",1},{"calibration_software_policy",1},{"calibration_gui_contract",1},
  {"bridge_id","fixture-publisher"},{"installation_path",directory},{"association",CalibrationInstallation.Association},{"single_bridge_no_relay",true},
  {"mirror_address","127.0.0.1"},{"mirror_port",29813},{"source","pico"},{"device","device"},{"tracker","hip"},{"input_space","native"},{"input_revision",UInt32.MaxValue},{"reference_role","left"}};}
 static byte[] Bytes(Dictionary<string,object> doc){return CalibrationJson.Encode(doc,65536);}
 static CalibrationInstallation Parse(Dictionary<string,object> doc){return CalibrationInstallation.Parse(path,Bytes(doc));}
 static void Mutation(string key,object value){var doc=Document();doc[key]=value;Reject(delegate{Parse(doc);});}
 static int Main(){try{
  directory=Path.Combine(Path.GetTempPath(),"monaka-u1-profile-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(directory);path=Path.Combine(directory,"installation-v2.json");
  config=Path.Combine(directory,"config 測定.json");native=Path.Combine(directory,"native-helper.fixture");helper=Path.Combine(directory,"config-helper.fixture");
  File.WriteAllText(config,"{\"synthetic\":true}",new UTF8Encoding(false));original=File.ReadAllBytes(config);File.WriteAllText(native,"test-only file hash, never executable");File.WriteAllText(helper,"test-only config helper hash, never executable");
  byte[] raw=Bytes(Document());File.WriteAllBytes(path,raw);var profile=CalibrationInstallation.Load(path,null);
  Check(profile.ConfigFile==config&&profile.CalibrationHelper==native&&profile.ConfigHelper==helper&&profile.InputRevision==UInt32.MaxValue&&profile.ReferenceRole=="left");cases++;
  byte[] snapshot=profile.SnapshotBytes();snapshot[0]=0;Check(profile.ProfileSha256==CalibrationInstallation.Sha256(raw)&&profile.SnapshotBytes()[0]==raw[0]);cases++;
  Check((bool)profile.OpenRequest(false)["readonly"]);var writable=Document();writable["read_only"]=false;Check((bool)Parse(writable).OpenRequest(true)["readonly"]);cases++;
  profile.VerifyHelper();cases++;
  File.AppendAllText(native,"drift");Reject(profile.VerifyHelper);File.WriteAllText(native,"test-only file hash, never executable");
  var changed=Document();changed["name"]="replaced";File.WriteAllBytes(path,Bytes(changed));Reject(delegate{CalibrationInstallation.Load(path,profile.ProfileSha256);});File.WriteAllBytes(path,raw);
  Mutation("unknown",true);Mutation("schema","monaka.bridge.installation.v1");Mutation("read_only","true");Mutation("single_bridge_no_relay",false);Mutation("association","inferred");
  Mutation("calibration_api",2);Mutation("calibration_software_policy",2);Mutation("calibration_gui_contract",2);Mutation("mirror_address","localhost");Mutation("mirror_port",0);Mutation("mirror_port",65536);
  Mutation("input_revision",-1);Mutation("input_revision",4294967296L);Mutation("reference_role","automatic");Mutation("reference_role","Left");Mutation("source","pico\nother");
  Mutation("config_helper","config 測定.json");Mutation("calibration_helper","config-helper.fixture");Mutation("helper_sha256",new string('g',64));Mutation("name",new string('測',100));
  var missing=Document();missing.Remove("reference_role");Reject(delegate{Parse(missing);});
  string duplicate="{\"schema\":\""+CalibrationInstallation.Schema+"\","+CalibrationJson.Utf8.GetString(raw).Substring(1);Reject(delegate{CalibrationInstallation.Parse(path,CalibrationJson.Utf8.GetBytes(duplicate));});
  Reject(delegate{CalibrationInstallation.Parse(path,new byte[65537]);});
  var integer=Document();string badInteger=CalibrationJson.Utf8.GetString(Bytes(integer)).Replace("\"mirror_port\":29813","\"mirror_port\":1e0");Reject(delegate{CalibrationInstallation.Parse(path,CalibrationJson.Utf8.GetBytes(badInteger));});
  string saved=Path.Combine(directory,"saved-v2.json");var copy=CalibrationInstallation.SaveNew(saved,profile.AbsoluteDocument(),new[]{path,config,native,helper});
  Check(copy.ConfigFile==profile.ConfigFile&&copy.CalibrationHelper==native&&File.Exists(saved)&&CalibrationInstallation.Load(saved,copy.ProfileSha256).ReadOnly);cases++;
  byte[] before=File.ReadAllBytes(saved);Reject(delegate{CalibrationInstallation.SaveNew(saved,profile.AbsoluteDocument(),null);});Check(Convert.ToBase64String(before)==Convert.ToBase64String(File.ReadAllBytes(saved)));
  Reject(delegate{CalibrationInstallation.SaveNew(config,profile.AbsoluteDocument(),new[]{path,config,native,helper});});
  string alias=Path.Combine(directory,"config-hardlink-alias.json");Check(CreateHardLink(alias,config,IntPtr.Zero));Reject(delegate{CalibrationInstallation.SaveNew(alias,profile.AbsoluteDocument(),null);});
  using(var writer=new FileStream(path,FileMode.Open,FileAccess.Write,FileShare.ReadWrite)){Reject(delegate{CalibrationInstallation.Load(path,null);});}
  string relocated=Path.Combine(directory,"relocated");Directory.CreateDirectory(relocated);var moved=CalibrationInstallation.SaveNew(Path.Combine(relocated,"new-v2.json"),profile.AbsoluteDocument(),new[]{path,config,native,helper});Check(moved.ConfigFile==config&&moved.HealthFile==profile.HealthFile&&moved.CalibrationHelper==native);cases++;
  var row=new Dictionary<string,object>{{"source_id","pico"},{"device_id","device"},{"tracker_id","hip"},{"input_space","current-frame"},{"input_revision",2}};
  var current=new Dictionary<string,object>{{"bridge_id","fixture-publisher"},{"mappings",new[]{row}}};
  File.WriteAllBytes(config,Bytes(current));var resolved=profile.OpenMeasurementRequest(false);
  Check((string)resolved["input_space"]=="current-frame"&&Convert.ToInt64(resolved["input_revision"])==2&&profile.InputRevision==UInt32.MaxValue&&profile.ProfileSha256==CalibrationInstallation.Sha256(raw));cases++;
  row["input_space"]="native";row["input_revision"]=1;File.WriteAllBytes(config,Bytes(current));Check(Convert.ToInt64(profile.OpenMeasurementRequest(false)["input_revision"])==1);cases++;
  foreach(string field in new[]{"source_id","device_id","tracker_id"}){
   object savedValue=row[field];row[field]="rebound";File.WriteAllBytes(config,Bytes(current));Reject(delegate{profile.OpenMeasurementRequest(false);});row[field]=savedValue;
  }
  current["bridge_id"]="different-bridge";File.WriteAllBytes(config,Bytes(current));Reject(delegate{profile.OpenMeasurementRequest(false);});current["bridge_id"]="fixture-publisher";
  current["mappings"]=new Dictionary<string,object>[0];File.WriteAllBytes(config,Bytes(current));Reject(delegate{profile.OpenMeasurementRequest(false);});
  current["mappings"]=new[]{row,row};File.WriteAllBytes(config,Bytes(current));Reject(delegate{profile.OpenMeasurementRequest(false);});
  Check(Convert.ToBase64String(raw)==Convert.ToBase64String(File.ReadAllBytes(path)));File.WriteAllBytes(config,original);
  Check(Convert.ToBase64String(original)==Convert.ToBase64String(File.ReadAllBytes(config))&&Convert.ToBase64String(raw)==Convert.ToBase64String(File.ReadAllBytes(path)));cases++;
  Console.WriteLine("PASS calibration v2 profile "+cases+" cases; immutable byte/hash snapshot; CreateNew/hardlink/readonly; synthetic files only; GUI/SDK/socket NOT_RUN");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
