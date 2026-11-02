using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Diagnostics;
using System.Threading;

internal static class LifecycleFixture {
 static int Main(string[] args) {
  if(args.Length!=4 || args[2]!="--tray-owner")return 2;
  var owner=args[3].Split(':');
  using(var self=Process.GetCurrentProcess()) {
   long created=self.StartTime.ToUniversalTime().ToFileTimeUtc();
   using(var stop=new EventWaitHandle(false,EventResetMode.ManualReset,args[0]+"_Stop_"+self.Id+"_"+created))
   using(var map=MemoryMappedFile.CreateNew(args[0],48))
   using(var view=map.CreateViewAccessor()) {
    view.Write(0,0x314c4350u);view.Write(4,1u);view.Write(8,1L);
    view.Write(16,(uint)self.Id);view.Write(20,uint.Parse(owner[0]));
    view.Write(24,created);view.Write(32,long.Parse(owner[1]));view.Write(40,1u);
    Thread.MemoryBarrier();view.Write(8,2L);
    File.WriteAllText(args[1],self.Id.ToString());
    return stop.WaitOne(10000)?0:3;
   }
  }
 }
}
