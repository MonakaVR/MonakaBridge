using System;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using MonakaBridge;
class CalibrationJsonTests {
 static int cases;
 static void Check(bool ok){if(!ok)throw new Exception("test assertion");}
 static Dictionary<string,object> Parse(string raw){return CalibrationJson.Object(CalibrationJson.Utf8.GetBytes(raw),65536,32768);}
 static void Reject(Action action){try{action();}catch(Exception e){if(!(e is InvalidOperationException)&&!(e is DecoderFallbackException)&&!(e is EncoderFallbackException))throw;cases++;return;}throw new Exception("Expected rejection");}
 static string Nest(int depth){return "{\"value\":"+new string('[',depth-1)+"0"+new string(']',depth-1)+"}";}
 static int Main(){try{
  var value=Parse("{\"text\":\"測定 \\uD83D\\uDE80\",\"id\":9223372036854775807,\"u32\":4294967295,\"flag\":false,\"pose\":[1.25,-2e-3,null]}");
  Check((string)value["text"]=="測定 🚀"&&CalibrationJson.Integer(value,"id",0,long.MaxValue)==long.MaxValue&&CalibrationJson.Integer(value,"u32",0,UInt32.MaxValue)==UInt32.MaxValue&&!CalibrationJson.Boolean(value,"flag"));cases++;
  var copy=CalibrationJson.Object(CalibrationJson.Encode(value,65536),65536,32768);Check((string)copy["text"]==(string)value["text"]&&copy["id"] is long);cases++;
  Parse(Nest(16));cases++;Reject(delegate{Parse(Nest(17));});
  foreach(string raw in new[]{"{\"a\":1,\"a\":2}","{\"a\":{\"x\":0,\"x\":1}}","{} true","[]","{\"x\":01}","{\"x\":-01}","{\"x\":1.}","{\"x\":1e}","{\"x\":1e9999}","{\"x\":9223372036854775808}","{\"x\":NaN}","{\"x\":\"\\uD800\"}","{\"x\":\"\\uDC00\"}","{\"x\":\"\\q\"}","{\"x\":\"unterminated}","{\"x\":\"raw\nnewline\"}","{\"x\":1,}"}){string bad=raw;Reject(delegate{Parse(bad);});}
  Reject(delegate{CalibrationJson.Object(new byte[]{0x7b,0x22,0xc3,0x28,0x22,0x3a,0x30,0x7d},65536,32768);});
  Reject(delegate{Parse("\ufeff{}");});
  var array=new StringBuilder("{\"a\":[");for(int i=0;i<257;i++){if(i>0)array.Append(',');array.Append('0');}array.Append("]}");Reject(delegate{Parse(array.ToString());});
  var fields=new StringBuilder("{");for(int i=0;i<65;i++){if(i>0)fields.Append(',');fields.Append('"').Append(i).Append("\":0");}fields.Append('}');Reject(delegate{Parse(fields.ToString());});
  var total=new StringBuilder("{\"items\":[");for(int i=0;i<6;i++){if(i>0)total.Append(',');total.Append('{');for(int n=0;n<44;n++){if(n>0)total.Append(',');total.Append('"').Append(n).Append("\":0");}total.Append('}');}total.Append("]}");Reject(delegate{Parse(total.ToString());});
  Reject(delegate{Parse("{\"x\":\""+new string('a',32769)+"\"}");});
  Reject(delegate{CalibrationJson.Object(new byte[65537],65536,32768);});
  Reject(delegate{CalibrationJson.Integer(Parse("{\"x\":1e0}"),"x",0,10);});
  Reject(delegate{CalibrationJson.Integer(Parse("{\"x\":-1}"),"x",0,UInt32.MaxValue);});
  Reject(delegate{CalibrationJson.Boolean(Parse("{\"x\":1}"),"x");});
  Reject(delegate{CalibrationJson.Text(Parse("{\"x\":null}"),"x",4096);});
  Reject(delegate{CalibrationJson.Text(Parse("{\"x\":\"\\u0000\"}"),"x",4096);});
  Reject(delegate{CalibrationJson.Text(Parse("{\"x\":\""+new string('測',1400)+"\"}"),"x",4096);});
  Reject(delegate{CalibrationJson.Keys(Parse("{\"x\":1,\"unknown\":true}"),new[]{"x"},null);});
  Reject(delegate{CalibrationJson.Keys(Parse("{}"),new[]{"x"},null);});
  string hash=new string('A',64);Check(CalibrationJson.Hash(Parse("{\"hash\":\""+hash+"\"}"),"hash")==hash.ToLowerInvariant());cases++;
  Reject(delegate{CalibrationJson.Hash(Parse("{\"hash\":\""+new string('g',64)+"\"}"),"hash");});
  Console.WriteLine("PASS calibration strict local JSON "+cases+" cases; no GUI/SDK/socket/config activity");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
