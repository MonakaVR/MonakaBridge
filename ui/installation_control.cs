using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

namespace MonakaBridge {
 // The existing tray remains the only runtime owner. This versioned local
 // channel conveys explicit GUI commands; it is not an input protocol.
 internal sealed class BridgeOwnerChannel : IDisposable {
  readonly object gate=new object();readonly string pipeName,configIdentity;
  readonly Func<string,string> command;readonly Thread worker;
  readonly System.IO.MemoryMappedFiles.MemoryMappedFile identityMap;
  System.IO.Pipes.NamedPipeServerStream active;bool disposed;
  [System.Runtime.InteropServices.DllImport("kernel32.dll",SetLastError=true)]
  static extern bool GetNamedPipeServerProcessId(Microsoft.Win32.SafeHandles.SafePipeHandle pipe,out uint pid);
  internal static string Scope(string config,string ingress){
   // Same configuration cannot acquire a second owner by changing endpoints.
   using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(Path.GetFullPath(config).ToUpperInvariant()))).Replace("-","").ToLowerInvariant();
  }
  internal static void Validate(string pipe,string identity){
   const string prefix="MonakaBridgeOwnerV1_";Guid nonce;if(!pipe.StartsWith(prefix,StringComparison.Ordinal)||!Guid.TryParseExact(pipe.Substring(prefix.Length),"N",out nonce))throw new ArgumentException("Invalid owner channel");
   var parts=identity.Split(':');int pid;long created;
   if(parts.Length!=2||!int.TryParse(parts[0],out pid)||pid<=0||!long.TryParse(parts[1],out created)||created<=0)throw new ArgumentException("Invalid owner identity");
  }
  internal BridgeOwnerChannel(GuiLaunchOptions options,Func<string,string> action){
   pipeName="MonakaBridgeOwnerV1_"+Guid.NewGuid().ToString("N");configIdentity=Scope(options.ConfigFile,"");command=action;
   using(var self=Process.GetCurrentProcess()){
    long created=self.StartTime.ToUniversalTime().ToFileTimeUtc();
    identityMap=System.IO.MemoryMappedFiles.MemoryMappedFile.CreateNew(@"Local\MonakaBridge_Controller_"+configIdentity,48);
    using(var view=identityMap.CreateViewAccessor()){
     view.Write(0,0x314c4350u);view.Write(4,1u);view.Write(8,1L);view.Write(16,(uint)self.Id);view.Write(20,(uint)self.Id);view.Write(24,created);view.Write(32,created);view.Write(40,1u);Thread.MemoryBarrier();view.Write(8,2L);
    }
    options.OwnerPipe=pipeName;options.OwnerIdentity=self.Id+":"+created;
   }
   worker=new Thread(Serve){IsBackground=true,Name="Bridge owned GUI requests"};worker.Start();
  }
  void Serve(){
   while(true){
    System.IO.Pipes.NamedPipeServerStream pipe;
    lock(gate){if(disposed)return;pipe=new System.IO.Pipes.NamedPipeServerStream(pipeName,System.IO.Pipes.PipeDirection.InOut,1,System.IO.Pipes.PipeTransmissionMode.Byte,System.IO.Pipes.PipeOptions.Asynchronous);active=pipe;}
    try{pipe.WaitForConnection();using(var timeout=new Timer(delegate{try{pipe.Dispose();}catch{}},null,15000,Timeout.Infinite)){
     byte[] request=new byte[66];int got=0;while(got<request.Length){int n=pipe.Read(request,got,request.Length-got);if(n==0)throw new EndOfStreamException();got+=n;}
     string result;
     try{
      if(request[0]!=1||Encoding.ASCII.GetString(request,2,64)!=configIdentity)throw new IOException("Owner target/version mismatch");
      string operation=request[1]==1?"Start":request[1]==2?"Stop":request[1]==3?"Restart":null;
      if(operation==null)throw new IOException("Unknown owner command");
      result="OK "+command(operation);
     }catch(Exception e){result="UNCONFIRMED "+e.Message;}
     byte[] response=Encoding.UTF8.GetBytes(result);if(response.Length>4096)response=Encoding.UTF8.GetBytes("UNCONFIRMED Owner diagnostic exceeds bound");
     byte[] length=BitConverter.GetBytes(response.Length);pipe.Write(length,0,4);pipe.Write(response,0,response.Length);pipe.Flush();
    }}catch(IOException){}catch(ObjectDisposedException){}
    finally{lock(gate){if(object.ReferenceEquals(active,pipe))active=null;}pipe.Dispose();}
   }
  }
  internal static string Request(GuiLaunchOptions options,string operation){
   Validate(options.OwnerPipe,options.OwnerIdentity);var identity=options.OwnerIdentity.Split(':');
   using(var owner=Process.GetProcessById(int.Parse(identity[0])))
   using(var self=Process.GetCurrentProcess())
   using(var pipe=new System.IO.Pipes.NamedPipeClientStream(".",options.OwnerPipe,System.IO.Pipes.PipeDirection.InOut,System.IO.Pipes.PipeOptions.Asynchronous)){
    if(owner.Handle==IntPtr.Zero||owner.StartTime.ToUniversalTime().ToFileTimeUtc()!=long.Parse(identity[1])||owner.HasExited||
      !string.Equals(owner.MainModule.FileName,self.MainModule.FileName,StringComparison.OrdinalIgnoreCase)||PcServiceLifecycle.Control.Hash(owner.MainModule.FileName)!=PcServiceLifecycle.Control.Hash(self.MainModule.FileName))throw new IOException("Owning tray process identity changed");
    pipe.Connect(2000);uint server;
    if(!GetNamedPipeServerProcessId(pipe.SafePipeHandle,out server)||server!=(uint)owner.Id)throw new IOException("Owner pipe belongs to another process");
    using(var timeout=new Timer(delegate{try{pipe.Dispose();}catch{}},null,12000,Timeout.Infinite)){
     byte opcode=operation=="Start"?(byte)1:operation=="Stop"?(byte)2:operation=="Restart"?(byte)3:(byte)0;
     if(opcode==0)throw new ArgumentException("Unsupported service command");
     byte[] request=new byte[66];request[0]=1;request[1]=opcode;Encoding.ASCII.GetBytes(Scope(options.ConfigFile,"")).CopyTo(request,2);pipe.Write(request,0,request.Length);pipe.Flush();
     var reader=new BinaryReader(pipe,Encoding.UTF8);int length=reader.ReadInt32();if(length<1||length>4096)throw new IOException("Owner response length invalid");
     byte[] response=reader.ReadBytes(length);if(response.Length!=length)throw new IOException("Owner response incomplete; outcome unknown");
     string result=Encoding.UTF8.GetString(response);if(!result.StartsWith("OK ",StringComparison.Ordinal))throw new IOException(result);return result.Substring(3);
    }
   }
  }
  public void Dispose(){lock(gate){if(disposed)return;disposed=true;if(active!=null)active.Dispose();}if(!worker.Join(16000))throw new IOException("Owner channel shutdown unconfirmed");identityMap.Dispose();}
 }
 // Local GUI control-plane configuration, never a tracking/wire contract.
 internal sealed class GuiLaunchOptions {
  internal readonly string Root,ConfigFile,HealthFile,ConfigHelper,HelperSha256,InstallationName;
  internal readonly bool ReadOnly,ControlWindowOnly;
  internal readonly CalibrationInstallation Calibration;
  internal string OwnerPipe,OwnerIdentity;
  GuiLaunchOptions(string root,string config,string health,string helper,string hash,string name,bool readOnly,bool window,CalibrationInstallation calibration=null){
   Root=root;ConfigFile=config;HealthFile=health;ConfigHelper=helper;HelperSha256=hash;InstallationName=name;ReadOnly=readOnly;ControlWindowOnly=window;Calibration=calibration;
  }
  internal static GuiLaunchOptions FromRoot(string root){return Parse(new[]{root},root);}
  internal static string Absolute(string value,string option){
   if(string.IsNullOrWhiteSpace(value)||value.IndexOf('"')>=0||value.IndexOf('\0')>=0||!Path.IsPathRooted(value)||
     !(value.StartsWith(@"\\",StringComparison.Ordinal)||(value.Length>2&&char.IsLetter(value[0])&&value[1]==':'&&(value[2]=='\\'||value[2]=='/'))))
    throw new ArgumentException(option+" requires an absolute path.");
   return Path.GetFullPath(value);
  }
  static string ProfilePath(Dictionary<string,object> profile,string key,string directory){
   object raw;if(!profile.TryGetValue(key,out raw)||!(raw is string)||string.IsNullOrWhiteSpace((string)raw))throw new ArgumentException("Installation requires "+key);
   string value=(string)raw;
   return Absolute(Path.IsPathRooted(value)?value:Path.Combine(directory,value),key);
  }
  internal static GuiLaunchOptions Parse(string[] args,string defaultRoot){
   string root=null,config=null,health=null,helper=null,hash=null,installation=null,name=null,profileHash=null,ownerPipe=null,ownerIdentity=null;
   bool readOnly=false,allowWrite=false,window=false,profileReadOnly=true;
   var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   for(int i=0;args!=null&&i<args.Length;i++){
    string arg=args[i];if(string.IsNullOrWhiteSpace(arg))throw new ArgumentException("Empty GUI argument");
    if(!arg.StartsWith("--",StringComparison.Ordinal)){if(root!=null)throw new ArgumentException("Only one root is allowed");root=Path.GetFullPath(arg);continue;}
    if(!seen.Add(arg))throw new ArgumentException("Duplicate option: "+arg);
    switch(arg.ToLowerInvariant()){
     case "--read-only":readOnly=true;break;
     case "--allow-write":allowWrite=true;break;
     case "--control-window-only":window=true;break;
     case "--owner-pipe":case "--owner-identity":case "--installation":case "--config":case "--health":case "--config-helper":case "--helper-sha256":case "--installation-name":case "--profile-sha256":
      if(++i>=args.Length||string.IsNullOrWhiteSpace(args[i])||args[i].StartsWith("--",StringComparison.Ordinal))throw new ArgumentException("Missing value for "+arg);
      switch(arg.ToLowerInvariant()){
       case "--owner-pipe":ownerPipe=args[i];break;
       case "--owner-identity":ownerIdentity=args[i];break;
       case "--installation":installation=Absolute(args[i],arg);break;
       case "--config":config=Absolute(args[i],arg);break;
       case "--health":health=Absolute(args[i],arg);break;
       case "--config-helper":helper=Absolute(args[i],arg);break;
       case "--helper-sha256":hash=args[i];break;
       case "--installation-name":name=args[i];break;
       case "--profile-sha256":profileHash=args[i];break;
      }break;
     default:throw new ArgumentException("Unknown GUI option: "+arg);
    }
   }
   if((ownerPipe==null)!=(ownerIdentity==null))throw new ArgumentException("Incomplete owner endpoint");
   if(ownerPipe!=null)BridgeOwnerChannel.Validate(ownerPipe,ownerIdentity);
   if(readOnly&&allowWrite)throw new ArgumentException("Read-only and allow-write conflict");
   bool explicitPaths=config!=null||health!=null||helper!=null||hash!=null;
   if(profileHash!=null&&(installation==null||profileHash.Length!=64))throw new ArgumentException("Profile snapshot hash requires an installation and64 hexadecimal characters");
   if(profileHash!=null)foreach(char c in profileHash)if(!Uri.IsHexDigit(c))throw new ArgumentException("Invalid profile snapshot SHA256");
   if(installation!=null){
    if(explicitPaths||name!=null)throw new ArgumentException("Installation profile cannot be combined with path/name overrides");
    var info=new FileInfo(installation);if(!info.Exists||info.Length>65536)throw new ArgumentException("Installation profile missing or too large");
    byte[] profileBytes=CalibrationInstallation.ReadBytes(installation);
    string legacyText;using(var reader=new StreamReader(new MemoryStream(profileBytes),Encoding.UTF8,true))legacyText=reader.ReadToEnd();
    var p=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(legacyText);
    object schema;
    if(p.TryGetValue("schema",out schema)&&object.Equals(schema,CalibrationInstallation.Schema)){
     var selected=CalibrationInstallation.Parse(installation,profileBytes);
     if(profileHash!=null&&!string.Equals(profileHash,selected.ProfileSha256,StringComparison.OrdinalIgnoreCase))throw new ArgumentException("Selected profile changed since launch; reopen explicitly");
     bool effectiveReadonly=readOnly||selected.ReadOnly;
     return new GuiLaunchOptions(Path.GetFullPath(root??Path.GetDirectoryName(installation)),selected.ConfigFile,selected.HealthFile,selected.ConfigHelper,selected.ConfigHelperSha256,selected.Name,effectiveReadonly,window,selected){OwnerPipe=ownerPipe,OwnerIdentity=ownerIdentity};
    }
    if(profileHash!=null)throw new ArgumentException("Profile snapshot requires the selected v2 schema; no fallback");
    var keys=new HashSet<string>(new[]{"schema","name","config","health","config_helper","helper_sha256","read_only"});
    foreach(string key in p.Keys)if(!keys.Contains(key))throw new ArgumentException("Unknown installation field: "+key);
    if(!p.ContainsKey("schema")||!object.Equals(p["schema"],"monaka.bridge.installation.v1"))throw new ArgumentException("Unsupported installation profile schema");
    if(!p.ContainsKey("name")||!(p["name"] is string)||string.IsNullOrWhiteSpace((string)p["name"]))throw new ArgumentException("Installation name required");
    name=(string)p["name"];string dir=Path.GetDirectoryName(installation);
    config=ProfilePath(p,"config",dir);health=ProfilePath(p,"health",dir);helper=ProfilePath(p,"config_helper",dir);
    if(p.ContainsKey("helper_sha256")){if(!(p["helper_sha256"] is string))throw new ArgumentException("Helper hash must be a string");hash=(string)p["helper_sha256"];}
    if(p.ContainsKey("read_only")){if(!(p["read_only"] is bool))throw new ArgumentException("read_only must be boolean");profileReadOnly=(bool)p["read_only"];}
    root=root??dir;
   }
   root=Path.GetFullPath(root??defaultRoot??AppDomain.CurrentDomain.BaseDirectory);
   config=config??Path.Combine(root,"config","bridge.json");health=health??config+".status.json";helper=helper??Path.Combine(root,"build","Release","monaka_bridge_config.exe");
   var distinct=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach(string value in new[]{config,health,helper})if(!distinct.Add(Path.GetFullPath(value)))throw new ArgumentException("Config, health and helper paths must be distinct");
   if(hash!=null){if(hash.Length!=64)throw new ArgumentException("Helper SHA256 must have64 hexadecimal characters");foreach(char c in hash)if(!Uri.IsHexDigit(c))throw new ArgumentException("Invalid helper SHA256");hash=hash.ToLowerInvariant();}
   if(name!=null&&(name.Length>128||name.IndexOfAny(new[]{'\r','\n','\0'})>=0))throw new ArgumentException("Invalid installation name");
   bool ro=readOnly||(!allowWrite&&(installation!=null?profileReadOnly:explicitPaths));
   return new GuiLaunchOptions(root,config,health,helper,hash,name??"Legacy root target",ro,window){OwnerPipe=ownerPipe,OwnerIdentity=ownerIdentity};
  }
  internal static string Quote(string value){
   if(value==null||value.IndexOf('\0')>=0)throw new ArgumentException("Invalid command argument");
   var b=new StringBuilder("\"");int slashes=0;
   foreach(char c in value){if(c=='\\'){slashes++;continue;}if(c=='"'){b.Append('\\',slashes*2+1);b.Append(c);}else{b.Append('\\',slashes);b.Append(c);}slashes=0;}
   b.Append('\\',slashes*2);return b.Append('"').ToString();
  }
  internal string OwnerArguments(){return OwnerPipe==null?"":" --owner-pipe "+Quote(OwnerPipe)+" --owner-identity "+Quote(OwnerIdentity);}
  internal string WindowArguments(){return WindowTargetArguments()+OwnerArguments();}
  string WindowTargetArguments(){if(Calibration!=null)return Quote(Root)+" --control-window-only --installation "+Quote(Calibration.ProfilePath)+" --profile-sha256 "+Calibration.ProfileSha256+(ReadOnly?" --read-only":" --allow-write");return Quote(Root)+" --control-window-only --installation-name "+Quote(InstallationName)+" --config "+Quote(ConfigFile)+" --health "+Quote(HealthFile)+" --config-helper "+Quote(ConfigHelper)+(HelperSha256==null?"":" --helper-sha256 "+HelperSha256)+(ReadOnly?" --read-only":" --allow-write");}
  internal string EditBlockReason(){
   if(ReadOnly)return "Read-only installation: editing is disabled";
   if(!File.Exists(ConfigFile))return "Configuration unavailable: "+ConfigFile;
   try{
    if((File.GetAttributes(ConfigFile)&FileAttributes.ReadOnly)!=0)return "Configuration is read-only: "+ConfigFile;
    using(var file=new FileStream(ConfigFile,FileMode.Open,FileAccess.ReadWrite,FileShare.ReadWrite|FileShare.Delete)){}
   }catch(Exception e){return "Configuration permission/access unavailable: "+e.Message;}
   if(!File.Exists(ConfigHelper))return "Config helper unavailable: "+ConfigHelper;
   try{VerifyHelper();}catch(Exception e){return e.Message;}
   return null;
  }
  internal void RequireEdit(){string reason=EditBlockReason();if(reason!=null)throw new InvalidOperationException(reason);}
  internal void VerifyHelper(){
   if(!File.Exists(ConfigHelper))throw new InvalidOperationException("Config helper unavailable: "+ConfigHelper);
   if(HelperSha256!=null){using(var input=File.OpenRead(ConfigHelper))using(var sha=SHA256.Create()){
    string actual=BitConverter.ToString(sha.ComputeHash(input)).Replace("-","").ToLowerInvariant();
    if(actual!=HelperSha256)throw new InvalidOperationException("Config helper SHA256 mismatch");
   }}
  }
  internal string ValidateEditing(){
   string reason=EditBlockReason();if(reason!=null)return reason;
   try{new ConfigHelperClient(this).CheckCompatibility();return null;}catch(Exception e){return e.Message;}
  }
  internal string ServiceBlockReason(){return ReadOnly?"Read-only target: runtime controls disabled":OwnerPipe==null?"Unsupported: no verified owning tray endpoint; open through the selected tray for lifecycle controls":null;}
  internal void RequireServiceControl(){string reason=ServiceBlockReason();if(reason!=null)throw new InvalidOperationException(reason);}
  internal string ServiceCommand(string operation){RequireServiceControl();return BridgeOwnerChannel.Request(this,operation);}
 }

 internal sealed class ConfigHelperClient {
  readonly GuiLaunchOptions options;
  internal ConfigHelperClient(GuiLaunchOptions target){options=target;}
  internal string Run(string config,string arguments,int timeoutMs){
   options.VerifyHelper();
   var start=new ProcessStartInfo(options.ConfigHelper,GuiLaunchOptions.Quote(config)+" "+arguments){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
   var output=new StringBuilder();var error=new StringBuilder();
   using(var process=new Process{StartInfo=start}){
    process.OutputDataReceived+=delegate(object s,DataReceivedEventArgs e){if(e.Data!=null)lock(output){if(output.Length<8192)output.AppendLine(e.Data.Substring(0,Math.Min(e.Data.Length,8192-output.Length)));}};
    process.ErrorDataReceived+=delegate(object s,DataReceivedEventArgs e){if(e.Data!=null)lock(error){if(error.Length<8192)error.AppendLine(e.Data.Substring(0,Math.Min(e.Data.Length,8192-error.Length)));}};
    process.Start();process.BeginOutputReadLine();process.BeginErrorReadLine();
    if(!process.WaitForExit(timeoutMs)){
     int ownedId=process.Id;bool stopped=false;
     try{process.Kill();stopped=process.WaitForExit(2000);}catch(InvalidOperationException){stopped=true;}
     throw new InvalidOperationException("Helper outcome unconfirmed (timeout), owned child PID "+ownedId+(stopped?" terminated/exited":" may still be running")+". Reload and review before another save. No service was signalled.");
    }
    process.WaitForExit();
    if(process.ExitCode!=0)throw new InvalidOperationException("Config helper rejected operation (exit "+process.ExitCode+"): "+error.ToString().Trim());
    return output.ToString();
   }
  }
  internal void CheckCompatibility(){
   if(Run(options.ConfigFile,"transaction-version",5000).Trim()!="monaka-config-transaction-v1")throw new InvalidOperationException("Unsupported config helper transaction generation");
  }
  internal void Command(string arguments){options.RequireEdit();CheckCompatibility();Run(options.ConfigFile,arguments,15000);}
  internal void TransformAndCommit(byte[] expected,string arguments,bool migration,CancellationToken cancel){
   options.RequireEdit();cancel.ThrowIfCancellationRequested();
   string directory=Path.Combine(Path.GetTempPath(),"monaka-config-operation-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(directory);
   string working=Path.Combine(directory,"working.json"),candidate=migration?Path.Combine(directory,"candidate.json"):working;
   try{
    File.WriteAllBytes(working,expected);CheckCompatibility();cancel.ThrowIfCancellationRequested();
    Run(working,arguments+(migration?" "+GuiLaunchOptions.Quote(candidate):""),15000);cancel.ThrowIfCancellationRequested();
    Commit(expected,candidate,cancel);
   }finally{
    // Only files in this freshly created private directory belong to this operation.
    foreach(string file in Directory.GetFiles(directory))File.Delete(file);Directory.Delete(directory,false);
   }
  }
  internal void Commit(byte[] expected,string candidate,CancellationToken cancel){
   Commit(expected,candidate,cancel,null);
  }
  internal void Commit(byte[] expected,string candidate,CancellationToken cancel,Action beforeNativeCommit){
   options.RequireEdit();cancel.ThrowIfCancellationRequested();CheckCompatibility();cancel.ThrowIfCancellationRequested();
   Run(candidate,"validate",5000);cancel.ThrowIfCancellationRequested();
   // An older transaction-capable helper may ignore runtime_contract. Prove the
   // selected target extension is actually validated, without touching the target.
   var document=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(candidate,Encoding.UTF8));
   var mappings=document["mappings"] as IList;
   if(mappings!=null)foreach(Dictionary<string,object> mapping in mappings)if(mapping.ContainsKey("runtime_contract")){
    var probe=MappingEditor.Clone(document);foreach(Dictionary<string,object> row in (IList)probe["mappings"])if(row.ContainsKey("runtime_contract"))((Dictionary<string,object>)row["runtime_contract"])["input_declaration"]=new[]{"ws01a.unsupported.channel"};
    string path=candidate+".contract-probe";
    try{File.WriteAllText(path,new JavaScriptSerializer().Serialize(probe),new UTF8Encoding(false));bool rejected=false;
     try{Run(path,"validate",5000);}catch(InvalidOperationException e){if(e.Message.Contains("rejected operation"))rejected=true;else throw;}
     if(!rejected)throw new InvalidOperationException("Config helper ignores runtime_contract; select the matching toolset");
    }finally{if(File.Exists(path))File.Delete(path);}break;
   }
   cancel.ThrowIfCancellationRequested();
   string snapshot=candidate+".expected";
   try{File.WriteAllBytes(snapshot,expected);cancel.ThrowIfCancellationRequested();
    // Cancellation is checked before commit. Once native commit starts, its
    // actual result decides SAVED/REJECTED/UNKNOWN; no rollback or fake cancel.
    if(beforeNativeCommit!=null)beforeNativeCommit();cancel.ThrowIfCancellationRequested();
    Run(options.ConfigFile,"commit "+GuiLaunchOptions.Quote(snapshot)+" "+GuiLaunchOptions.Quote(candidate),15000);
   }finally{if(File.Exists(snapshot))File.Delete(snapshot);}
  }
 }
}
