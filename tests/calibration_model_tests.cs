using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text;
using MonakaBridge;
class CalibrationModelTests {
 static int cases;static string directory;static CalibrationInstallation profile;
 static void Check(bool ok){if(!ok)throw new Exception("model assertion");}
 static void Reject(Action action){try{action();}catch(InvalidOperationException){cases++;return;}throw new Exception("Expected model rejection");}
 static byte[] Bytes(Dictionary<string,object> value){return CalibrationJson.Encode(value,65536);}
 static Dictionary<string,object> Version(){return new Dictionary<string,object>{{"api_version",1},{"software_policy",1},{"gui_contract",1},{"capture_supported",true},{"static_supported",false},{"production_capture","OPENVR_BACKGROUND_MIRROR"},{"standing_policy","EXACT_FINITE_NUMERIC"},{"runtime_validation","NOT_RUN"},{"output_approval",false},{"features",new[]{"idle_invalidation","phase_events","witness_details","immutable_result_commit"}}};}
 static object Rigid(){return new Dictionary<string,object>{{"translation_m",new[]{1.23456789012345,2.0,3.0}},{"rotation_xyzw",new[]{0.0,0.0,0.0,1.0}}};}
 static Dictionary<string,object> Result(){return new Dictionary<string,object>{{"status","RESULT_READY"},{"result",new Dictionary<string,object>{{"result_id","native-result"},{"config_sha256",new string('a',64)},{"world",Rigid()},{"extrinsic_not_mount",Rigid()},{"mount_existing",Rigid()}}}};}
 static Dictionary<string,object> Prepared(string path,byte[] candidate){return new Dictionary<string,object>{{"status","PREPARED"},{"result_id","native-result"},{"review_token","native-review"},{"candidate_sha256",CalibrationInstallation.Sha256(candidate)},{"candidate_bytes",candidate.Length},{"candidate_path",path},{"output_approval","NOT_REQUIRED"}};}
 static int Main(){try{
  directory=Path.Combine(Path.GetTempPath(),"monaka-u1-model-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(directory);string hash=new string('a',64);
  var doc=new Dictionary<string,object>{{"schema",CalibrationInstallation.Schema},{"name","model fixture"},{"config","config.json"},{"health","health.json"},{"config_helper","config.exe"},{"helper_sha256",hash},{"read_only",true},{"calibration_helper","native.exe"},{"calibration_helper_sha256",hash},{"calibration_api",1},{"calibration_software_policy",1},{"calibration_gui_contract",1},{"bridge_id","fixture"},{"installation_path",directory},{"association",CalibrationInstallation.Association},{"single_bridge_no_relay",true},{"mirror_address","127.0.0.1"},{"mirror_port",29813},{"source","pico"},{"device","device"},{"tracker","hip"},{"input_space","native"},{"input_revision",1},{"reference_role","left"}};
  profile=CalibrationInstallation.Parse(Path.Combine(directory,"profile.json"),Bytes(doc));CalibrationContract.VerifyVersion(profile,Bytes(Version()));cases++;
  foreach(string name in new[]{"gui_contract","features"}){var old=Version();old.Remove(name);Reject(delegate{CalibrationContract.VerifyVersion(profile,Bytes(old));});}
  foreach(string name in new[]{"api_version","software_policy","gui_contract"}){var wrong=Version();wrong[name]=2;Reject(delegate{CalibrationContract.VerifyVersion(profile,Bytes(wrong));});}
  foreach(var mutation in new[]{new KeyValuePair<string,object>("capture_supported",false),new KeyValuePair<string,object>("output_approval",true),new KeyValuePair<string,object>("production_capture","UNSUPPORTED"),new KeyValuePair<string,object>("standing_policy","APPROXIMATE"),new KeyValuePair<string,object>("unknown",true),new KeyValuePair<string,object>("features",new[]{"phase_events"}),new KeyValuePair<string,object>("features",new[]{"idle_invalidation","phase_events","witness_details","immutable_result_commit","phase_events"})}){var wrong=Version();wrong[mutation.Key]=mutation.Value;Reject(delegate{CalibrationContract.VerifyVersion(profile,Bytes(wrong));});}
  byte[] raw=Bytes(Result());var result=new CalibrationNativeResult(raw);Check(result.NativeJson==CalibrationJson.Utf8.GetString(raw)&&result.ResultId=="native-result");cases++;
  var display=result.DisplayCopy();var nested=(Dictionary<string,object>)display["result"];nested["result_id"]="tampered";((Dictionary<string,object>)nested["world"])["translation_m"]=new[]{999.0,0,0};
  Check(result.ResultId=="native-result"&&CalibrationJson.Text((Dictionary<string,object>)result.DisplayCopy()["result"],"result_id",4096)=="native-result");cases++;
  var noMount=Result();((Dictionary<string,object>)noMount["result"]).Remove("mount_existing");Reject(delegate{new CalibrationNativeResult(Bytes(noMount));});
  var rejected=Result();rejected["status"]="REJECTED";Reject(delegate{new CalibrationNativeResult(Bytes(rejected));});
  string candidatePath=Path.Combine(directory,"candidate.json");byte[] candidate=CalibrationJson.Utf8.GetBytes("{\"native-exact\":1.234567890123456789,\"mapping_revision\":2}");File.WriteAllBytes(candidatePath,candidate);var prepared=new CalibrationPreparedCandidate(result,Bytes(Prepared(candidatePath,candidate)));
  bool truncated;Check(prepared.ReadPreview(out truncated)==CalibrationJson.Utf8.GetString(candidate)&&!truncated);cases++;
  var fields=prepared.CommitFields();Check(fields.Count==3&&fields["result_id"].Equals(result.ResultId)&&fields["candidate_sha256"].Equals(CalibrationInstallation.Sha256(candidate)));fields["result_id"]="wrong";fields["world"]=new[]{0,0,0};var stable=prepared.CommitFields();Check(stable.Count==3&&stable["result_id"].Equals("native-result")&&!stable.ContainsKey("world")&&Convert.ToBase64String(candidate)==Convert.ToBase64String(File.ReadAllBytes(candidatePath)));cases++;
  var other=Prepared(candidatePath,candidate);other["result_id"]="foreign";Reject(delegate{new CalibrationPreparedCandidate(result,Bytes(other));});
  var blank=Prepared(candidatePath,candidate);blank["review_token"]="";Reject(delegate{new CalibrationPreparedCandidate(result,Bytes(blank));});
  var approved=Prepared(candidatePath,candidate);approved["output_approval"]="APPROVED";Reject(delegate{new CalibrationPreparedCandidate(result,Bytes(approved));});
  File.AppendAllText(candidatePath," ");Reject(delegate{bool t;prepared.ReadPreview(out t);});File.WriteAllBytes(candidatePath,candidate);byte[] changed=(byte[])candidate.Clone();changed[0]=(byte)'[';File.WriteAllBytes(candidatePath,changed);Reject(delegate{bool t;prepared.ReadPreview(out t);});
  byte[] big=CalibrationJson.Utf8.GetBytes("{\"preview\":\""+new string('a',70000)+"\"}");File.WriteAllBytes(candidatePath,big);var large=new CalibrationPreparedCandidate(result,Bytes(Prepared(candidatePath,big)));Check(large.ReadPreview(out truncated).Length==65536&&truncated);cases++;
  var excess=Prepared(candidatePath,big);excess["candidate_bytes"]=2*1024*1024+1;Reject(delegate{new CalibrationPreparedCandidate(result,Bytes(excess));});
  Console.WriteLine("PASS calibration immutable native model "+cases+" cases; native helper extension is separately runtime-tested; no GUI/SDK/socket/live config");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
