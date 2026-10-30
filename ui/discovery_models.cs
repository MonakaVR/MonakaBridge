using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Web.Script.Serialization;

namespace MonakaBridge {
 // Diagnostic claims never authorize tracking. Only an explicitly selected
 // persisted source contract and verified coordinate profile can make a draft.
 internal sealed class DiscoveryObservation {
  internal string Source,Device,Session,Clock,Space,Convention,Reason;
  internal uint Revision;internal string[] Claims;internal bool Configured,Fresh;
  internal double SeenAge;
  public override string ToString(){return Source+" / "+Device+" | "+Reason+" | "+Space+" @"+Revision+" | "+(Fresh?"fresh":"stale");}
  internal bool Same(DiscoveryObservation other){return other!=null&&Source==other.Source&&Device==other.Device&&Session==other.Session&&Clock==other.Clock&&Space==other.Space&&Convention==other.Convention&&Revision==other.Revision&&DiscoveryModel.SetEqual(Claims,other.Claims);}
 }
 internal sealed class DiscoveryContract {
  internal int MappingIndex;internal string Source,Device,Label;internal Dictionary<string,object> Value;
  public override string ToString(){return Label;}
 }
 internal sealed class DiscoverySnapshot {
  internal readonly List<DiscoveryObservation> Rows=new List<DiscoveryObservation>();
  internal readonly Dictionary<Tuple<string,string>,double> ConfiguredReceipts=new Dictionary<Tuple<string,string>,double>();
  internal string Error;internal bool Ready {get{return Error==null;}}
  internal DiscoveryObservation Find(string source,string device){return Rows.Find(delegate(DiscoveryObservation r){return r.Source==source&&r.Device==device;});}
 }
 internal enum DiscoveryAction {Create,Replace,UpdateRevision}
 internal sealed class DiscoveryDraft {
  internal Dictionary<string,object> Document;internal DiscoveryObservation Observation;
  internal DiscoveryAction Action;internal int OldIndex;internal string OldDevice,Tracker,Profile;internal DiscoveryContract Contract;
  internal string Confirmation;
 }
 internal static class DiscoveryModel {
  internal static string Text(Dictionary<string,object> o,string key){object v;return o!=null&&o.TryGetValue(key,out v)?Convert.ToString(v,CultureInfo.InvariantCulture):"";}
  internal static bool Flag(Dictionary<string,object> o,string key){object v;return o!=null&&o.TryGetValue(key,out v)&&v is bool&&(bool)v;}
  internal static IList Mappings(Dictionary<string,object> c){return (IList)c["mappings"];}
  internal static bool SetEqual(IEnumerable<string> a,IEnumerable<string> b){return new HashSet<string>(a,StringComparer.Ordinal).SetEquals(b);}
  static string[] Tokens(object value){var result=new List<string>();var list=value as IList;if(list==null||list.Count>16)throw new InvalidOperationException("Invalid capability claim list");foreach(object v in list){var s=v as string;if(string.IsNullOrWhiteSpace(s)||s.Length>64||result.Contains(s))throw new InvalidOperationException("Invalid capability claim");result.Add(s);}return result.ToArray();}
  static string Id(Dictionary<string,object> o,string name){string s=Text(o,name);if(string.IsNullOrWhiteSpace(s)||s.Length>256||s.IndexOfAny(new[]{'\r','\n','\0'})>=0)throw new InvalidOperationException("Invalid diagnostic identity: "+name);return s;}
  internal static DiscoverySnapshot Read(Dictionary<string,object> config,Dictionary<string,object> health,DateTime now){
   var result=new DiscoverySnapshot();try{
    if(config==null||health==null)throw new InvalidOperationException("Load configuration and current discovery health");
    if(Text(config,"bridge_id")!=Text(health,"publisher_id"))throw new InvalidOperationException("Foreign publisher health; no discovery writes");
    var stamp=new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc).AddMilliseconds(Convert.ToDouble(health["snapshot_unix_ms"],CultureInfo.InvariantCulture));
    if(!HealthPresentation.Current(stamp,now))throw new InvalidOperationException("Stale or future health; no discovery writes");
    if(Text(config,"mapping_revision")!=Text(health,"mapping_revision"))throw new InvalidOperationException("Runtime/config revision differs; restart or reload/review first");
    var diagnostic=health.ContainsKey("discovery")?health["discovery"] as Dictionary<string,object>:null;
    if(diagnostic==null||!Flag(diagnostic,"diagnostic_only")||!Flag(diagnostic,"claims_are_untrusted")||!diagnostic.ContainsKey("truncated")||!(diagnostic["truncated"] is bool)||Flag(diagnostic,"truncated")||Convert.ToInt32(diagnostic["capacity"])!=32||Convert.ToInt32(diagnostic["source_capacity"])!=8||Convert.ToInt32(diagnostic["recent_ms"])!=3000)throw new InvalidOperationException("Discovery unavailable or bounded cache incomplete; no discovery writes");
    var rows=diagnostic["observations"] as IList;if(rows==null||rows.Count>32)throw new InvalidOperationException("Invalid discovery rows");
    var keys=new HashSet<Tuple<string,string>>();double elapsed=(now-stamp).TotalMilliseconds;
    var receipts=diagnostic["recent_configured_receipts"] as IList;if(receipts==null||receipts.Count>32)throw new InvalidOperationException("Configured receipt guard unavailable");
    foreach(Dictionary<string,object> receipt in receipts){var key=Tuple.Create(Id(receipt,"source"),Id(receipt,"device"));double age=Convert.ToDouble(receipt["seen_age_ms"],CultureInfo.InvariantCulture);if(double.IsNaN(age)||double.IsInfinity(age)||age<0||result.ConfiguredReceipts.ContainsKey(key))throw new InvalidOperationException("Invalid configured receipt guard");result.ConfiguredReceipts.Add(key,age+elapsed);}
    foreach(Dictionary<string,object> row in rows){var frame=(Dictionary<string,object>)row["coordinate_space"];
     var r=new DiscoveryObservation{Source=Id(row,"source"),Device=Id(row,"device"),Session=Id(row,"session_id"),Clock=Id(row,"clock_id"),Space=Id(frame,"id"),Convention=Id(frame,"convention"),Revision=Convert.ToUInt32(frame["revision"]),Claims=Tokens(row["advertised_capabilities"]),Reason=Text(row,"reason"),Configured=Flag(row,"configured")};
     double seen=Convert.ToDouble(row["seen_age_ms"],CultureInfo.InvariantCulture),sample=Convert.ToDouble(row["sample_age_ms"],CultureInfo.InvariantCulture);
     if(double.IsNaN(seen)||double.IsInfinity(seen)||double.IsNaN(sample)||double.IsInfinity(sample))throw new InvalidOperationException("Invalid discovery age");
     r.SeenAge=seen<0?-1:seen+elapsed;r.Fresh=Flag(row,"fresh")&&r.SeenAge>=0&&r.SeenAge<3000&&sample>=0&&sample+elapsed<3000;
     if(!keys.Add(Tuple.Create(r.Source,r.Device)))throw new InvalidOperationException("Duplicate discovery identity");result.Rows.Add(r);
    }
   }catch(Exception e){result.Error=e.Message;result.Rows.Clear();result.ConfiguredReceipts.Clear();}return result;
  }
  internal static List<DiscoveryContract> Contracts(Dictionary<string,object> config,string source){
   var result=new List<DiscoveryContract>();if(config==null)return result;var rows=Mappings(config);
   for(int n=0;n<rows.Count;++n){var b=(Dictionary<string,object>)rows[n];if(Text(b,"source_id")!=source||!b.ContainsKey("runtime_contract"))continue;
    var c=b["runtime_contract"] as Dictionary<string,object>;if(c==null||Text(c,"id")==""||Text(c,"provenance")=="")continue;
    result.Add(new DiscoveryContract{MappingIndex=n,Source=source,Value=MappingEditor.Clone(c),Label=Text(c,"id")+" | "+Text(c,"input_policy")+" | template "+Text(b,"device_id")});
   }return result;
  }
  // Static public declaration from the exact selected observation. The user
  // reviews this candidate; current FULL/Q-only/NONE never determines support.
  internal static DiscoveryContract DeclaredContract(DiscoveryObservation row){
   var input=new List<string>(row.Claims);input.Sort(StringComparer.Ordinal);
   var known=new HashSet<string>(new[]{"position","orientation","linear_velocity","angular_velocity","linear_acceleration","battery_fraction","charging"},StringComparer.Ordinal);
   foreach(string token in input)if(!known.Contains(token))throw new InvalidOperationException("Unsupported declared capability: "+token);
   var output=new List<string>();foreach(string token in input){
    if((token=="linear_velocity"||token=="linear_acceleration")&&!input.Contains("position"))continue;
    if(token=="angular_velocity"&&!input.Contains("orientation"))continue;output.Add(token);
   }
   return new DiscoveryContract{MappingIndex=-1,Source=row.Source,Device=row.Device,Label="Use this device's declared static capabilities: "+string.Join(", ",input),Value=new Dictionary<string,object>{
    {"id","explicit-observation"},{"provenance","Explicit user selection of public static declaration; source="+row.Source+"; device="+row.Device+"; session="+row.Session+"; clock="+row.Clock},
    {"input_policy","STRICT"},{"output_policy","STRICT"},{"input_declaration",input.ToArray()},{"output_declaration",output.ToArray()}}};
  }
  internal static void RequireContract(DiscoveryContract contract,DiscoveryObservation row){
   if(contract==null||contract.Source!=row.Source||Text(contract.Value,"id")==""||Text(contract.Value,"provenance")=="")throw new InvalidOperationException("NEEDS_CONTRACT_REVIEW: select an explicit contract for this source");
   var input=Tokens(contract.Value["input_declaration"]);Tokens(contract.Value["output_declaration"]);var declared=new HashSet<string>(input,StringComparer.Ordinal);
   string policy=Text(contract.Value,"input_policy");if(policy=="")policy="STRICT";
   if((policy=="STRICT"&&!SetEqual(input,row.Claims))||(policy=="LEGACY_DYNAMIC"&&!declared.IsSupersetOf(row.Claims))||(policy!="STRICT"&&policy!="LEGACY_DYNAMIC"))throw new InvalidOperationException("NEEDS_CONTRACT_REVIEW: advertised claims outside the explicit contract/policy");
  }
  static Dictionary<string,object> Identity(){return new Dictionary<string,object>{{"rotation",new double[]{0,0,0,1}},{"translation",new double[]{0,0,0}}};}
  static string Json(object o){return new JavaScriptSerializer().Serialize(o);}
  internal static void RequireWritable(bool readOnly,bool active){if(readOnly)throw new InvalidOperationException("Read-only installation");if(active)throw new InvalidOperationException("Finish calibration, pending saves and reload/review first");}
  internal static void RequireOffline(DiscoverySnapshot snapshot,Dictionary<string,object> old){
   double received;if(snapshot.ConfiguredReceipts.TryGetValue(Tuple.Create(Text(old,"source_id"),Text(old,"device_id")),out received)&&received<3000)throw new InvalidOperationException("Old slot recently received a canonical packet; replacement rejected");
   var row=snapshot.Find(Text(old,"source_id"),Text(old,"device_id"));
   // Packet receipt, including invalid pose/state, means online. Validity is
   // deliberately not an offline criterion. Incomplete snapshots fail above.
   if(row!=null&&row.SeenAge>=0&&row.SeenAge<3000)throw new InvalidOperationException("Old slot is online; replacement rejected");
  }
  internal static DiscoveryDraft Prepare(Dictionary<string,object> loaded,DiscoverySnapshot snapshot,DiscoveryObservation selected,DiscoveryAction action,int oldIndex,string tracker,string profile,DiscoveryContract contract,bool readOnly,bool active){
   loaded=MappingEditor.WithDefaults(loaded);
   RequireWritable(readOnly,active);if(snapshot==null||!snapshot.Ready||selected==null)throw new InvalidOperationException(snapshot==null?"Select a current discovery observation":snapshot.Error??"Select a current discovery observation");
   var current=snapshot.Find(selected.Source,selected.Device);if(!selected.Same(current)||!current.Fresh)throw new InvalidOperationException("Discovery selection changed or stale; select and review again");
   RequireContract(contract,current);var mappings=Mappings(loaded);Dictionary<string,object> template=null;
   if(contract.MappingIndex==-1){if(contract.Device!=current.Device||Json(DeclaredContract(current).Value)!=Json(contract.Value))throw new InvalidOperationException("Declared static contract changed; select again");}
   else {if(contract.MappingIndex<0||contract.MappingIndex>=mappings.Count)throw new InvalidOperationException("Contract selection changed");
    template=(Dictionary<string,object>)mappings[contract.MappingIndex];if(Text(template,"source_id")!=current.Source||!template.ContainsKey("runtime_contract")||Json(template["runtime_contract"])!=Json(contract.Value))throw new InvalidOperationException("Contract selection changed");}
   var verified=MappingEditor.RequireProfile(loaded,profile);if(Text(verified,"convention")!=current.Convention)throw new InvalidOperationException("Select an explicitly defined matching coordinate profile");
   Dictionary<string,object> old=null;if(action!=DiscoveryAction.Create){if(oldIndex<0||oldIndex>=mappings.Count)throw new InvalidOperationException("Select one persisted slot explicitly");old=(Dictionary<string,object>)mappings[oldIndex];if(Text(old,"source_id")!=current.Source)throw new InvalidOperationException("Foreign source slot rejected");tracker=Text(old,"tracker_id");
    if(action==DiscoveryAction.Replace){if(Text(old,"device_id")==current.Device)throw new InvalidOperationException("Select an unmapped replacement device");RequireOffline(snapshot,old);}
    else if(Text(old,"device_id")!=current.Device||!current.Configured||Text(old,"input_space")==current.Space&&Convert.ToUInt32(old["input_revision"])==current.Revision)throw new InvalidOperationException("Select a known device with a changed input frame/revision");
   }
   if(action!=DiscoveryAction.UpdateRevision&&current.Configured)throw new InvalidOperationException("Device is already configured");
   if(string.IsNullOrWhiteSpace(tracker)||tracker.Length>128||tracker.IndexOfAny(new[]{'\r','\n','\0'})>=0)throw new InvalidOperationException("Enter an explicit unique logical tracker ID");
   for(int n=0;n<mappings.Count;++n){if(n==oldIndex&&action!=DiscoveryAction.Create)continue;var b=(Dictionary<string,object>)mappings[n];if(Text(b,"source_id")==current.Source&&(Text(b,"device_id")==current.Device||Text(b,"tracker_id")==tracker))throw new InvalidOperationException("Duplicate source/device or logical tracker assignment");}
   var baseDoc=MappingEditor.Clone(loaded);var list=new ArrayList((ICollection)baseDoc["mappings"]);if(old!=null)list.RemoveAt(oldIndex);baseDoc["mappings"]=list.ToArray();
   var destination=old??template??new Dictionary<string,object>{{"world_space","world"},{"world_revision",0}};
   var edit=new Dictionary<string,object>{{"source_id",current.Source},{"device_id",current.Device},{"tracker_id",tracker},{"profile",profile},{"input_space",current.Space},{"input_revision",current.Revision},{"world_space",destination["world_space"]},{"space_approved",false}};
   var candidate=MappingEditor.Candidate(baseDoc,-1,edit);var output=(IList)candidate["mappings"];var binding=(Dictionary<string,object>)output[output.Count-1];binding["runtime_contract"]=MappingEditor.Clone(contract.Value);
   binding["world"]=Identity();binding["world_revision"]=destination["world_revision"];binding["mount"]=Identity();
   // A shared W is reusable only for the exact source/frame/revision group.
   // No approval or physical-device Mount/evidence is inherited by a rebind.
   string shared=null;Dictionary<string,object> sharedBinding=null;
   foreach(Dictionary<string,object> b in mappings)if(Text(b,"source_id")==current.Source&&Text(b,"input_space")==current.Space&&Convert.ToUInt32(b["input_revision"])==current.Revision){
    // Native measurement groups require the exact profile ID, not just the
    // same convention or axes. Never reuse another profile's calibrated W.
    if(Text(b,"profile")!=profile)throw new InvalidOperationException("Shared W group profile mismatch; select the existing group profile");
    string value=Json(new object[]{b["world"],b["world_space"],b["world_revision"]});if(shared!=null&&shared!=value)throw new InvalidOperationException("Shared W group inconsistent; review alignment separately");shared=value;sharedBinding=b;
   }
   if(sharedBinding!=null){binding["world"]=MappingEditor.Clone((Dictionary<string,object>)sharedBinding["world"]);binding["world_space"]=sharedBinding["world_space"];binding["world_revision"]=sharedBinding["world_revision"];}
   if(action==DiscoveryAction.UpdateRevision){
    // Coordinate generation is diagnostic. This is the same physical identity;
    // saved geometry remains the user's interpretation until explicitly changed.
    binding["world"]=MappingEditor.Clone((Dictionary<string,object>)old["world"]);
    binding["world_space"]=old["world_space"];binding["world_revision"]=old["world_revision"];
    binding["mount"]=MappingEditor.Clone((Dictionary<string,object>)old["mount"]);
   }
   // Keep the selected slot's position in the UI/config; other rows are exact
   // cloned values, including extension fields. Candidate owns rev+1 once.
   if(old!=null){var ordered=new ArrayList((ICollection)candidate["mappings"]);ordered.RemoveAt(ordered.Count-1);ordered.Insert(oldIndex,binding);candidate["mappings"]=ordered.ToArray();}
   string previous=old==null?"(new slot)":Text(old,"device_id")+" | "+Text(old,"input_space")+" @"+Text(old,"input_revision");
   return new DiscoveryDraft{Document=candidate,Observation=current,Action=action,OldIndex=oldIndex,OldDevice=old==null?null:Text(old,"device_id"),Tracker=tracker,Profile=profile,Contract=contract,
    Confirmation="Source: "+current.Source+"\nDevice: "+previous+" -> "+current.Device+"\nInput: "+current.Space+" @"+current.Revision+" | "+current.Convention+"\nPublisher/source/tracker: "+Text(loaded,"bridge_id")+" / "+current.Source+" / "+tracker+"\nContract: "+Text(contract.Value,"id")+" | "+Text(contract.Value,"input_policy")+"\nDeclaration: "+string.Join(", ",Tokens(contract.Value["input_declaration"]))+"\nProvenance: "+Text(contract.Value,"provenance")+"\nOnly this binding changes. "+(action==DiscoveryAction.UpdateRevision?"Saved World and Mount are retained for this physical device.":"Uncalibrated World/Mount use Identity unless this source already has a saved shared World.")+"\nSave uses native exact-snapshot CAS and backup. No physical quality approval is required. Runtime/output application remains unconfirmed."};
  }
  internal static void Recheck(Dictionary<string,object> loaded,Dictionary<string,object> health,DiscoveryDraft draft,bool readOnly,bool active,DateTime now){
   var next=Prepare(loaded,Read(loaded,health,now),draft.Observation,draft.Action,draft.OldIndex,draft.Tracker,draft.Profile,draft.Contract,readOnly,active);
   if(Json(next.Document)!=Json(draft.Document))throw new InvalidOperationException("Draft changed; review again");
  }
  internal static bool Applied(DiscoveryDraft draft,Dictionary<string,object> health,DateTime now){
   var snapshot=Read(draft.Document,health,now);if(!snapshot.Ready)return false;var r=snapshot.Find(draft.Observation.Source,draft.Observation.Device);if(r==null||!r.Fresh||!r.Configured||r.Reason!="CONFIGURED"||r.Space!=draft.Observation.Space||r.Revision!=draft.Observation.Revision)return false;
   var devices=health["devices"] as IList;if(devices==null)return false;foreach(Dictionary<string,object> d in devices)if(Text(d,"publisher_id")==Text(draft.Document,"bridge_id")&&Text(d,"source")==r.Source&&Text(d,"device")==r.Device&&Text(d,"tracker")==draft.Tracker&&Text(d,"space")==r.Space&&Text(d,"revision")==r.Revision.ToString(CultureInfo.InvariantCulture))return true;return false;
  }
 }
}
