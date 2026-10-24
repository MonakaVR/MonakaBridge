using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace MonakaBridge {
 internal interface ICalibrationProcessJob:IDisposable {void Assign(Process process);}
 internal sealed class CalibrationWindowsJob:ICalibrationProcessJob {
  sealed class JobHandle:SafeHandleZeroOrMinusOneIsInvalid {
   public JobHandle():base(true){}
   protected override bool ReleaseHandle(){return CloseHandle(handle);}
  }
  [StructLayout(LayoutKind.Sequential)]struct BasicLimits {
   internal long ProcessUserTime,JobUserTime;internal uint Flags;
   internal UIntPtr MinimumWorkingSet,MaximumWorkingSet;internal uint ActiveProcesses;
   internal UIntPtr Affinity;internal uint PriorityClass,SchedulingClass;
  }
  [StructLayout(LayoutKind.Sequential)]struct IoCounters {internal ulong ReadOperations,WriteOperations,OtherOperations,ReadBytes,WriteBytes,OtherBytes;}
  [StructLayout(LayoutKind.Sequential)]struct ExtendedLimits {
   internal BasicLimits Basic;internal IoCounters Io;internal UIntPtr ProcessMemory,JobMemory,PeakProcessMemory,PeakJobMemory;
  }
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern JobHandle CreateJobObject(IntPtr attributes,string name);
  [DllImport("kernel32.dll",SetLastError=true)]static extern bool SetInformationJobObject(JobHandle job,int information,ref ExtendedLimits limits,uint length);
  [DllImport("kernel32.dll",SetLastError=true)]static extern bool AssignProcessToJobObject(JobHandle job,IntPtr process);
  [DllImport("kernel32.dll",SetLastError=true)]static extern bool CloseHandle(IntPtr handle);
  readonly JobHandle job;
  internal CalibrationWindowsJob() {
   job=CreateJobObject(IntPtr.Zero,null);if(job==null||job.IsInvalid){if(job!=null)job.Dispose();throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot create owned calibration job");}
   var limits=new ExtendedLimits();limits.Basic.Flags=0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
   if(!SetInformationJobObject(job,9,ref limits,(uint)Marshal.SizeOf(typeof(ExtendedLimits)))){int error=Marshal.GetLastWin32Error();job.Dispose();throw new Win32Exception(error,"Cannot set owned calibration job");}
  }
  public void Assign(Process process){if(!AssignProcessToJobObject(job,process.Handle))throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot assign owned calibration child");}
  public void Dispose(){job.Dispose();}
 }
 // One exact Process object/handle. No process-name, health PID, service, or
 // port-owner lookup is used to stop a worker. The client owns pipe threads.
 internal sealed class CalibrationOwnedProcess:IDisposable {
  readonly Process process;ICalibrationProcessJob job;readonly object gate=new object();bool disposed;
  internal readonly Stream Input,Output,Error;
  internal readonly bool AssignedBeforeCommands;
  CalibrationOwnedProcess(Process child,ICalibrationProcessJob owner,bool assigned) {
   process=child;job=owner;AssignedBeforeCommands=assigned;Input=child.StandardInput.BaseStream;Output=child.StandardOutput.BaseStream;Error=child.StandardError.BaseStream;
  }
  internal static CalibrationOwnedProcess Start(string executable,string arguments,bool resourceFreeVersion,IDictionary<string,string> environment,Func<ICalibrationProcessJob> createJob) {
   ICalibrationProcessJob owner=null;Process child=null;bool started=false,assigned=false;
   try{
    owner=createJob==null?(ICalibrationProcessJob)new CalibrationWindowsJob():createJob();if(owner==null)throw new InvalidOperationException("Owned job is required");
    var start=new ProcessStartInfo(GuiLaunchOptions.Absolute(executable,"calibration helper"),arguments){UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true,WorkingDirectory=Path.GetDirectoryName(executable)};
    if(environment!=null)foreach(var item in environment)start.EnvironmentVariables[item.Key]=item.Value;
    child=new Process{StartInfo=start};if(!child.Start())throw new InvalidOperationException("Calibration child did not start");started=true;
    try{owner.Assign(child);assigned=true;}
    catch(Win32Exception){
     // A resource-free --local-api-version may finish before assignment. It
     // cannot receive open/discover. A live/session child must be assigned.
     if(!resourceFreeVersion||!child.HasExited)throw;
    }
    if(!resourceFreeVersion&&!assigned)throw new InvalidOperationException("Unowned calibration session refused");
    return new CalibrationOwnedProcess(child,owner,assigned);
   }catch{
    try{
     if(started&&child!=null)try{if(!child.HasExited)child.Kill();child.WaitForExit(2000);}catch(InvalidOperationException){}
    }finally{if(child!=null)child.Dispose();if(owner!=null)owner.Dispose();}
    throw;
   }
  }
  internal int Id {get{return process.Id;}}
  internal DateTime StartUtc {get{return process.StartTime.ToUniversalTime();}}
  internal bool HasExited {get{try{return process.HasExited;}catch(InvalidOperationException){return true;}}}
  internal bool WaitForExit(int milliseconds){return process.WaitForExit(milliseconds);}
  internal int ExitCode {get{return process.ExitCode;}}
  internal void CloseInput(){try{Input.Dispose();}catch(IOException){}}
  internal void KillOwned() {
   ICalibrationProcessJob owner;
   lock(gate){owner=job;job=null;}
   Exception failure=null;
   try{if(owner!=null)owner.Dispose();}catch(Exception e){failure=e;}
   // Cleanup may terminate only this held Process. Capture/session operation
   // never proceeds after failed job assignment.
   try{if(!HasExited)process.Kill();}catch(InvalidOperationException){}catch(Exception e){if(failure==null)failure=e;}
   if(failure!=null)throw new InvalidOperationException("Owned calibration child termination unconfirmed",failure);
  }
  public void Dispose() {
   lock(gate){if(disposed)return;disposed=true;}
   try{KillOwned();}finally{
    try{Input.Dispose();}catch(IOException){}
    try{Output.Dispose();}catch(IOException){}
    try{Error.Dispose();}catch(IOException){}
    process.Dispose();
   }
  }
 }
}
