using System;
using System.IO;
using System.Threading;
using PcServiceLifecycle;

internal static class LifecycleSerializationTests {
 static string fixture,folder;static int failures,passed;
 static string Quote(string value){return "\""+value+"\"";}
 static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
 static void Case(string name,Action body){try{body();++passed;Console.WriteLine("PASS "+name);}catch(Exception e){++failures;Console.WriteLine("FAIL "+name+": "+e.Message);}}
 static Control NewControl(out string map,out string marker) {
  string id=Guid.NewGuid().ToString("N");map=@"Local\MonakaSa01_"+id;marker=Path.Combine(folder,id+".started");
  return new Control(map,fixture,Path.Combine(folder,id));
 }
 static int Main(string[] args) {
  fixture=Path.GetFullPath(args[0]);folder=Path.GetFullPath(args[1]);Directory.CreateDirectory(folder);
  Case("normal owned start, idempotent start, confirmed stop",delegate {
   string map,marker;using(var c=NewControl(out map,out marker)) {
    try{c.EnsureRunning(Quote(map)+" "+Quote(marker));Check(File.Exists(marker),"fixture not started");string pid=File.ReadAllText(marker);
     c.EnsureRunning(Quote(map)+" "+Quote(marker));Check(pid==File.ReadAllText(marker),"duplicate child");
     c.StopAndWait(2000);Check(Control.Read(map)==null,"identity retained after exit");
    }finally{c.StopAndWait(2000);}
   }
  });
  Case("exit before queued startup fences launch",delegate {
   string map,marker;using(var c=NewControl(out map,out marker))using(var release=new ManualResetEvent(false)) {
    Exception observed=null;var startup=new Thread(delegate(){release.WaitOne();try{c.EnsureRunning(Quote(map)+" "+Quote(marker));}catch(Exception e){observed=e;}});
    startup.Start();c.StopAndWait(2000);release.Set();Check(startup.Join(7000),"startup did not complete");
    try{Check(observed is InvalidOperationException,"late startup was not refused");Check(!File.Exists(marker),"service launched after stop confirmation");}
    finally{c.StopAndWait(2000);}
   }
  });
  Case("dispose before queued startup fences launch",delegate {
   string map,marker;var c=NewControl(out map,out marker);c.Dispose();Exception observed=null;
   try{c.EnsureRunning(Quote(map)+" "+Quote(marker));}catch(Exception e){observed=e;}
   try{Check(observed is ObjectDisposedException,"disposed controller admitted startup");Check(!File.Exists(marker),"service launched after Dispose");}
   finally{if(File.Exists(marker)){c.StopAndWait(2000);c.Dispose();}}
  });
  Case("failed stop never triggers ExitGate callback",delegate {
   bool exited=false;string error;bool result=new ExitGate().TryExit(delegate{throw new IOException("unconfirmed");},delegate{exited=true;},out error);
   Check(!result&&!exited&&error=="unconfirmed","unconfirmed exit accepted");
  });
  Console.WriteLine("RESULT "+passed+" PASS / "+failures+" FAIL; owned synthetic children only; no sockets or hardware");
  return failures==0?0:1;
 }
}
