using System;
using System.IO;
using System.Collections.Generic;
using System.Threading;
using System.Windows;

namespace MonakaBridge
{
    internal static class SteamVrControlGuiProgram
    {
        [STAThread]
        private static int Main(string[] args)
        {
            bool nativeWindow=Array.IndexOf(args,"--native-owner-window")>=0;
            try
            {
                string serviceExecutable=null,ports=null,logs=null;bool trayOnly=false;
                var guiArguments=new List<string>();
                for(int i=0;i<args.Length;++i) {
                    if(args[i]=="--service-exe") {
                        if(++i==args.Length)throw new ArgumentException("Missing service executable");
                        serviceExecutable=GuiLaunchOptions.Absolute(args[i],"--service-exe");
                    }else if(args[i]=="--runtime-ports") {
                        if(ports!=null||i+4>=args.Length)throw new ArgumentException("Four explicit runtime ports required");
                        var values=new List<string>();for(int k=0;k<4;k++){int port;if(!int.TryParse(args[++i],out port)||port<1||port>65535)throw new ArgumentException("Invalid runtime port");values.Add(port.ToString(System.Globalization.CultureInfo.InvariantCulture));}ports=string.Join(" ",values);
                    }else if(args[i]=="--runtime-logs") {if(logs!=null||++i==args.Length)throw new ArgumentException("Runtime log directory required");logs=GuiLaunchOptions.Absolute(args[i],"--runtime-logs");}
                    else if(args[i]=="--tray-only")trayOnly=true;
                    else if(args[i]=="--native-owner-window"){}
                    else guiArguments.Add(args[i]);
                }
                var options = GuiLaunchOptions.Parse(guiArguments.ToArray(), ResolveDefaultRoot());
                BridgeText.Language=options.Language;
                if((ports==null)!=(logs==null))throw new ArgumentException("Explicit runtime ports and owned log directory must be selected together");
                if(ports!=null&&options.HealthFile!=options.ConfigFile+".status.json")throw new ArgumentException("Native runtime health must be CONFIG.status.json");
                if (options.ControlWindowOnly)
                {
                    if(nativeWindow&&(options.OwnerExecutable==null||BridgeOwnerChannel.Request(options,"Probe")!="OWNER_READY"))throw new IOException("Native management GUI requires its owning Agent handshake");
                    SteamVrControlWpf.Run(options);
                    return 0;
                }

                if(serviceExecutable==null)throw new ArgumentException("Managed tray requires --service-exe. Window-only mode never manages a service");
                string scope=ports==null?"29810":BridgeOwnerChannel.Scope(options.ConfigFile,ports.Split(' ')[0]);
                string openName=@"Local\MonakaBridge_OpenControl_"+scope;
                bool created;
                using(var singleton=new Mutex(true,@"Local\MonakaBridge_ServiceTray_"+scope,out created))
                {
                    bool owned=created;
                    if(!owned)try{owned=singleton.WaitOne(0);}catch(AbandonedMutexException){owned=true;}
                    if(!owned) {
                        string controller=System.Diagnostics.Process.GetCurrentProcess().MainModule.FileName;
                        PcServiceLifecycle.Control.VerifyExistingTray(ports==null?@"Local\MonakaBridge_ServiceLifecycle_29810":@"Local\MonakaBridge_Controller_"+scope,ports==null?serviceExecutable:controller,controller);
                        if(!trayOnly)using(var request=EventWaitHandle.OpenExisting(openName))request.Set();
                        return 0;
                    }
                    try {using(var tray=new SteamVrControlTray(options,serviceExecutable,!trayOnly,openName,ports,logs))tray.Run();}
                    finally {singleton.ReleaseMutex();}
                }
                return 0;
            }
            catch (Exception ex)
            {
                if(nativeWindow){Console.Error.WriteLine(ex);return 1;}
                MessageBox.Show(
                    BridgeText.Diagnostic(ex.ToString()),
                    "Monaka Bridge",
                    MessageBoxButton.OK,
                    MessageBoxImage.Error);
                return 1;
            }
        }

        private static string ResolveDefaultRoot()
        {
            string baseDirectory = AppDomain.CurrentDomain.BaseDirectory;
            DirectoryInfo buildDirectory = new DirectoryInfo(
                baseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            if (buildDirectory != null &&
                string.Equals(buildDirectory.Name, "build-gui", StringComparison.OrdinalIgnoreCase) &&
                buildDirectory.Parent != null)
            {
                return buildDirectory.Parent.FullName;
            }

            string currentDirectory = Environment.CurrentDirectory;
            if (File.Exists(Path.Combine(currentDirectory, "ui", "steamvr_control_wpf.cs")))
            {
                return currentDirectory;
            }

            return baseDirectory;
        }
    }
}
