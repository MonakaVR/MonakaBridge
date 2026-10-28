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
  var selected=options.RetainMatchingOwner(GuiLaunchOptions.Parse(new[]{"--installation",Path.GetFullPath(args[2])},null));Check(selected.OwnerPipe==pipe&&selected.Calibration!=null,"Same installation lost native owner on profile selection");
  Check(options.ServiceCommand("Stop").StartsWith("STOPPED"),"Native Stop not confirmed");
  Check(options.ServiceCommand("Start").StartsWith("RUNNING"),"Native Start not confirmed");
  Check(options.ServiceCommand("Restart").StartsWith("RUNNING"),"Native Restart not confirmed");
  options.OwnerSha256=new string('0',64);bool refused=false;try{options.ServiceCommand("Stop");}catch(IOException e){refused=e.Message.Contains("identity changed");}Check(refused,"Wrong native owner hash accepted");
  Console.WriteLine("PASS actual WPF product owner client -> native parent pipe; Stop/Start/Restart and wrong build hash refusal");return 0;
 }catch(Exception e){Console.WriteLine(e);return 1;}}
}
