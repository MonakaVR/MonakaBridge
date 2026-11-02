using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;
using System.Security.Cryptography;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace PcServiceLifecycle {
    // Local controller metadata, independent of tracking/wire/lifecycle ABIs.
    // An existing tray may be reopened only for its exact active config target.
    internal sealed class ActiveConfigurationLease : IDisposable {
        const uint Magic = 0x31464341; // ACF1
        MemoryMappedFile mapping;
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        static extern uint GetFinalPathNameByHandle(SafeFileHandle file, StringBuilder path, uint size, uint flags);
        static byte[] Identity(string config) {
            if(config.StartsWith(@"\\?\",StringComparison.Ordinal))throw new IOException("Active configuration path alias refused");
            string selected=Path.GetFullPath(config);
            using(var file=new FileStream(selected,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)) {
                var path=new StringBuilder(32768);
                uint length=GetFinalPathNameByHandle(file.SafeFileHandle,path,(uint)path.Capacity,0);
                if(length==0||length>=path.Capacity)throw new IOException("Active configuration canonical path unavailable");
                string canonical=path.ToString();
                if(canonical.StartsWith(@"\\?\UNC\",StringComparison.Ordinal))canonical=@"\\"+canonical.Substring(8);
                else if(canonical.StartsWith(@"\\?\",StringComparison.Ordinal))canonical=canonical.Substring(4);
                if(!string.Equals(selected,canonical,StringComparison.OrdinalIgnoreCase))
                    throw new IOException("Active configuration path alias refused");
                using(var sha=SHA256.Create())return sha.ComputeHash(Encoding.UTF8.GetBytes(canonical.ToUpperInvariant()));
            }
        }
        internal ActiveConfigurationLease(string name,string config) {
            byte[] identity=Identity(config);
            // CreateNew refuses stale/foreign retained mappings rather than overwriting them.
            mapping=MemoryMappedFile.CreateNew(name,64,MemoryMappedFileAccess.ReadWrite);
            try {
                using(var process=Process.GetCurrentProcess())
                using(var view=mapping.CreateViewAccessor(0,64,MemoryMappedFileAccess.ReadWrite)) {
                    view.Write(4,1u);view.Write(8,(uint)process.Id);
                    view.Write(16,process.StartTime.ToUniversalTime().ToFileTimeUtc());
                    view.WriteArray(24,identity,0,identity.Length);
                    Thread.MemoryBarrier();view.Write(0,Magic);
                }
            } catch {mapping.Dispose();mapping=null;throw;}
        }
        internal static void VerifyAndSignal(string name,string config,State state,string openEvent,bool openWindow) {
            byte[] expected=Identity(config);
            try {
                using(var map=MemoryMappedFile.OpenExisting(name,MemoryMappedFileRights.Read))
                using(var view=map.CreateViewAccessor(0,64,MemoryMappedFileAccess.Read)) {
                    if(view.ReadUInt32(0)!=Magic||view.ReadUInt32(4)!=1)throw new IOException("Active configuration metadata incomplete");
                    Thread.MemoryBarrier();
                    if(view.ReadUInt32(8)!=state.OwnerPid||view.ReadInt64(16)!=state.OwnerCreated)
                        throw new IOException("Active configuration owner identity changed");
                    byte[] actual=new byte[32];view.ReadArray(24,actual,0,actual.Length);
                    for(int i=0;i<actual.Length;i++)if(actual[i]!=expected[i])
                        throw new IOException("Existing tray active configuration differs; no reopen request sent");
                    using(var owner=Process.GetProcessById(checked((int)state.OwnerPid))) {
                        if(owner.HasExited||owner.StartTime.ToUniversalTime().ToFileTimeUtc()!=state.OwnerCreated)
                            throw new IOException("Active configuration owner is stale");
                    }
                    // Hold the verified immutable mapping until after the signal.
                    if(openWindow)using(var request=EventWaitHandle.OpenExisting(openEvent))request.Set();
                }
            } catch(FileNotFoundException) {throw new IOException("Existing tray active configuration is unavailable; no reopen request sent");}
        }
        public void Dispose(){if(mapping!=null){mapping.Dispose();mapping=null;}}
    }
    internal sealed class State {
        internal uint Pid, OwnerPid;
        internal long Created, OwnerCreated;
        internal bool Ready;
    }
    internal sealed class Control : IDisposable {
        readonly string name, executable, logs, executableSha;
        readonly bool scoped;
        readonly object lifetime = new object();
        bool stopRequested, disposed;
        Process child;
        StreamWriter stdout, stderr;
        internal Control(string mappingName, string serviceExecutable, string logDirectory) : this(mappingName,serviceExecutable,logDirectory,false) {}
        internal Control(string mappingName, string serviceExecutable, string logDirectory, bool installationScoped) {
            scoped=installationScoped;
            name=mappingName; executable=Path.GetFullPath(serviceExecutable); logs=logDirectory;
            executableSha=File.Exists(executable)?Hash(executable):null;
        }
        internal static string Hash(string path) {
            using(var stream=File.OpenRead(path))using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant();
        }
        internal static State VerifyExistingTray(string mappingName,string expectedService,string expectedController) {
            var state=Read(mappingName);
            if(state==null || !state.Ready)throw new IOException("Existing tray has no ready service identity");
            using(var control=new Control(mappingName,expectedService,Path.GetTempPath()))
            using(var service=control.Verified(state))
            using(var owner=Process.GetProcessById(checked((int)state.OwnerPid))) {
                if(owner.Handle==IntPtr.Zero || owner.StartTime.ToUniversalTime().ToFileTimeUtc()!=state.OwnerCreated ||
                   !string.Equals(Path.GetFullPath(owner.MainModule.FileName),Path.GetFullPath(expectedController),StringComparison.OrdinalIgnoreCase) ||
                   Hash(owner.MainModule.FileName)!=Hash(expectedController) || owner.HasExited || service.HasExited)
                    throw new IOException("Existing tray path/build differs; no reopen request sent");
            }
            return state;
        }
        internal static State Read(string name) {
            try {
                using(var map=MemoryMappedFile.OpenExisting(name,MemoryMappedFileRights.Read))
                using(var view=map.CreateViewAccessor(0,48,MemoryMappedFileAccess.Read)) {
                    long first=view.ReadInt64(8); Thread.MemoryBarrier();
                    if(view.ReadUInt32(0)!=0x314c4350 || view.ReadUInt32(4)!=1 || first==0 || (first&1)!=0)
                        throw new IOException("Service lifetime identity is incomplete");
                    var state=new State {Pid=view.ReadUInt32(16),OwnerPid=view.ReadUInt32(20),
                        Created=view.ReadInt64(24),OwnerCreated=view.ReadInt64(32),Ready=view.ReadUInt32(40)==1};
                    Thread.MemoryBarrier();
                    if(first!=view.ReadInt64(8))throw new IOException("Service lifetime identity changed");
                    return state;
                }
            } catch(FileNotFoundException) { return null; }
        }
        Process Verified(State state) {
            var process=Process.GetProcessById(checked((int)state.Pid));
            try {
                // Retain the handle before signalling: .NET Framework cannot
                // retrieve ExitCode from an already-gone GetProcessById target.
                if(process.Handle==IntPtr.Zero)throw new IOException("Service process handle unavailable");
                if(process.StartTime.ToUniversalTime().ToFileTimeUtc()!=state.Created ||
                    !string.Equals(Path.GetFullPath(process.MainModule.FileName),executable,StringComparison.OrdinalIgnoreCase) ||
                    executableSha==null || Hash(process.MainModule.FileName)!=executableSha)
                    throw new IOException("Service identity/path differs; no stop request sent");
                return process;
            } catch {process.Dispose();throw;}
        }
        static void Log(StreamWriter writer,string value) {
            if(writer==null || value==null)return;
            try {lock(writer) { if(writer.BaseStream.Length<1024*1024) { writer.WriteLine(value);writer.Flush(); } }}
            catch(IOException){}catch(ObjectDisposedException){}
        }
        bool AnyUnmanagedProcess() {
            var processes=Process.GetProcessesByName(Path.GetFileNameWithoutExtension(executable));
            try { return processes.Length!=0; }
            finally {foreach(var process in processes)process.Dispose();}
        }
        void RequireOwned(State state) {
            using(var current=Process.GetCurrentProcess())
                if(state.OwnerPid!=(uint)current.Id || state.OwnerCreated!=current.StartTime.ToUniversalTime().ToFileTimeUtc())
                    throw new IOException("Receiver belongs to another controller or a headless session; it will not be stopped or replaced");
        }
        internal void EnsureRunning(string additionalArguments) {
            lock(lifetime) {
                if(disposed)throw new ObjectDisposedException("Control");
                if(stopRequested)throw new InvalidOperationException("Controller shutdown has started; no service will be launched");
                EnsureRunningOwned(additionalArguments);
            }
        }
        void EnsureRunningOwned(string additionalArguments) {
            var existing=Read(name);
            if(existing!=null) {
                using(var process=Verified(existing)) {
                    RequireOwned(existing);
                    if(existing.Ready && !process.HasExited)return;
                }
                throw new IOException("Existing service is not ready");
            }
            if(!scoped && AnyUnmanagedProcess())throw new IOException("An existing/legacy service has no compatible lifetime controller. No duplicate was started");
            if(!File.Exists(executable))throw new FileNotFoundException("Selected service executable is missing",executable);
            if(child!=null) {
                if(!child.HasExited)throw new IOException("Owned child has not exited; no replacement launched");
                child.WaitForExit();child.Dispose();child=null;
            }
            if(stdout!=null){lock(stdout)stdout.Dispose();stdout=null;}
            if(stderr!=null){lock(stderr)stderr.Dispose();stderr=null;}
            Directory.CreateDirectory(logs);
            string stamp=DateTime.UtcNow.ToString("yyyyMMddTHHmmssfffffffZ");
            stdout=new StreamWriter(Path.Combine(logs,stamp+".stdout.log"));
            stderr=new StreamWriter(Path.Combine(logs,stamp+".stderr.log"));
            using(var current=Process.GetCurrentProcess()) {
                string owner=current.Id+":"+current.StartTime.ToUniversalTime().ToFileTimeUtc();
                child=new Process {StartInfo=new ProcessStartInfo(executable,additionalArguments+" --tray-owner "+owner) {
                    UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,
                    WorkingDirectory=Path.GetDirectoryName(executable)}};
                child.OutputDataReceived+=delegate(object sender,DataReceivedEventArgs e){Log(stdout,e.Data);};
                child.ErrorDataReceived+=delegate(object sender,DataReceivedEventArgs e){Log(stderr,e.Data);};
                if(!child.Start())throw new IOException("Service launch failed");
                child.BeginOutputReadLine();child.BeginErrorReadLine();
            }
            var deadline=Stopwatch.StartNew();
            while(deadline.ElapsedMilliseconds<5000) {
                if(child.HasExited) { child.WaitForExit();throw new IOException("Service exited with code "+child.ExitCode+". Logs: "+logs); }
                var state=Read(name);
                if(state!=null) {
                    using(var process=Verified(state)) { RequireOwned(state);if(state.Ready)return; }
                }
                Thread.Sleep(20);
            }
            throw new IOException("Service startup confirmation timed out. The tray remains available for stop/retry. Logs: "+logs);
        }
        internal string Command(string operation,string arguments,int timeoutMs) {
            lock(lifetime) {
                if(disposed)throw new ObjectDisposedException("Control");
                if(stopRequested)throw new IOException("Controller exit has started");
                if(operation!="Start"&&operation!="Stop"&&operation!="Restart")throw new ArgumentException("Unsupported service command");
                if(operation!="Start")StopOwnedAndWait(timeoutMs);
                if(operation!="Stop")EnsureRunningOwned(arguments);
                return operation=="Stop"?"STOPPED: owned process exit confirmed":"RUNNING: owned native process ready; tracking/output not verified";
            }
        }
        internal void StopAndWait(int timeoutMs) {
            lock(lifetime) {
                if(disposed)throw new ObjectDisposedException("Control");
                // Exit may run before the queued startup callback. Keep the
                // launch fence even if stopping an existing child fails.
                stopRequested=true;
                StopOwnedAndWait(timeoutMs);
            }
        }
        void StopOwnedAndWait(int timeoutMs) {
            var state=Read(name);
            if(state==null) {
                if(child!=null&&!child.HasExited)throw new IOException("Owned child is still running without identity; tray retained, no force kill");
                if(!scoped&&AnyUnmanagedProcess())throw new IOException("Service identity unavailable; tray retained, no force kill");
                return;
            }
            Process verified;
            try {verified=Verified(state);}
            catch(ArgumentException) {if((child==null||child.HasExited)&&(scoped||!AnyUnmanagedProcess()))return;throw;}
            using(var process=verified) {
                RequireOwned(state);
                using(var stop=EventWaitHandle.OpenExisting(name+"_Stop_"+state.Pid+"_"+state.Created))stop.Set();
                if(!process.WaitForExit(timeoutMs))throw new IOException("Service did not stop in time. Tray retained; no force kill");
                if(process.ExitCode!=0)throw new IOException("Service exited with code "+process.ExitCode+". Tray retained; inspect logs: "+logs);
            }
        }
        public void Dispose() {
            lock(lifetime) {
                if(disposed)return;
                disposed=true;stopRequested=true;
                DisposeOwned();
            }
        }
        void DisposeOwned() {
            // Disposing an exited tray cannot kill any process. Owner death is
            // detected by the native service and follows its normal cleanup path.
            if(child!=null) {
                if(child.HasExited)child.WaitForExit();
                child.Dispose();child=null;
            }
            if(stdout!=null) {lock(stdout)stdout.Dispose();stdout=null;}
            if(stderr!=null) {lock(stderr)stderr.Dispose();stderr=null;}
        }
    }
    internal sealed class ExitGate {
        internal bool TryExit(Action stopAndConfirm,Action exit,out string error) {
            try {stopAndConfirm();exit();error=null;return true;}
            catch(Exception e) {error=e.Message;return false;}
        }
    }
}
