using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using MonakaBridge;
class CalibrationJobTests {
 static int cases;static string fixture,directory;
 static void Check(bool ok,string why){if(!ok)throw new Exception(why);}
 static string Line(CalibrationOwnedProcess child){var task=Task.Run(delegate{var bytes=new List<byte>();for(int i=0;i<512;i++){int b=child.Output.ReadByte();if(b<0)throw new EndOfStreamException();if(b=='\n'){if(bytes.Count>0&&bytes[bytes.Count-1]=='\r')bytes.RemoveAt(bytes.Count-1);return CalibrationJson.Utf8.GetString(bytes.ToArray());}bytes.Add((byte)b);}throw new Exception("fixture line bound");});if(!task.Wait(3000)){child.KillOwned();throw new Exception("fixture line timeout");}return task.Result;}
 static bool OriginalAlive(int id,long ticks){try{using(var p=Process.GetProcessById(id))return p.StartTime.ToUniversalTime().Ticks==ticks&&!p.HasExited;}catch(ArgumentException){return false;}}
 static void Exited(int id,long ticks){var timer=Stopwatch.StartNew();while(OriginalAlive(id,ticks)){Check(timer.ElapsedMilliseconds<3000,"original owned child still alive");Thread.Sleep(10);}}
 sealed class DeniedJob:ICalibrationProcessJob {
  internal int Id,Disposed;internal long Ticks;
  public void Assign(Process p){Id=p.Id;Ticks=p.StartTime.ToUniversalTime().Ticks;throw new Win32Exception(5,"test-only assignment denied");}
  public void Dispose(){Disposed++;}
 }
 static int Main(string[] args){try{
  Check(args.Length==2,"fixture and evidence directory required");fixture=Path.GetFullPath(args[0]);directory=Path.GetFullPath(args[1]);Directory.CreateDirectory(directory);
  using(var child=CalibrationOwnedProcess.Start(fixture,"--busy",false,null,null)){
   Check(child.AssignedBeforeCommands,"real job not assigned");Check(Line(child)=="READY","owned child framing mismatch");int id=child.Id;long ticks=child.StartUtc.Ticks;child.KillOwned();Check(child.WaitForExit(2000),"own job did not stop child");Exited(id,ticks);cases++;
  }
  string marker=Path.Combine(directory,"must-not-start.fixture");bool rejected=false;
  try{using(var unused=CalibrationOwnedProcess.Start(fixture,"--mark-start "+GuiLaunchOptions.Quote(marker),false,null,delegate{throw new Win32Exception(5,"test-only job creation denied");})){} }
  catch(Win32Exception){rejected=true;}Check(rejected&&!File.Exists(marker),"failed job creation launched a child");cases++;
  var deny=new DeniedJob();rejected=false;
  try{using(var unused=CalibrationOwnedProcess.Start(fixture,"--busy",false,null,delegate{return deny;})){} }
  catch(Win32Exception){rejected=true;}Check(rejected&&deny.Disposed==1,"failed assignment was ignored");Exited(deny.Id,deny.Ticks);cases++;
  foreach(bool withJob in new[]{false,true}){
   // Keep the outer job alive while checking the descendant. Otherwise outer
   // cleanup could falsely make the parent-death positive control pass.
   using(var parent=CalibrationOwnedProcess.Start(fixture,(withJob?"--parent-job ":"--parent-no-job ")+GuiLaunchOptions.Quote(fixture),false,null,null)){
    var record=CalibrationJson.Object(CalibrationJson.Utf8.GetBytes(Line(parent)),512,128);int id=(int)CalibrationJson.Integer(record,"pid",1,Int32.MaxValue);long ticks=CalibrationJson.Integer(record,"ticks",1,long.MaxValue);
    Check(parent.WaitForExit(3000)&&parent.ExitCode==73,"owned parent hard exit failed");
    if(withJob)Exited(id,ticks);else{Thread.Sleep(100);Check(OriginalAlive(id,ticks),"no-inner-job negative control did not stay alive");}
    File.WriteAllText(Path.Combine(directory,withJob?"parent-job.json":"parent-no-job.json"),"{\"pid\":"+id+",\"ticks\":"+ticks+",\"expected_alive_with_outer_job_open\":"+(withJob?"false":"true")+"}");cases++;
   }
  }
  Console.WriteLine("PASS calibration real Windows job "+cases+" cases; held owned processes only; parent-death negative control; no GUI/SDK/socket/config");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
