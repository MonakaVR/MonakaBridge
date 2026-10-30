using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;
using MonakaBridge;
using PcServiceLifecycle;
internal static class NativeOwnerClientTests {
 static void Check(bool ok,string reason){if(!ok)throw new Exception(reason);}
 static int Main(string[] args){try{
  string config=Path.GetFullPath(args[0]),owner=Path.GetFullPath(args[1]);string mapping=@"Local\MonakaBridge_Controller_"+BridgeOwnerChannel.Scope(config,"");
  var identity=Control.Read(mapping);Check(identity!=null&&identity.Ready,"Native owner not ready");string pipe;
  using(var map=MemoryMappedFile.OpenExisting(mapping,MemoryMappedFileRights.Read))using(var view=map.CreateViewAccessor(48,160,MemoryMappedFileAccess.Read)){byte[] bytes=new byte[160];view.ReadArray(0,bytes,0,bytes.Length);pipe=Encoding.Unicode.GetString(bytes).TrimEnd('\0');}
  var options=GuiLaunchOptions.Parse(new[]{"--installation",Path.GetFullPath(args[2]),"--allow-write","--owner-pipe",pipe,"--owner-identity",identity.Pid+":"+identity.Created,"--owner-executable",owner,"--owner-sha256",Control.Hash(owner)},Path.GetDirectoryName(config));
  Check(options.Calibration!=null,"Native v2 calibration profile missing");
  byte[] originalConfig=File.ReadAllBytes(config);
  var japanese=GuiLaunchOptions.Parse(new[]{"--installation",Path.GetFullPath(args[2]),"--language","ja"},null);Check(japanese.Language=="ja"&&japanese.WindowArguments().Contains("--language ja"),"Japanese presentation not forwarded");
  BridgeText.Language="ja";Check(BridgeText.T("Save mapping")=="\u5bfe\u5fdc\u3092\u4fdd\u5b58"&&BridgeText.T("Open session")=="\u30bb\u30c3\u30b7\u30e7\u30f3\u3092\u958b\u304f","Japanese control/calibration labels missing");Check(BridgeText.T("opaque-source-\u65e5\u672c\u8a9e")=="opaque-source-\u65e5\u672c\u8a9e","Opaque identity translated");BridgeText.Language="en";Check(BridgeText.T("Save mapping")=="Save mapping","English presentation changed");
  bool invalidLanguage=false;try{GuiLaunchOptions.Parse(new[]{"--config",config,"--language","unknown"},null);}catch(ArgumentException){invalidLanguage=true;}Check(invalidLanguage,"Unknown language accepted");Check(Convert.ToBase64String(originalConfig)==Convert.ToBase64String(File.ReadAllBytes(config)),"Presentation selection modified config");
  var selected=options.RetainMatchingOwner(GuiLaunchOptions.Parse(new[]{"--installation",Path.GetFullPath(args[2])},null));Check(selected.OwnerPipe==pipe&&selected.Calibration!=null,"Same installation lost native owner on profile selection");
  var readOnly=GuiLaunchOptions.Parse(new[]{"--installation",Path.GetFullPath(args[2]),"--read-only","--owner-pipe",pipe,"--owner-identity",identity.Pid+":"+identity.Created,"--owner-executable",owner,"--owner-sha256",Control.Hash(owner)},null);
  Check(readOnly.RetainMatchingOwner(options).ReadOnly,"Profile selection escalated readonly owner");
  bool foreign=false;try{options.RetainMatchingOwner(GuiLaunchOptions.Parse(new[]{"--config",config+".foreign","--read-only"},null));}catch(InvalidOperationException){foreign=true;}Check(foreign,"Native GUI accepted foreign installation selection");
  Check(options.ServiceCommand("Stop").StartsWith("STOPPED"),"Native Stop not confirmed");
  Check(BridgeOwnerChannel.Request(options,"Status").StartsWith("STOPPED"),"WPF IPC Stop left stale tray state");
  Check(options.ServiceCommand("Start").StartsWith("RUNNING"),"Native Start not confirmed");
  Check(options.ServiceCommand("Restart").StartsWith("RUNNING"),"Native Restart not confirmed");
  Check(BridgeOwnerChannel.Request(options,"Status").StartsWith("RUNNING"),"WPF IPC Restart omitted tray state");
  var bad=GuiLaunchOptions.Parse(new[]{"--config",config+".foreign","--allow-write","--owner-pipe",pipe,"--owner-identity",identity.Pid+":"+identity.Created,"--owner-executable",owner,"--owner-sha256",Control.Hash(owner)},null);
  bool mismatch=false;try{bad.ServiceCommand("Stop");}catch(IOException){mismatch=true;}Check(mismatch,"Foreign IPC target accepted");Check(BridgeOwnerChannel.Request(options,"Status").Contains("UNCONFIRMED"),"Failed IPC command omitted tray diagnostic");options.ServiceCommand("Start");
  options.OwnerSha256=new string('0',64);bool refused=false;try{options.ServiceCommand("Stop");}catch(IOException e){refused=e.Message.Contains("identity changed");}Check(refused,"Wrong native owner hash accepted");
  Console.WriteLine("PASS actual WPF product owner client -> native parent pipe; Stop/Start/Restart and wrong build hash refusal");return 0;
 }catch(Exception e){Console.WriteLine(e);return 1;}}
}
