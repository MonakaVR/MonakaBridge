using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using PcServiceLifecycle;
using MonakaBridge;

internal static class InstallationOwnerTests {
 static int passed;static readonly JavaScriptSerializer json=new JavaScriptSerializer();
 static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
 static void Reject(Action action,string name,string reason=null){try{action();}catch(Exception e){if(reason!=null)Check(e.Message.Contains(reason),"Wrong rejection for "+name+": "+e.Message);Console.WriteLine("REJECT "+name+": "+e.Message);return;}throw new Exception("Accepted "+name);}
 static int Port(){using(var s=new UdpClient(new IPEndPoint(IPAddress.Loopback,0)))return ((IPEndPoint)s.Client.LocalEndPoint).Port;}
 static bool Free(int port){try{using(var s=new Socket(AddressFamily.InterNetwork,SocketType.Dgram,ProtocolType.Udp)){s.ExclusiveAddressUse=true;s.Bind(new IPEndPoint(IPAddress.Loopback,port));return true;}}catch(SocketException){return false;}}
 static void Pass(string name){passed++;Console.WriteLine("PASS "+name);}
 static string Args(string config,int port){return GuiLaunchOptions.Quote(config)+" "+port+" "+Port()+" "+Port()+" "+Port();}
 static int Main(string[] args){try{
  string service=Path.GetFullPath(args[0]),folder=Path.GetFullPath(args[1]),example=Path.GetFullPath(args[2]);Directory.CreateDirectory(folder);
  string config=Path.Combine(folder,"first.json"),other=Path.Combine(folder,"other.json");
  var document=json.Deserialize<Dictionary<string,object>>(File.ReadAllText(example));document["policy"]="disabled";document["bridge_id"]="owner-test-only";File.WriteAllText(config,json.Serialize(document));File.WriteAllText(other,json.Serialize(document));
  int firstPort=Port(),secondPort=Port();while(secondPort==firstPort)secondPort=Port();
  string firstMap=@"Local\MonakaBridge_ServiceLifecycle_"+firstPort,secondMap=@"Local\MonakaBridge_ServiceLifecycle_"+secondPort;
  var options=GuiLaunchOptions.Parse(new[]{"--config",config,"--allow-write"},folder);
  string firstArgs=Args(config,firstPort),secondArgs=Args(other,secondPort);
  using(var first=new Control(firstMap,service,Path.Combine(folder,"first-logs"),true))
  using(var second=new Control(secondMap,service,Path.Combine(folder,"second-logs"),true)){
   try{
    using(var channel=new BridgeOwnerChannel(options,delegate(string operation){return first.Command(operation,firstArgs,5000);})){
     var roundtrip=GuiLaunchOptions.Parse(new[]{"--config",config,"--allow-write","--owner-pipe",options.OwnerPipe,"--owner-identity",options.OwnerIdentity},folder);
     Check(roundtrip.ServiceCommand("Start").StartsWith("RUNNING"),"No start confirmation");uint pid=Control.Read(firstMap).Pid;
     Check(!Free(firstPort),"Native ingress not owned");Check(roundtrip.ServiceCommand("Start").StartsWith("RUNNING")&&Control.Read(firstMap).Pid==pid,"Start duplicated native process");Pass("actual native service ready, IPC client and idempotent start");
     second.EnsureRunning(secondArgs);uint peer=Control.Read(secondMap).Pid;Check(peer!=pid&&!Free(secondPort),"second installation not independently bound");Pass("same executable two isolated installation owners");
     Check(roundtrip.ServiceCommand("Stop").StartsWith("STOPPED"),"No stop confirmation");Check(Control.Read(firstMap)==null&&Free(firstPort),"Stopped owner retained port or native mapping");Check(Control.Read(secondMap).Pid==peer&&!Free(secondPort),"peer installation affected");Pass("stop confirms process, port and native mapping release; peer remains");
     Control.VerifyExistingTray(@"Local\MonakaBridge_Controller_"+BridgeOwnerChannel.Scope(config,""),System.Diagnostics.Process.GetCurrentProcess().MainModule.FileName,System.Diagnostics.Process.GetCurrentProcess().MainModule.FileName);Pass("tray identity remains verifiable while service stopped");
     Check(roundtrip.ServiceCommand("Start").StartsWith("RUNNING"),"Cannot start after confirmed stop");pid=Control.Read(firstMap).Pid;
     Check(roundtrip.ServiceCommand("Restart").StartsWith("RUNNING")&&Control.Read(firstMap).Pid!=pid,"Restart not confirmed with replacement process");Pass("explicit start after stop and restart preserve owner");
     var wrong=GuiLaunchOptions.Parse(new[]{"--config",other,"--allow-write","--owner-pipe",options.OwnerPipe,"--owner-identity",options.OwnerIdentity},folder);Reject(delegate{wrong.ServiceCommand("Stop");},"foreign configuration");Check(Control.Read(firstMap)!=null,"foreign target stopped service");
     wrong.OwnerIdentity=options.OwnerIdentity.Split(':')[0]+":1";Reject(delegate{wrong.ServiceCommand("Stop");},"reused PID with wrong creation time");Pass("foreign config and stale owner identity fail closed");
     var readOnly=GuiLaunchOptions.Parse(new[]{"--config",config,"--read-only","--owner-pipe",options.OwnerPipe,"--owner-identity",options.OwnerIdentity},folder);Reject(delegate{readOnly.ServiceCommand("Stop");},"read-only control");
     Reject(delegate{using(var duplicate=new BridgeOwnerChannel(roundtrip,delegate(string op){return "bad";})){}},"duplicate installation owner");Pass("read-only and duplicate owner refused");
     first.StopAndWait(5000);Reject(delegate{roundtrip.ServiceCommand("Start");},"start after exit fence","Controller exit has started");Check(Free(firstPort),"fenced owner rebound port");Pass("exit fence rejects delayed GUI start");
    }
    Check(Control.Read(@"Local\MonakaBridge_Controller_"+BridgeOwnerChannel.Scope(config,""))==null,"tray owner mapping leaked");Pass("owner channel and identity disposed after confirmed exit");
   }finally{first.StopAndWait(5000);second.StopAndWait(5000);}
   Check(Free(firstPort)&&Free(secondPort),"owned UDP remained");
  }
  Console.WriteLine("RESULT "+passed+" PASS; real Windows process/IPC/UDP, actual native service, no hardware");return 0;
 }catch(Exception e){Console.WriteLine(e);return 1;}}
}
