using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace MonakaBridge {
 // Separate v2 local profile. Runtime session/clock/frame/floors are not saved.
 internal sealed class CalibrationInstallation {
  internal const string Schema="monaka.bridge.installation.v2";
  internal const string Association="operator_declared_process_owner_unverified";
  internal readonly string ProfilePath,ProfileSha256,Name,ConfigFile,HealthFile,ConfigHelper,ConfigHelperSha256;
  internal readonly string CalibrationHelper,CalibrationHelperSha256,InstallationPath,BridgeId,MirrorAddress;
  internal readonly string Source,Device,Tracker,InputSpace,ReferenceRole;
  internal readonly uint InputRevision;internal readonly ushort MirrorPort;
  internal readonly bool ReadOnly;
  internal readonly int ApiVersion,SoftwarePolicy,GuiContract;
  readonly byte[] profileBytes;
  static readonly string[] Required={"schema","name","config","health","config_helper","helper_sha256","read_only",
   "calibration_helper","calibration_helper_sha256","calibration_api","calibration_software_policy","calibration_gui_contract",
   "bridge_id","installation_path","association","single_bridge_no_relay","mirror_address","mirror_port",
   "source","device","tracker","input_space","input_revision","reference_role"};
  internal static string Sha256(byte[] bytes){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-","").ToLowerInvariant();}
  static string Resolve(Dictionary<string,object> value,string name,string directory) {
   string text=CalibrationJson.Text(value,name,32768);
   return GuiLaunchOptions.Absolute(Path.IsPathRooted(text)?text:Path.Combine(directory,text),name);
  }
  CalibrationInstallation(string path,byte[] bytes) {
   if(bytes==null||bytes.Length>65536)throw new InvalidOperationException("Calibration profile byte limit");
   ProfilePath=GuiLaunchOptions.Absolute(path,"installation");profileBytes=(byte[])bytes.Clone();ProfileSha256=Sha256(profileBytes);
   var value=CalibrationJson.Object(profileBytes,65536,32768);CalibrationJson.Keys(value,Required,null);
   if(CalibrationJson.Text(value,"schema",128)!=Schema)throw new InvalidOperationException("Unsupported calibration profile schema");
   Name=CalibrationJson.Text(value,"name",128);ReadOnly=CalibrationJson.Boolean(value,"read_only");string directory=Path.GetDirectoryName(ProfilePath);
   ConfigFile=Resolve(value,"config",directory);HealthFile=Resolve(value,"health",directory);ConfigHelper=Resolve(value,"config_helper",directory);
   ConfigHelperSha256=CalibrationJson.Hash(value,"helper_sha256");CalibrationHelper=Resolve(value,"calibration_helper",directory);CalibrationHelperSha256=CalibrationJson.Hash(value,"calibration_helper_sha256");
   InstallationPath=Resolve(value,"installation_path",directory);BridgeId=CalibrationJson.Text(value,"bridge_id",4096);
   ApiVersion=(int)CalibrationJson.Integer(value,"calibration_api",1,1);SoftwarePolicy=(int)CalibrationJson.Integer(value,"calibration_software_policy",1,1);GuiContract=(int)CalibrationJson.Integer(value,"calibration_gui_contract",1,1);
   if(CalibrationJson.Text(value,"association",128)!=Association||!CalibrationJson.Boolean(value,"single_bridge_no_relay"))throw new InvalidOperationException("Explicit single Bridge/no relay declaration required; process ownership unverified");
   MirrorAddress=CalibrationJson.Text(value,"mirror_address",64);if(MirrorAddress!="127.0.0.1")throw new InvalidOperationException("Only explicit 127.0.0.1 mirror is supported");
   MirrorPort=(ushort)CalibrationJson.Integer(value,"mirror_port",1,65535);
   Source=CalibrationJson.Text(value,"source",4096);Device=CalibrationJson.Text(value,"device",4096);Tracker=CalibrationJson.Text(value,"tracker",4096);
   InputSpace=CalibrationJson.Text(value,"input_space",4096);InputRevision=(uint)CalibrationJson.Integer(value,"input_revision",0,UInt32.MaxValue);
   ReferenceRole=CalibrationJson.Text(value,"reference_role",32);if(ReferenceRole!="left"&&ReferenceRole!="right"&&ReferenceRole!="hmd")throw new InvalidOperationException("Select explicit left/right/HMD reference");
   var distinct=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach(string file in new[]{ProfilePath,ConfigFile,HealthFile,ConfigHelper,CalibrationHelper})if(!distinct.Add(file))throw new InvalidOperationException("Profile, config, health and helper paths must be distinct");
  }
  internal static CalibrationInstallation Parse(string path,byte[] bytes){return new CalibrationInstallation(path,bytes);}
  internal static byte[] ReadBytes(string path) {
   path=GuiLaunchOptions.Absolute(path,"installation");byte[] bytes;
   using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) {
    if(file.Length>65536)throw new InvalidOperationException("Calibration profile byte limit");
    using(var copy=new MemoryStream()){
     byte[] buffer=new byte[4096];int n;
     while((n=file.Read(buffer,0,Math.Min(buffer.Length,65537-(int)copy.Length)))>0){copy.Write(buffer,0,n);if(copy.Length>65536)throw new InvalidOperationException("Calibration profile byte limit");}
     bytes=copy.ToArray();
    }
   }
   return bytes;
  }
  internal static CalibrationInstallation Load(string path,string expectedHash) {
   var selected=new CalibrationInstallation(path,ReadBytes(path));
   if(expectedHash!=null&&!string.Equals(selected.ProfileSha256,expectedHash,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("Selected profile changed since launch; reopen explicitly");return selected;
  }
  internal byte[] SnapshotBytes(){return (byte[])profileBytes.Clone();}
  internal void VerifyHelper() {
   using(var input=new FileStream(CalibrationHelper,FileMode.Open,FileAccess.Read,FileShare.Read))using(var sha=SHA256.Create()) {
    string actual=BitConverter.ToString(sha.ComputeHash(input)).Replace("-","").ToLowerInvariant();
    if(actual!=CalibrationHelperSha256)throw new InvalidOperationException("Calibration helper SHA256 mismatch");
   }
  }
  internal Dictionary<string,object> OpenMeasurementRequest(bool effectiveReadOnly) {
   // Resolve only the selected opaque identity in the current saved snapshot.
   // A successful recalibration may have advanced its stored frame since this
   // installation profile was created. Native Open still validates and freezes
   // the complete current config; subsequent changes remain CAS conflicts.
   byte[] bytes;
   using(var file=new FileStream(ConfigFile,FileMode.Open,FileAccess.Read,FileShare.Read)){
    if(file.Length>2*1024*1024)throw new InvalidOperationException("Calibration config byte limit");
    bytes=new byte[(int)file.Length];int at=0,n;
    while(at<bytes.Length&&(n=file.Read(bytes,at,bytes.Length-at))>0)at+=n;
    if(at!=bytes.Length||file.ReadByte()!=-1)throw new InvalidOperationException("Calibration config changed during read");
   }
   var config=new System.Web.Script.Serialization.JavaScriptSerializer{MaxJsonLength=2*1024*1024,RecursionLimit=16}.Deserialize<Dictionary<string,object>>(CalibrationJson.Utf8.GetString(bytes).TrimStart('\uFEFF'));
   if(CalibrationJson.Text(config,"bridge_id",4096)!=BridgeId)throw new InvalidOperationException("Calibration config bridge differs from selected target");
   Dictionary<string,object> selected=null;
   foreach(Dictionary<string,object> row in (System.Collections.IEnumerable)config["mappings"])
    if(CalibrationJson.Text(row,"source_id",4096)==Source&&CalibrationJson.Text(row,"device_id",4096)==Device){
     if(selected!=null||CalibrationJson.Text(row,"tracker_id",4096)!=Tracker)throw new InvalidOperationException("Calibration mapping differs from selected target");selected=row;
    }
   if(selected==null)throw new InvalidOperationException("Calibration mapping missing for selected target");
   var request=OpenRequest(effectiveReadOnly);request["input_space"]=CalibrationJson.Text(selected,"input_space",4096);request["input_revision"]=CalibrationJson.Integer(selected,"input_revision",0,UInt32.MaxValue);return request;
  }
  internal Dictionary<string,object> OpenRequest(bool effectiveReadOnly) {
   // Request ID is added by the one native client. No pose/world numeric data.
   return new Dictionary<string,object>{{"config_path",ConfigFile},{"source",Source},{"device",Device},{"tracker",Tracker},
    {"input_space",InputSpace},{"input_revision",InputRevision},{"readonly",ReadOnly||effectiveReadOnly},{"bridge_id",BridgeId},
    {"installation_path",InstallationPath},{"helper_path",CalibrationHelper},{"helper_sha256",CalibrationHelperSha256},
    {"association",Association},{"mirror_address",MirrorAddress},{"mirror_port",MirrorPort},{"reference_role",ReferenceRole}};
  }
  internal Dictionary<string,object> AbsoluteDocument() {
   return new Dictionary<string,object>{{"schema",Schema},{"name",Name},{"config",ConfigFile},{"health",HealthFile},{"config_helper",ConfigHelper},
    {"helper_sha256",ConfigHelperSha256},{"read_only",ReadOnly},{"calibration_helper",CalibrationHelper},{"calibration_helper_sha256",CalibrationHelperSha256},
    {"calibration_api",ApiVersion},{"calibration_software_policy",SoftwarePolicy},{"calibration_gui_contract",GuiContract},{"bridge_id",BridgeId},
    {"installation_path",InstallationPath},{"association",Association},{"single_bridge_no_relay",true},{"mirror_address",MirrorAddress},{"mirror_port",MirrorPort},
    {"source",Source},{"device",Device},{"tracker",Tracker},{"input_space",InputSpace},{"input_revision",InputRevision},{"reference_role",ReferenceRole}};
  }
  internal static CalibrationInstallation SaveNew(string destination,Dictionary<string,object> document,IEnumerable<string> protectedPaths) {
   destination=GuiLaunchOptions.Absolute(destination,"new calibration profile");
   if(protectedPaths!=null)foreach(string path in protectedPaths)if(!string.IsNullOrEmpty(path)&&string.Equals(destination,Path.GetFullPath(path),StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("Save As requires a separate new file");
   byte[] bytes=CalibrationJson.Encode(document,65536);var profile=Parse(destination,bytes);
   using(var output=new FileStream(destination,FileMode.CreateNew,FileAccess.Write,FileShare.None)){output.Write(bytes,0,bytes.Length);output.Flush(true);}
   return profile;
  }
 }
}
