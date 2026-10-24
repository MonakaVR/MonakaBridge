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
            try
            {
                string serviceExecutable=null;bool trayOnly=false;
                var guiArguments=new List<string>();
                for(int i=0;i<args.Length;++i) {
                    if(args[i]=="--service-exe") {
                        if(++i==args.Length)throw new ArgumentException("Missing service executable");
                        serviceExecutable=GuiLaunchOptions.Absolute(args[i],"--service-exe");
                    }else if(args[i]=="--tray-only")trayOnly=true;
                    else guiArguments.Add(args[i]);
                }
                var options = GuiLaunchOptions.Parse(guiArguments.ToArray(), ResolveDefaultRoot());
                if (options.ControlWindowOnly)
                {
                    SteamVrControlWpf.Run(options);
                    return 0;
                }

                if(serviceExecutable==null)throw new ArgumentException("Managed tray requires --service-exe. Window-only mode never manages a service");
                bool created;
                using(var singleton=new Mutex(true,@"Local\MonakaBridge_ServiceTray_29810",out created))
                {
                    bool owned=created;
                    if(!owned)try{owned=singleton.WaitOne(0);}catch(AbandonedMutexException){owned=true;}
                    if(!owned) {
                        PcServiceLifecycle.Control.VerifyExistingTray(@"Local\MonakaBridge_ServiceLifecycle_29810",serviceExecutable,System.Diagnostics.Process.GetCurrentProcess().MainModule.FileName);
                        if(!trayOnly)using(var request=EventWaitHandle.OpenExisting(@"Local\MonakaBridge_OpenControl_29810"))request.Set();
                        return 0;
                    }
                    try {using(var tray=new SteamVrControlTray(options,serviceExecutable,!trayOnly))tray.Run();}
                    finally {singleton.ReleaseMutex();}
                }
                return 0;
            }
            catch (Exception ex)
            {
                MessageBox.Show(
                    ex.ToString(),
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
