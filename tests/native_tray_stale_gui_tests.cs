using System;
using System.IO;
using System.Threading;
using MonakaBridge;
internal static class NativeTrayStaleGuiTests {
 static int Main(string[] args) {try {
  string config=Path.GetFullPath(args[0]);byte[] before=File.ReadAllBytes(config);
  var options=GuiLaunchOptions.Parse(new[]{"--config",config,"--config-helper",Path.GetFullPath(args[1]),"--allow-write"},Path.GetDirectoryName(config));
  bool rejected=false;
  try {new ConfigHelperClient(options).TransformAndCommit(File.ReadAllBytes(args[2]),"policy disabled",false,CancellationToken.None);}
  catch(InvalidOperationException e) {rejected=e.Message.Contains("Configuration changed in another tool; reload before applying.");}
  if(!rejected||Convert.ToBase64String(before)!=Convert.ToBase64String(File.ReadAllBytes(config)))throw new Exception("Stale actual GUI helper client overwrote native tray selection");
  Console.WriteLine("PASS actual ConfigHelperClient stale snapshot rejected; reload required; tray selection retained");return 0;
 }catch(Exception e){Console.WriteLine(e);return 1;}}
}
