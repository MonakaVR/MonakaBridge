using System;
using System.IO;
using System.Reflection;
using System.Runtime.Serialization;
using MonakaBridge;
internal static class SharedWriterHarness {
 [STAThread] static int Main(string[] args){try{
  if(args.Length==2){
   var type=typeof(SteamVrControlTray);var tray=FormatterServices.GetUninitializedObject(type);
   foreach(var pair in new[]{new[]{"repoRoot",args[0]},new[]{"configDirectory",Path.Combine(args[0],"config")}})type.GetField(pair[0],BindingFlags.NonPublic|BindingFlags.Instance).SetValue(tray,pair[1]);
   var route=type.GetNestedType("OutputRoute",BindingFlags.NonPublic);type.GetMethod("SaveRoute",BindingFlags.NonPublic|BindingFlags.Instance).Invoke(tray,new[]{Enum.Parse(route,args[1])});
  }else if(args.Length==3){SteamVrControlWpf.CommitFile(args[0],File.ReadAllBytes(args[1]),args[2]);}
  else throw new ArgumentException("owned root + route OR root + expected + candidate");
  return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
