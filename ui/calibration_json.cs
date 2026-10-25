using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Web.Script.Serialization;

namespace MonakaBridge {
 // Bounded local control JSON. Canonical tracking data/codec stay native.
 internal static class CalibrationJson {
  internal static readonly Encoding Utf8=new UTF8Encoding(false,true);
  internal static Dictionary<string,object> Object(byte[] bytes,int maxBytes,int maxString) {
   if(bytes==null||bytes.Length>maxBytes)throw new InvalidOperationException("JSON byte limit");
   var parser=new Parser(Utf8.GetString(bytes),maxString);
   var value=parser.Read();var result=value as Dictionary<string,object>;
   if(result==null)throw new InvalidOperationException("JSON object required");return result;
  }
  internal static byte[] Encode(Dictionary<string,object> value,int maxBytes) {
   var serializer=new JavaScriptSerializer{MaxJsonLength=maxBytes,RecursionLimit=16};
   byte[] raw=Utf8.GetBytes(serializer.Serialize(value));Object(raw,maxBytes,32768);return raw;
  }
  internal static void Keys(Dictionary<string,object> value,string[] required,string[] optional) {
   var allowed=new HashSet<string>(required,StringComparer.Ordinal);
   if(optional!=null)foreach(string name in optional)allowed.Add(name);
   foreach(string name in required)if(!value.ContainsKey(name))throw new InvalidOperationException("Missing field: "+name);
   foreach(string name in value.Keys)if(!allowed.Contains(name))throw new InvalidOperationException("Unknown field: "+name);
  }
  internal static string Text(Dictionary<string,object> value,string name,int maxBytes) {
   object raw;if(!value.TryGetValue(name,out raw)||!(raw is string))throw new InvalidOperationException("String required: "+name);
   string text=(string)raw;
   if(text.Length==0||Utf8.GetByteCount(text)>maxBytes)throw new InvalidOperationException("String bound: "+name);
   foreach(char c in text)if(c<32)throw new InvalidOperationException("Control character: "+name);
   return text;
  }
  internal static long Integer(Dictionary<string,object> value,string name,long minimum,long maximum) {
   object raw;if(!value.TryGetValue(name,out raw)||(!(raw is long)&&!(raw is int)))throw new InvalidOperationException("Integer required: "+name);
   long number=Convert.ToInt64(raw,CultureInfo.InvariantCulture);
   if(number<minimum||number>maximum)throw new InvalidOperationException("Integer bound: "+name);return number;
  }
  internal static bool Boolean(Dictionary<string,object> value,string name) {
   object raw;if(!value.TryGetValue(name,out raw)||!(raw is bool))throw new InvalidOperationException("Boolean required: "+name);return (bool)raw;
  }
  internal static string Hash(Dictionary<string,object> value,string name) {
   string hash=Text(value,name,64);if(hash.Length!=64)throw new InvalidOperationException("SHA256 required: "+name);
   foreach(char c in hash)if(!Uri.IsHexDigit(c))throw new InvalidOperationException("SHA256 required: "+name);return hash.ToLowerInvariant();
  }
  sealed class Parser {
   readonly string text;readonly int maxString;int at,fields,values;
   internal Parser(string input,int limit){text=input;maxString=limit;}
   InvalidOperationException Bad(string reason){return new InvalidOperationException("Invalid local JSON: "+reason);}
   void Space(){while(at<text.Length&&(text[at]==' '||text[at]=='\t'||text[at]=='\r'||text[at]=='\n'))at++;}
   bool Take(char c){Space();if(at<text.Length&&text[at]==c){at++;return true;}return false;}
   void Need(char c){if(!Take(c))throw Bad("expected "+c);}
   internal object Read(){object value=Value(0);Space();if(at!=text.Length)throw Bad("trailing bytes");return value;}
   object Value(int depth) {
    if(depth>16||++values>4096)throw Bad("depth/value limit");Space();if(at>=text.Length)throw Bad("unexpected end");
    char c=text[at];if(depth>=16&&(c=='{'||c=='['))throw Bad("depth limit");
    if(c=='{')return Map(depth+1);if(c=='[')return Array(depth+1);if(c=='"')return String();
    if(c=='t'){Word("true");return true;}if(c=='f'){Word("false");return false;}if(c=='n'){Word("null");return null;}
    if(c=='-'||(c>='0'&&c<='9'))return Number();throw Bad("unexpected value");
   }
   void Word(string word){if(text.Length-at<word.Length||string.CompareOrdinal(text,at,word,0,word.Length)!=0)throw Bad("literal");at+=word.Length;}
   Dictionary<string,object> Map(int depth) {
    Need('{');var result=new Dictionary<string,object>(StringComparer.Ordinal);if(Take('}'))return result;
    while(true){
     Space();if(at>=text.Length||text[at]!='"')throw Bad("object key");string key=String();
     if(result.Count>=64||++fields>256)throw Bad("field limit");if(result.ContainsKey(key))throw Bad("duplicate key");
     Need(':');result.Add(key,Value(depth));if(Take('}'))return result;Need(',');
    }
   }
   IList Array(int depth) {
    Need('[');var result=new ArrayList();if(Take(']'))return result;
    while(true){if(result.Count>=256)throw Bad("array limit");result.Add(Value(depth));if(Take(']'))return result;Need(',');}
   }
   string String() {
    Need('"');var result=new StringBuilder();bool ended=false;
    while(at<text.Length){
     char c=text[at++];if(c=='"'){ended=true;break;}if(c<32)throw Bad("raw control character");
     if(c=='\\'){
      if(at>=text.Length)throw Bad("escape");c=text[at++];
      switch(c){
       case '"':case '\\':case '/':break;
       case 'b':c='\b';break;case 'f':c='\f';break;case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;
       case 'u':
        if(text.Length-at<4)throw Bad("unicode escape");int value=0;
        for(int i=0;i<4;i++){char h=text[at++];int n=h>='0'&&h<='9'?h-'0':h>='a'&&h<='f'?h-'a'+10:h>='A'&&h<='F'?h-'A'+10:-1;if(n<0)throw Bad("unicode hex");value=value*16+n;}
        c=(char)value;break;
       default:throw Bad("escape");
      }
     }
     if(result.Length>=maxString)throw Bad("string limit");result.Append(c);
    }
    if(!ended)throw Bad("unterminated string");string output=result.ToString();
    for(int i=0;i<output.Length;i++){
     if(char.IsHighSurrogate(output[i])){if(++i>=output.Length||!char.IsLowSurrogate(output[i]))throw Bad("unpaired surrogate");}
     else if(char.IsLowSurrogate(output[i]))throw Bad("unpaired surrogate");
    }
    return output;
   }
   object Number() {
    int start=at;if(text[at]=='-'){at++;if(at>=text.Length)throw Bad("number");}
    if(text[at]=='0')at++;else {if(text[at]<'1'||text[at]>'9')throw Bad("number");while(at<text.Length&&text[at]>='0'&&text[at]<='9')at++;}
    bool integer=true;
    if(at<text.Length&&text[at]=='.'){integer=false;at++;Digits();}
    if(at<text.Length&&(text[at]=='e'||text[at]=='E')){integer=false;at++;if(at<text.Length&&(text[at]=='+'||text[at]=='-'))at++;Digits();}
    string raw=text.Substring(start,at-start);
    if(integer){long number;if(!long.TryParse(raw,NumberStyles.AllowLeadingSign,CultureInfo.InvariantCulture,out number))throw Bad("integer overflow");return number;}
    double floating;if(!double.TryParse(raw,NumberStyles.Float,CultureInfo.InvariantCulture,out floating)||double.IsNaN(floating)||double.IsInfinity(floating))throw Bad("nonfinite number");return floating;
   }
   void Digits(){int start=at;while(at<text.Length&&text[at]>='0'&&text[at]<='9')at++;if(at==start)throw Bad("number digits");}
  }
 }
}
