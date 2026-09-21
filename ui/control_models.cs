using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
[assembly:System.Runtime.CompilerServices.InternalsVisibleTo("bridge_ui_tests")]
namespace MonakaBridge {
 // The selection key refers to the loaded mapping, never to in-progress edits.
 // Programmatic updates are synchronous and nest safely even when WPF fires events.
 internal sealed class MappingEditorState {
  int updateDepth;
  internal bool Updating {get{return updateDepth!=0;}}
  internal int SelectedIndex {get;private set;}
  internal MappingEditorState(){SelectedIndex=-1;}
  internal Tuple<string,string> SelectedKey {get;private set;}
  internal bool Approved;
  internal static Tuple<string,string> Key(Dictionary<string,object> row){return Tuple.Create((string)row["source_id"],(string)row["device_id"]);}
  internal void Programmatic(Action update){++updateDepth;try{update();}finally{--updateDepth;}}
  internal bool IdentityEdited(){if(Updating)return false;Approved=false;return true;}
  internal void Select(IList rows,int index,Action<Dictionary<string,object>> render){
   var row=rows!=null&&index>=0&&index<rows.Count?(Dictionary<string,object>)rows[index]:null;
   SelectedIndex=row==null?-1:index;SelectedKey=row==null?null:Key(row);
   Approved=row!=null&&Convert.ToBoolean(row["space_approved"],CultureInfo.InvariantCulture);
   Programmatic(delegate{render(row);});
  }
  internal void Restore(IList rows,Tuple<string,string> key,Action<Dictionary<string,object>> render){
   int found=-1;
   if(key!=null)for(int i=0;i<rows.Count;++i)if(Key((Dictionary<string,object>)rows[i]).Equals(key)){
    if(found>=0)throw new InvalidOperationException("Duplicate source/device mapping; correct the configuration before editing.");
    found=i;
   }
   Select(rows,found,render);
  }
 }
 internal static class MappingEditor {
  internal static Dictionary<string,object> RequireProfile(Dictionary<string,object> config,string name){
   object value;var profiles=(Dictionary<string,object>)config["profiles"];
   if(string.IsNullOrEmpty(name)||!profiles.TryGetValue(name,out value)||!(value is Dictionary<string,object>))
    throw new InvalidOperationException("Select an existing profile from the configuration. Unknown or missing profile: "+(name??"(none)"));
   return (Dictionary<string,object>)value;
  }
  internal static Dictionary<string,object> Clone(Dictionary<string,object> value){
   var json=new JavaScriptSerializer();return json.Deserialize<Dictionary<string,object>>(json.Serialize(value));
  }
  internal static Tuple<string,string,string> Identity(string publisher,string source,string tracker){return Tuple.Create(publisher,source,tracker);}
  static string Text(Dictionary<string,object> value,string name){return Convert.ToString(value[name],CultureInfo.InvariantCulture);}
  static Dictionary<string,object> Rigid(){return new Dictionary<string,object>{{"rotation",new double[]{0,0,0,1}},{"translation",new double[]{0,0,0}}};}
  // Build a candidate without mutating the loaded UI model. Native validation and
  // atomic replacement must succeed before the GUI adopts this candidate.
  internal static Dictionary<string,object> Candidate(Dictionary<string,object> loaded,int selectedIndex,Dictionary<string,object> edit){
   RequireProfile(loaded,Convert.ToString(edit["profile"],CultureInfo.InvariantCulture));
   var result=Clone(loaded);var list=new ArrayList((ICollection)result["mappings"]);
   if(selectedIndex < -1 || selectedIndex>=list.Count)throw new InvalidOperationException("Mapping selection changed; reload first.");
   Dictionary<string,object> selected=selectedIndex<0?null:(Dictionary<string,object>)list[selectedIndex];
   if(selected==null)foreach(Dictionary<string,object> b in list)
    if(Text(b,"source_id")==Text(edit,"source_id")&&Text(b,"device_id")==Text(edit,"device_id"))selected=b;
   if(selected==null){selected=new Dictionary<string,object>{{"world",Rigid()},{"mount",Rigid()},{"world_revision",0}};list.Add(selected);}
   foreach(var name in new[]{"source_id","device_id","tracker_id","profile","input_space","input_revision","world_space","space_approved"})selected[name]=edit[name];
   var inputs=new HashSet<Tuple<string,string>>();var outputs=new HashSet<Tuple<string,string,string>>();
   string publisher=Text(result,"bridge_id");
   foreach(Dictionary<string,object> b in list){
    if(!inputs.Add(Tuple.Create(Text(b,"source_id"),Text(b,"device_id"))))throw new InvalidOperationException("Source/device mapping collision; select the existing mapping.");
    if(!outputs.Add(Identity(publisher,Text(b,"source_id"),Text(b,"tracker_id"))))throw new InvalidOperationException("Publisher/source/tracker mapping collision.");
   }
   result["mappings"]=list.ToArray();result["mapping_revision"]=checked(Convert.ToUInt32(result["mapping_revision"])+1);
   return result;
  }
 }
 internal sealed class HealthReadResult {
  internal Dictionary<string,object> Value;
  internal DateTime SnapshotUtc;
  internal string Error;
 }
 internal sealed class HealthReader:IDisposable {
  readonly Func<string,HealthReadResult> read;
  int busy,disposed;
  internal HealthReader():this(ReadOnce){}
  internal HealthReader(Func<string,HealthReadResult> operation){read=operation;}
  internal static HealthReadResult ReadOnce(string path){
   // Single attempt off the UI thread. Permit the producer's atomic replacement.
   using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)){
    if(file.Length>32*1024*1024)throw new IOException("Health snapshot exceeds diagnostic size bound.");
    using(var text=new StreamReader(file,Encoding.UTF8)){
     var json=new JavaScriptSerializer{MaxJsonLength=32*1024*1024};
     var value=json.Deserialize<Dictionary<string,object>>(text.ReadToEnd());
     if(value==null||!(value["devices"] is ICollection))throw new IOException("Invalid health snapshot.");
     // Time of snapshot creation, not delayed disk replacement/read completion.
     var timestamp=value.ContainsKey("snapshot_unix_ms")?new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc).AddMilliseconds(Convert.ToDouble(value["snapshot_unix_ms"],CultureInfo.InvariantCulture)):DateTime.MinValue;
     return new HealthReadResult{Value=value,SnapshotUtc=timestamp};
    }
   }
  }
  internal bool Request(string path,Action<Action> post,Action<HealthReadResult> apply){
   if(Volatile.Read(ref disposed)!=0||Interlocked.CompareExchange(ref busy,1,0)!=0)return false;
   ThreadPool.QueueUserWorkItem(delegate{
    HealthReadResult result;
    try{result=read(path);}catch(Exception e){result=new HealthReadResult{Error=e.Message};}
    if(Volatile.Read(ref disposed)!=0){Interlocked.Exchange(ref busy,0);return;}
    try{post(delegate{
     try{if(Volatile.Read(ref disposed)==0)apply(result);}
     finally{Interlocked.Exchange(ref busy,0);}
    });}catch{Interlocked.Exchange(ref busy,0);}
   });return true;
  }
  // A slow file read may finish later; never join it on the UI thread or queue
  // more reads. The single background job drops its result after window close.
  public void Dispose(){Interlocked.Exchange(ref disposed,1);}
 }
 internal static class HealthPresentation {
  internal static string MappingRevision(object configured,object observed,bool current,bool samePublisher){
   string configText=Convert.ToString(configured,CultureInfo.InvariantCulture),healthText=observed==null?"unknown":Convert.ToString(observed,CultureInfo.InvariantCulture);
   string comparison=!current||!samePublisher||observed==null?"unavailable / historical; application status unknown":
    (configText==healthText?"match (diagnostic snapshot)":"different (diagnostic snapshot); check runtime/restart requirements");
   return "Mapping revision: config="+configText+" | health="+healthText+" | "+comparison;
  }
  internal static bool Current(DateTime snapshot,DateTime now){return snapshot!=DateTime.MinValue&&now>=snapshot&&now-snapshot<TimeSpan.FromSeconds(3);}
  static string Text(Dictionary<string,object> row,string name){return row.ContainsKey(name)?Convert.ToString(row[name],CultureInfo.InvariantCulture):"unknown";}
  static bool Flag(Dictionary<string,object> row,string name){return row.ContainsKey(name)&&Convert.ToBoolean(row[name],CultureInfo.InvariantCulture);}
  static string ArrayText(object value,int expected){
   var a=value as IList;if(a==null||a.Count!=expected)return "-";
   var parts=new string[a.Count];for(int i=0;i<a.Count;++i)parts[i]=Convert.ToDouble(a[i],CultureInfo.InvariantCulture).ToString("F5",CultureInfo.InvariantCulture);
   return "["+string.Join(", ",parts)+"]";
  }
  internal static string Pose(Dictionary<string,object> row,bool available,double elapsedMs=0,double timeoutMs=500){
   try{return FormatPose(row,available,elapsedMs,timeoutMs);}
   catch(Exception){return "Pose unavailable (invalid health data); not final SteamVR output.";}
  }
  static string FormatPose(Dictionary<string,object> row,bool available,double elapsedMs,double timeoutMs){
   string stage=Text(row,"pose_stage");
   double age=row.ContainsKey("pose_age_ms")?Convert.ToDouble(row["pose_age_ms"],CultureInfo.InvariantCulture):-1;
   bool fresh=Flag(row,"fresh")&&age>=0&&elapsedMs>=0&&age+elapsedMs<timeoutMs;
   string freshness=available?(fresh?"fresh":"stale"):"unavailable / historical";
   var p=row.ContainsKey("position")?ArrayText(row["position"],3):"-";
   var q=row.ContainsKey("orientation_xyzw")?ArrayText(row["orientation_xyzw"],4):"-";
   return "Pose stage: "+stage+" (not final SteamVR output)\nCoordinate space: "+Text(row,"space")+" | convention="+Text(row,"convention")+" | revision="+Text(row,"revision")+
    "\nTracking: "+Text(row,"tracking_state")+" | "+freshness+" | sample position valid="+Flag(row,"position_valid")+" | sample orientation valid="+Flag(row,"orientation_valid")+
    "\nPosition [m]: "+p+"\nQuaternion xyzw: "+q;
  }
 }
}
