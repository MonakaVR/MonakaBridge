using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;
using PcServiceLifecycle;

internal static class ActiveConfigGuardTests {
    static int passed;
    static void Check(bool value,string message){if(!value)throw new Exception(message);}
    static void Reject(Action action,string message){
        try{action();}catch(IOException){Console.WriteLine("REJECT "+message);passed++;return;}
        throw new Exception("Accepted "+message);
    }
    static int Main(string[] args){try{
        if(args.Length!=1)throw new ArgumentException("Explicit isolated output directory required");
        string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);
        string config=Path.Combine(root,"active.json"),other=Path.Combine(root,"different.json");
        File.WriteAllText(config,"{}");File.WriteAllText(other,"{}");
        string name=@"Local\MonakaBridge_ActiveConfigTest_"+Guid.NewGuid().ToString("N");
        string eventName=name+"_Open";
        using(var process=Process.GetCurrentProcess())
        using(var signal=new EventWaitHandle(false,EventResetMode.AutoReset,eventName)) {
            var state=new State{OwnerPid=(uint)process.Id,OwnerCreated=process.StartTime.ToUniversalTime().ToFileTimeUtc()};
            Reject(delegate{ActiveConfigurationLease.VerifyAndSignal(name,config,state,eventName,true);},"legacy missing active config");
            Check(!signal.WaitOne(0),"missing metadata signalled reopen");
            using(var lease=new ActiveConfigurationLease(name,config)) {
                ActiveConfigurationLease.VerifyAndSignal(name,config,state,eventName,true);
                Check(signal.WaitOne(0),"same-config reopen was not signalled");passed++;
                ActiveConfigurationLease.VerifyAndSignal(name,Path.Combine(root,".","active.json"),state,eventName,true);
                Check(signal.WaitOne(0),"normalized same path refused");passed++;
                ActiveConfigurationLease.VerifyAndSignal(name,config,state,eventName,false);
                Check(!signal.WaitOne(0),"tray-only duplicate signalled a window");passed++;
                Reject(delegate{ActiveConfigurationLease.VerifyAndSignal(name,other,state,eventName,true);},"same bytes at different config path");
                Check(!signal.WaitOne(0),"foreign config signalled reopen");
                Reject(delegate{ActiveConfigurationLease.VerifyAndSignal(name,@"\\?\"+config,state,eventName,true);},"extended-path alias");
                Check(!signal.WaitOne(0),"path alias signalled reopen");
                string replacement=Path.Combine(root,"replacement.json");File.WriteAllText(replacement,"{\"changed\":true}");
                File.Replace(replacement,config,null);
                ActiveConfigurationLease.VerifyAndSignal(name,config,state,eventName,true);
                Check(signal.WaitOne(0),"normal atomic config replacement changed path ownership");passed++;
                var stale=new State{OwnerPid=state.OwnerPid,OwnerCreated=state.OwnerCreated-1};
                Reject(delegate{ActiveConfigurationLease.VerifyAndSignal(name,config,stale,eventName,true);},"PID reuse/wrong creation time");
                Check(!signal.WaitOne(0),"stale identity signalled reopen");
                Reject(delegate{using(var duplicate=new ActiveConfigurationLease(name,config)){}},"existing metadata is never overwritten");
            }
            Reject(delegate{ActiveConfigurationLease.VerifyAndSignal(name,config,state,eventName,true);},"disposed owner metadata");
            using(var map=MemoryMappedFile.CreateNew(name,64)) {
                Reject(delegate{ActiveConfigurationLease.VerifyAndSignal(name,config,state,eventName,true);},"incomplete metadata");
                Check(!signal.WaitOne(0),"incomplete metadata signalled reopen");
            }
        }
        Console.WriteLine("RESULT "+passed+" PASS; actual Windows config files, named mapping and reopen event; no product/hardware");
        return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
