using System;
using System.IO;
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
                var options = GuiLaunchOptions.Parse(args, ResolveDefaultRoot());
                if (options.ControlWindowOnly)
                {
                    SteamVrControlWpf.Run(options);
                    return 0;
                }

                using (var tray = new SteamVrControlTray(options))
                {
                    tray.Run();
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
