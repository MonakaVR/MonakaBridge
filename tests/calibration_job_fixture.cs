using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using MonakaBridge;
class CalibrationJobFixture {
 static object retained;
 static void Ready(Stream output){byte[] expected=System.Text.Encoding.ASCII.GetBytes("READY"+Environment.NewLine);for(int i=0;i<expected.Length;i++)if(output.ReadByte()!=expected[i])throw new Exception("child ready mismatch");}
 static int Main(string[] args){try{
  if(args.Length==1&&args[0]=="--busy"){Console.WriteLine("READY");Console.Out.Flush();Thread.Sleep(Timeout.Infinite);return 0;}
  if(args.Length==2&&args[0]=="--mark-start"){File.WriteAllText(args[1],"test-only child started");Console.WriteLine("READY");Console.Out.Flush();Thread.Sleep(Timeout.Infinite);return 0;}
  if(args.Length==2&&(args[0]=="--parent-job"||args[0]=="--parent-no-job")){
   if(args[0]=="--parent-job"){
    // Deliberately no Dispose/finally: process death must close the real job.
    var child=CalibrationOwnedProcess.Start(args[1],"--busy",false,null,null);retained=child;Ready(child.Output);
    Console.WriteLine("{\"pid\":"+child.Id+",\"ticks\":"+child.StartUtc.Ticks+"}");Console.Out.Flush();Environment.Exit(73);
   }else{
    // Negative control remains within the outer test's still-open owned job.
    var child=Process.Start(new ProcessStartInfo(args[1],"--busy"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardInput=true,RedirectStandardError=true});retained=child;Ready(child.StandardOutput.BaseStream);
    Console.WriteLine("{\"pid\":"+child.Id+",\"ticks\":"+child.StartTime.ToUniversalTime().Ticks+"}");Console.Out.Flush();Environment.Exit(73);
   }
  }
  throw new Exception("test-only fixture mode required");
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
