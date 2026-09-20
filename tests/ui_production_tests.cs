using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
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
 static int Main(string[] args){try{
  var original=SteamVrControlWpf.Read(args[2]);original["bridge_id"]="test-publisher";
  var created=MappingEditor.Candidate(original,-1,Edit("source-a","device-a","tracker"));
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
