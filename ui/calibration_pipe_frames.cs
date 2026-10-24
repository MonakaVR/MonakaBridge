using System;
using System.IO;

namespace MonakaBridge {
 internal static class CalibrationPipeFrames {
  internal static byte[] ReadLine(Stream input,int maximum) {
   byte[] bytes=new byte[maximum];int length=0;
   while(true){int value=input.ReadByte();if(value<0){if(length!=0)throw new InvalidOperationException("Truncated native JSON line");return null;}
    if(value=='\n'){if(length>0&&bytes[length-1]=='\r')length--;byte[] line=new byte[length];Buffer.BlockCopy(bytes,0,line,0,length);return line;}
    if(length==maximum)throw new InvalidOperationException("Native JSON line limit");bytes[length++]=(byte)value;
   }
  }
  internal static void Drain(Stream input,int maximum,Action<byte[],int> consume) {
   byte[] bytes=new byte[4096];int total=0,n;
   while((n=input.Read(bytes,0,bytes.Length))>0){if(n>maximum-total)throw new InvalidOperationException("Native stderr byte limit");total+=n;if(consume!=null)consume(bytes,n);}
  }
 }
}
