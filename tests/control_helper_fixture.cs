using System;
using System.IO;
using System.Threading;
class FixtureHelper {
 static int Main(string[] args){
  string mode=File.ReadAllText(System.Reflection.Assembly.GetExecutingAssembly().Location+".mode").Trim();
  if(mode=="slow")Thread.Sleep(5000);
  if(args.Length>1&&args[1]=="transaction-version"){Console.WriteLine(mode=="wrong"?"legacy-v0":"monaka-config-transaction-v1");return 0;}
  if(mode=="slowvalidate"&&args.Length>1&&args[1]=="validate")Thread.Sleep(1000);
  if(mode=="failcommit"&&args.Length>1&&args[1]=="commit"){Console.Error.WriteLine("injected fixture commit rejection");return 9;}
  if(mode=="fail"){Console.Error.WriteLine("injected fixture rejection");return 8;}
  return 0;
 }
}
