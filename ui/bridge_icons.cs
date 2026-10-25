using System;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Microsoft.Win32;

namespace MonakaBridge {
 internal static class BridgeIcons {
  internal static Icon Load(bool lightBackground) {
   string name="MonakaBridge.Icons."+(lightBackground?"black":"white")+".ico";
   using(Stream stream=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)) {
    if(stream==null)throw new InvalidOperationException("Missing embedded Bridge icon: "+name);
    using(var original=new Icon(stream))return (Icon)original.Clone();
   }
  }
  internal static Icon TrayIcon() { return Load(false); }
  internal static bool LightTaskbar() {
   try {
    using(var key=Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize")) {
     object value=key==null?null:key.GetValue("SystemUsesLightTheme");
     return value!=null&&Convert.ToInt32(value)!=0;
    }
   }catch(System.Security.SecurityException){}catch(UnauthorizedAccessException){}catch(IOException){}
   return false;
  }
  internal static ImageSource WindowIcon() {
   using(Stream stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("MonakaBridge.Icons.white.ico")) {
    var image=BitmapFrame.Create(stream,BitmapCreateOptions.PreservePixelFormat,BitmapCacheOption.OnLoad);
    image.Freeze();return image;
   }
  }
 }
}
