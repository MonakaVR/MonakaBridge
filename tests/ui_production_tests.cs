using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using MonakaBridge;
class Tests {
 static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
 static Dictionary<string,object> Edit(string source,string device,string tracker){return new Dictionary<string,object>{
  {"source_id",source},{"device_id",device},{"tracker_id",tracker},{"profile","vive-hil-v1"},
  {"input_space","test-native"},{"input_revision",1},{"world_space","test-world"},{"space_approved",true}};}
 static IList Rows(Dictionary<string,object> c){return (IList)c["mappings"];}
 static Dictionary<string,object> Row(Dictionary<string,object> c,int i){return (Dictionary<string,object>)Rows(c)[i];}
 static void Reject(Action action){bool failed=false;try{action();}catch(InvalidOperationException){failed=true;}Check(failed,"Expected mapping collision/rejection");}
 static void Validate(Dictionary<string,object> c,string directory,string executable,int number){
  string path=Path.Combine(directory,"mapping-"+number+".json");File.WriteAllText(path,new JavaScriptSerializer().Serialize(c),new UTF8Encoding(false));
  using(var p=Process.Start(new ProcessStartInfo(executable,"\""+path+"\" validate"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true})){
   string output=p.StandardOutput.ReadToEnd(),error=p.StandardError.ReadToEnd();p.WaitForExit();Check(p.ExitCode==0,"Native config validation: "+output+error);
  }
 }
 static void StateSync(Dictionary<string,object> initial,string directory,string executable){
  var state=new MappingEditorState();Dictionary<string,object> rendered=null;bool checkbox=false;
  Action<Dictionary<string,object>> render=delegate(Dictionary<string,object> row){
   rendered=row;
   // Each programmatic TextBox update fires the same identity event as an edit.
   foreach(string field in new[]{"source_id","device_id","input_space","input_revision"})
    Check(!state.IdentityEdited(),"Loading "+field+" must suppress identity invalidation");
   checkbox=state.Approved;
  };
  state.Select(Rows(initial),0,render);
  Check(state.Approved&&checkbox&&state.SelectedIndex==0,"Persisted approval survives programmatic selection");
  foreach(string field in new[]{"source_id","device_id","input_space","input_revision"}){
   state.Select(Rows(initial),0,render);
   Check(state.IdentityEdited()&&!state.Approved,"User edit of "+field+" clears approval");
  }
  try{state.Programmatic(delegate{state.Programmatic(delegate{Check(state.Updating,"Nested suppression");});throw new IOException("render failed");});}catch(IOException){}
  Check(!state.Updating&&state.IdentityEdited(),"Exception must not leave suppression active");
  state.Select(Rows(initial),0,render);
  var selectedKey=state.SelectedKey;
  var reordered=MappingEditor.Candidate(initial,-1,Edit("source-b","device-a","tracker"));
  var list=Rows(reordered);var first=list[0];list[0]=list[1];list[1]=first;
  state.Restore(list,selectedKey,render);
  Check(state.SelectedIndex==1&&state.Approved&&(string)rendered["source_id"]=="source-a","Reload restores stable input key, not index or device alone");

  // Real native validation -> atomic replacement -> disk read -> key reselection.
  var edit=Edit("source-new","device-new","renamed");
  var saved=MappingEditor.Candidate(reordered,state.SelectedIndex,edit);
  Check(Convert.ToUInt32(saved["mapping_revision"])==Convert.ToUInt32(reordered["mapping_revision"])+1,"Revision increments once per save");
  Validate(saved,directory,executable,90);
  var json=new JavaScriptSerializer();string target=Path.Combine(directory,"state-sync-config.json");
  File.WriteAllText(target,json.Serialize(reordered),new UTF8Encoding(false));
  string candidate=Path.Combine(directory,"mapping-90.json");
  File.Replace(candidate,target,Path.Combine(directory,"state-sync-backup.json"));
  state.Approved=false; // UI state cannot be the authority after a save.
  var disk=SteamVrControlWpf.Read(target);
  state.Restore(Rows(disk),MappingEditorState.Key(edit),render);
  Check(state.SelectedIndex==1&&state.Approved&&checkbox&&(string)rendered["device_id"]=="device-new","Save/reload restores the saved mapping and persisted true approval");
  Row(disk,1)["space_approved"]=false;File.WriteAllText(target,json.Serialize(disk),new UTF8Encoding(false));
  state.Approved=true;
  state.Restore(Rows(SteamVrControlWpf.Read(target)),MappingEditorState.Key(edit),render);
  Check(!state.Approved&&!checkbox,"Persisted false overrides a checked UI value too");
  state.Restore(Rows(initial),MappingEditorState.Key(edit),render);
  Check(state.SelectedIndex==-1&&state.SelectedKey==null&&!state.Approved&&!checkbox&&rendered==null,"Disappeared selection clears target, fields and approval");
  state.Select(Rows(initial),0,render);state.Select(Rows(initial),-1,render);
  Check(rendered==null&&!state.Approved&&state.SelectedKey==null,"Explicit deselection clears stale fields");

  foreach(string name in new[]{null,"","missing-profile"}){
   try{MappingEditor.RequireProfile(initial,name);throw new Exception("Unknown profile allowed");}
   catch(InvalidOperationException e){Check(e.Message.Contains("Select an existing profile"),"Controlled user-facing profile error");}
  }
  var unknown=Edit("source-a","device-a","tracker");unknown["profile"]="missing-profile";
  Reject(delegate{MappingEditor.Candidate(initial,0,unknown);});
  MappingEditor.RequireProfile(initial,"vive-hil-v1");
  Check(!state.Approved,"Selecting an approved profile must not approve input space");

  Check(HealthPresentation.MappingRevision(4,4,true,true).Contains("match (diagnostic"),"Current applied revision matches");
  Check(HealthPresentation.MappingRevision(4,3,true,true).Contains("different (diagnostic"),"Current applied revision differs");
  foreach(var display in new[]{HealthPresentation.MappingRevision(4,3,false,true),HealthPresentation.MappingRevision(4,4,false,true),HealthPresentation.MappingRevision(4,null,true,true),HealthPresentation.MappingRevision(4,4,true,false)})
   Check(display.Contains("unavailable / historical")&&display.Contains("status unknown")&&!display.Contains("different (diagnostic"),"Stale/unavailable/wrong publisher health cannot decide application");
  Console.WriteLine("PASS GUI state sync: programmatic/user edits, persisted approval, atomic save/reload, stable key/reorder/disappearance, profile validation, diagnostic applied revision");
 }
 static object Field(object window,string name){return window.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(window);}
 static object Property(object control,string name){return control.GetType().GetProperty(name).GetValue(control,null);}
 static void Set(object control,string name,object value){control.GetType().GetProperty(name).SetValue(control,value,null);}
 static void Invoke(object window,string method){window.GetType().GetMethod(method,BindingFlags.Instance|BindingFlags.NonPublic,null,Type.EmptyTypes,null).Invoke(window,null);}
 static void WpfEvents(Dictionary<string,object> initial,string directory,string executable){
  // Instantiate controls on STA without showing a window or pumping timers. All
  // config/native validation writes are confined to this synthetic test root.
  string root=Path.Combine(directory,"state-sync-window"),target=Path.Combine(root,"config","bridge.json");
  Directory.CreateDirectory(Path.GetDirectoryName(target));Directory.CreateDirectory(Path.Combine(root,"build","Release"));
  File.Copy(executable,Path.Combine(root,"build","Release","monaka_bridge_config.exe"),true);
  var json=new JavaScriptSerializer();File.WriteAllText(target,json.Serialize(initial),new UTF8Encoding(false));
  var type=typeof(SteamVrControlWpf).GetNestedType("ControlWindow",BindingFlags.NonPublic);
  object window=Activator.CreateInstance(type,new object[]{root});
  try{
   Invoke(window,"Reload");
   var mappings=Field(window,"mappings");var approval=Field(window,"approved");var profile=Field(window,"profile");
   Check(Property(profile,"SelectedItem")==null,"New form requires explicit profile selection");
   Set(mappings,"SelectedIndex",0);
   Check((bool)Property(approval,"IsChecked"),"WPF selection preserves persisted approval");
   foreach(string name in new[]{"source","device","space","revision"}){
    Set(Field(window,name),"Text",name=="revision"?"2":"edited-"+name);
    Check(!(bool)Property(approval,"IsChecked"),"WPF user edit clears approval: "+name);
    Invoke(window,"Reload");Check((bool)Property(approval,"IsChecked"),"Reload restores approval and selected key: "+name);
   }
   Set(approval,"IsChecked",false);Set(profile,"SelectedItem","vive-unverified");
   Check(!(bool)Property(approval,"IsChecked"),"WPF profile selection does not approve space");
   Set(profile,"SelectedIndex",-1);
   try{Invoke(window,"SaveMapping");throw new Exception("Missing profile saved");}
   catch(TargetInvocationException e){Check(e.InnerException is InvalidOperationException&&e.InnerException.Message.Contains("Select an existing profile"),"WPF save has a controlled profile error");}
   Set(profile,"SelectedItem","vive-hil-v1");Set(Field(window,"source"),"Text","rebound-source");Set(approval,"IsChecked",true);
   Invoke(window,"SaveMapping");
   var disk=SteamVrControlWpf.Read(target);
   Check(Rows(disk).Count==1&&(string)Row(disk,0)["source_id"]=="rebound-source"&&(bool)Row(disk,0)["space_approved"],"WPF atomic save persists rebound mapping");
   Check((int)Property(mappings,"SelectedIndex")==0&&(bool)Property(approval,"IsChecked")&&(string)Property(Field(window,"source"),"Text")=="rebound-source","WPF save reload reselects new key and persisted approval");
   var observations=(ArrayList)Field(window,"observations");observations.Add(new Dictionary<string,object>{{"source","rebound-source"},{"device","device-a"},{"space","test-native"},{"revision",1},{"convention","test"}});
   var devices=Field(window,"devices");var items=(IList)Property(devices,"Items");items.Add("synthetic observation");Set(devices,"SelectedIndex",0);
   Invoke(window,"SelectObservation");Check(!(bool)Property(approval,"IsChecked"),"Observation adoption clears approval even for unchanged identity values");
   Row(disk,0)["profile"]="missing-profile";File.WriteAllText(target,json.Serialize(disk),new UTF8Encoding(false));Invoke(window,"Reload");
   Check(Property(profile,"SelectedItem")==null&&((string)Property(Field(window,"status"),"Text")).Contains("Unknown profile"),"Unknown persisted profile does not retain previous selection");
   disk["mappings"]=new object[0];File.WriteAllText(target,json.Serialize(disk),new UTF8Encoding(false));Invoke(window,"Reload");
   Check((int)Property(mappings,"SelectedIndex")==-1&&!(bool)Property(approval,"IsChecked")&&Property(profile,"SelectedItem")==null,"Missing mapping clears selection/profile/approval");
   foreach(string name in new[]{"source","device","tracker","space","revision","world"})Check((string)Property(Field(window,name),"Text")=="","Missing mapping clears WPF field: "+name);
  }finally{type.GetMethod("Close").Invoke(window,null);}
  Console.WriteLine("PASS WPF event wiring: hidden STA controls, save/reload/native validation, observation adoption, unknown profile and missing mapping; interactive UI NOT RUN");
 }
 [STAThread]
 static int Main(string[] args){try{
  var original=SteamVrControlWpf.Read(args[2]);original["bridge_id"]="test-publisher";
  var created=MappingEditor.Candidate(original,-1,Edit("source-a","device-a","tracker"));
  StateSync(created,args[0],args[1]);
  WpfEvents(created,args[0],args[1]);
  Check(Rows(original).Count==0&&Rows(created).Count==1,"New mapping must not mutate loaded config");
  var changed=MappingEditor.Candidate(created,0,Edit("source-a","device-b","tracker"));
  Check(Rows(changed).Count==1&&(string)Row(changed,0)["device_id"]=="device-b","Selected mapping rebind must replace, not append");
  Check((string)Row(created,0)["device_id"]=="device-a","Candidate edit must be transactional");
  var repeat=MappingEditor.Candidate(changed,0,Edit("source-a","device-b","tracker"));Check(Rows(repeat).Count==1,"Repeated rebind must not duplicate");
  var deselected=MappingEditor.Candidate(changed,-1,Edit("source-b","device-b","tracker"));
  Check(Rows(deselected).Count==2&&(string)Row(deselected,0)["source_id"]=="source-a","Cleared selection creates a distinct source mapping");
  var exact=MappingEditor.Candidate(deselected,-1,Edit("source-b","device-b","renamed"));
  Check(Rows(exact).Count==2&&(string)Row(exact,1)["tracker_id"]=="renamed","Unselected exact input updates without duplicate");
  Reject(delegate{MappingEditor.Candidate(changed,-1,Edit("source-a","device-c","tracker"));});
  Reject(delegate{MappingEditor.Candidate(deselected,0,Edit("source-b","device-b","new-name"));});
  Reject(delegate{MappingEditor.Candidate(changed,42,Edit("source-a","device-c","tracker"));});
  var sourceChange=MappingEditor.Candidate(changed,0,Edit("source-new","device-b","tracker"));
  Check(Rows(sourceChange).Count==1&&(string)Row(sourceChange,0)["source_id"]=="source-new","Explicit source change replaces mapping");
  var id=MappingEditor.Identity("publisher-a","source-a","tracker");
  Check(!id.Equals(MappingEditor.Identity("publisher-a","source-b","tracker")),"Source is part of identity");
  Check(!id.Equals(MappingEditor.Identity("publisher-b","source-a","tracker")),"Publisher is part of identity");
  var otherPublisher=MappingEditor.Clone(deselected);otherPublisher["bridge_id"]="publisher-b";
  int n=0;foreach(var c in new[]{created,changed,repeat,deselected,exact,sourceChange,otherPublisher})Validate(c,args[0],args[1],n++);
  string ambiguous=Path.Combine(args[0],"mapping-3.json"),before=File.ReadAllText(ambiguous);
  foreach(string command in new[]{"align tracker 1 2 3","rename tracker renamed"}){
   using(var p=Process.Start(new ProcessStartInfo(args[1],"\""+ambiguous+"\" "+command){UseShellExecute=false,CreateNoWindow=true,RedirectStandardError=true})){
    string error=p.StandardError.ReadToEnd();p.WaitForExit();Check(p.ExitCode!=0&&error.Contains("ambiguous tracker"),"CLI must reject ambiguous shorthand");
   }
   Check(File.ReadAllText(ambiguous)==before,"Rejected CLI must not change config");
  }
  Console.WriteLine("PASS mapping: edit/create/deselect/rebind/collisions/source identity/publisher identity; native config validation");

  // A stuck/failing disk read never runs on the caller/UI thread or grows jobs.
  var entered=new ManualResetEvent(false);var release=new ManualResetEvent(false);var posted=new ManualResetEvent(false);
  Action delivered=null;int calls=0;bool applied=false;
  using(var reader=new HealthReader(delegate(string path){Interlocked.Increment(ref calls);entered.Set();release.WaitOne(5000);throw new IOException("injected read failure");})){
   Check(reader.Request("unused",delegate(Action a){delivered=a;posted.Set();},delegate(HealthReadResult r){applied=true;Check(r.Error!=null,"Read failure must be reported");}),"First read queued");
   Check(entered.WaitOne(2000),"Reader entered");
   var timer=Stopwatch.StartNew();int uiUpdates=0;
   for(int i=0;i<1000;++i){Check(!reader.Request("unused",delegate(Action a){},delegate(HealthReadResult r){}),"Only one in-flight read");++uiUpdates;}
   Check(uiUpdates==1000&&timer.ElapsedMilliseconds<1000&&!applied&&calls==1,"UI updates continue while reader blocked");
   release.Set();Check(posted.WaitOne(2000),"Completion posted");Check(!applied,"Completion must use UI dispatcher");delivered();Check(applied,"Error delivered on dispatcher");
  }
  var queued=new ManualResetEvent(false);Action late=null;bool lateApplied=false;
  var closing=new HealthReader(delegate(string path){return new HealthReadResult();});
  closing.Request("unused",delegate(Action a){late=a;queued.Set();},delegate(HealthReadResult r){lateApplied=true;});
  Check(queued.WaitOne(2000),"Close case queued");closing.Dispose();late();Check(!lateApplied,"Closed window must discard completion");
  string health=Path.Combine(args[0],"health.json");
  File.WriteAllText(health,"{\"policy\":\"both\",\"snapshot_unix_ms\":1000,\"devices\":[]}");
  var read=HealthReader.ReadOnce(health);Check((string)read.Value["policy"]=="both","Current policy retained");
  var current=read.SnapshotUtc.AddSeconds(1);Check(HealthPresentation.Current(read.SnapshotUtc,current),"Current snapshot");
  Check(!HealthPresentation.Current(read.SnapshotUtc,current.AddSeconds(3)),"Delayed snapshot is historical");
  Check(!HealthPresentation.Current(current.AddSeconds(10),current),"Future diagnostic clock fails closed");
  var pose=new Dictionary<string,object>{{"pose_stage","native_observation"},{"space","native-map"},{"convention","vut-native-v1"},{"revision",7},
   {"fresh",true},{"pose_age_ms",100},{"position_valid",true},{"orientation_valid",false},{"tracking_state","degraded"},{"position",new double[]{1,2,3}},{"orientation_xyzw",null}};
  string text=HealthPresentation.Pose(pose,true);Check(text.Contains("native_observation")&&text.Contains("native-map")&&text.Contains("vut-native-v1")&&text.Contains("revision=7")&&text.Contains("not final SteamVR output"),"Pose stage/space must be explicit");
  Check(text.Contains("fresh")&&text.Contains("position valid=True")&&text.Contains("orientation valid=False"),"Separate freshness and sample validity");
  Check(HealthPresentation.Pose(pose,false).Contains("unavailable / historical"),"Read failure must not present retained pose as live");
  Check(HealthPresentation.Pose(pose,true,400,500).Contains("stale"),"Delayed snapshot must age the pose using the configured timeout");
  pose["fresh"]=false;Check(HealthPresentation.Pose(pose,true).Contains("stale"),"Stale pose");
  pose["position"]=new object[]{"malformed",2,3};Check(HealthPresentation.Pose(pose,true).Contains("unavailable"),"Malformed diagnostic components must not escape onto UI dispatcher");
  Console.WriteLine("PASS GUI health: single background read, failure delivery, nonblocking UI updates, close, policy, native/stale/component presentation; interactive UI NOT RUN");
  return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
