using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;

namespace MonakaBridge {
 internal static class CalibrationContract {
  internal const int LineBytes=17*1024;
   internal static Dictionary<string,object> VerifyVersion(CalibrationInstallation profile,byte[] bytes) {
   var value=CalibrationJson.Object(bytes,LineBytes,4096);
   CalibrationJson.Keys(value,new[]{"api_version","software_policy","gui_contract","capture_supported","static_supported","production_capture","standing_policy","runtime_validation","output_approval","features"},null);
   if(CalibrationJson.Integer(value,"api_version",1,1)!=profile.ApiVersion||CalibrationJson.Integer(value,"software_policy",1,1)!=profile.SoftwarePolicy||CalibrationJson.Integer(value,"gui_contract",1,1)!=profile.GuiContract)throw new InvalidOperationException("Unsupported calibration helper contract");
    if(!CalibrationJson.Boolean(value,"capture_supported"))throw new InvalidOperationException("Required capture helper capability unavailable");
   CalibrationJson.Boolean(value,"static_supported");
   if(CalibrationJson.Text(value,"production_capture",128)!="OPENVR_BACKGROUND_MIRROR"||CalibrationJson.Text(value,"standing_policy",128)!="EXACT_FINITE_NUMERIC")throw new InvalidOperationException("Unsupported calibration provider/Standing policy");
   CalibrationJson.Text(value,"runtime_validation",128);
   var features=value["features"] as IList;if(features==null)throw new InvalidOperationException("Helper feature list required");
    var known=new HashSet<string>(new[]{"idle_invalidation","phase_events","witness_details","immutable_result_commit","static_points_policy1","independent_output_review","max_residuals_v1","first_failure_v1","file_output_authority_v1"},StringComparer.Ordinal);var actual=new HashSet<string>(StringComparer.Ordinal);
   foreach(object feature in features){if(!(feature is string)||!known.Contains((string)feature)||!actual.Add((string)feature))throw new InvalidOperationException("Unsupported/duplicate calibration helper feature");}
    foreach(var required in new[]{"idle_invalidation","phase_events","witness_details","immutable_result_commit"})if(!actual.Contains(required))throw new InvalidOperationException("Required calibration helper features missing");
    if(actual.Contains("static_points_policy1")!=CalibrationJson.Boolean(value,"static_supported")||actual.Contains("independent_output_review")!=CalibrationJson.Boolean(value,"output_approval"))throw new InvalidOperationException("B4 feature/capability mismatch");
    return value;
  }
 }
  internal sealed class CalibrationOutputReview {
   internal readonly string ApprovalId,ConfigSha256,NativeJson;
   internal CalibrationOutputReview(byte[] line){var value=CalibrationJson.Object(line,CalibrationContract.LineBytes,32768);if(CalibrationJson.Text(value,"status",128)!="APPROVAL_REVIEW")throw new InvalidOperationException("Native output review required");ApprovalId=CalibrationJson.Text(value,"approval_id",4096);ConfigSha256=CalibrationJson.Hash(value,"config_sha256");if(!(value["members"] is IList))throw new InvalidOperationException("Native approval members required");var seen=new HashSet<string>(StringComparer.Ordinal);foreach(object raw in (IList)value["members"]){var member=raw as Dictionary<string,object>;if(member==null||!seen.Add(CalibrationJson.Text(member,"device",4096))||CalibrationJson.Boolean(member,"selected"))throw new InvalidOperationException("Invalid/duplicate/default-selected native approval member");CalibrationJson.Boolean(member,"eligible");CalibrationJson.Boolean(member,"approved");CalibrationJson.Text(member,"reason",4096);}NativeJson=CalibrationJson.Utf8.GetString(line);}
   internal Dictionary<string,object> DisplayCopy(){return CalibrationJson.Object(CalibrationJson.Utf8.GetBytes(NativeJson),CalibrationContract.LineBytes,32768);}
  }
 // Verbatim native response is the display/evidence source. Values parsed for
 // presentation are copies; C# never constructs a world/extrinsic for commit.
 internal sealed class CalibrationNativeResult {
   internal readonly string ResultId,ConfigSha256,NativeJson;internal readonly bool Static;
   internal readonly double? MaxPosition,MaxOrientation,AllMaxPosition,AllMaxOrientation;
   static double? Metric(Dictionary<string,object> value,string key){object raw;if(!value.TryGetValue(key,out raw)||raw==null)return null;double n=Convert.ToDouble(raw,System.Globalization.CultureInfo.InvariantCulture);if(Double.IsNaN(n)||Double.IsInfinity(n)||n<0)throw new InvalidOperationException("Invalid native residual");return n;}
  internal CalibrationNativeResult(byte[] line) {
   var response=CalibrationJson.Object(line,CalibrationContract.LineBytes,32768);
   if(CalibrationJson.Text(response,"status",128)!="RESULT_READY")throw new InvalidOperationException("Native result required");
   object raw;if(!response.TryGetValue("result",out raw)||!(raw is Dictionary<string,object>))throw new InvalidOperationException("Native result object required");
   var value=(Dictionary<string,object>)raw;ResultId=CalibrationJson.Text(value,"result_id",4096);ConfigSha256=CalibrationJson.Hash(value,"config_sha256");
    Static=value.ContainsKey("mode")&&CalibrationJson.Text(value,"mode",128)=="static";
    foreach(string key in new[]{"world","mount_existing"})if(!value.ContainsKey(key)||!(value[key] is Dictionary<string,object>))throw new InvalidOperationException("Native W/M display missing: "+key);
    if(!value.ContainsKey("extrinsic_not_mount")||(Static?value["extrinsic_not_mount"]!=null:!(value["extrinsic_not_mount"] is Dictionary<string,object>)))throw new InvalidOperationException("Native E display contract changed");
    if(Static&&(!value.ContainsKey("rms_orientation")||value["rms_orientation"]!=null))throw new InvalidOperationException("Static orientation residual must be not estimated");
   MaxPosition=Metric(value,"max_position");MaxOrientation=Metric(value,"max_orientation");
   object all;if(value.TryGetValue("all_residuals",out all)){var metrics=all as Dictionary<string,object>;if(metrics==null)throw new InvalidOperationException("Native all-pair residuals required");AllMaxPosition=Metric(metrics,"max_position");AllMaxOrientation=Metric(metrics,"max_orientation");}
   NativeJson=CalibrationJson.Utf8.GetString(line);
  }
  internal Dictionary<string,object> DisplayCopy(){return CalibrationJson.Object(CalibrationJson.Utf8.GetBytes(NativeJson),CalibrationContract.LineBytes,32768);}
 }
 internal sealed class CalibrationPreparedCandidate {
  internal readonly string ResultId,ReviewToken,CandidateSha256,CandidatePath,NativeJson;
  internal readonly long CandidateBytes;
   internal readonly bool OutputApproval;
   internal CalibrationPreparedCandidate(CalibrationNativeResult result,byte[] line):this(result,null,line){}
   internal CalibrationPreparedCandidate(CalibrationNativeResult result,CalibrationOutputReview approval,byte[] line) {
    OutputApproval=approval!=null;if(result==null&&!OutputApproval)throw new InvalidOperationException("Same native result required");
   var value=CalibrationJson.Object(line,CalibrationContract.LineBytes,32768);
    if(CalibrationJson.Text(value,"status",128)!=(OutputApproval?"OUTPUT_PREPARED":"PREPARED"))throw new InvalidOperationException("Native prepared candidate required");
    ResultId=CalibrationJson.Text(value,OutputApproval?"approval_id":"result_id",4096);if(ResultId!=(OutputApproval?approval.ApprovalId:result.ResultId))throw new InvalidOperationException("Native result/review identity changed");
   ReviewToken=CalibrationJson.Text(value,"review_token",4096);CandidateSha256=CalibrationJson.Hash(value,"candidate_sha256");
   CandidatePath=GuiLaunchOptions.Absolute(CalibrationJson.Text(value,"candidate_path",32768),"candidate_path");CandidateBytes=CalibrationJson.Integer(value,"candidate_bytes",1,2*1024*1024);
    if(!OutputApproval&&CalibrationJson.Text(value,"output_approval",128)!="REQUIRED")throw new InvalidOperationException("Native output reapproval contract changed");
    if(OutputApproval&&CalibrationJson.Text(value,"world_extrinsic_mount",128)!="UNCHANGED")throw new InvalidOperationException("Output approval must preserve W/E/M");
   NativeJson=CalibrationJson.Utf8.GetString(line);
  }
   internal Dictionary<string,object> CommitFields(){return new Dictionary<string,object>{{OutputApproval?"approval_id":"result_id",ResultId},{"review_token",ReviewToken},{"candidate_sha256",CandidateSha256}};}
  internal string ReadPreview(out bool truncated) {
   byte[] raw;
   using(var file=new FileStream(CandidatePath,FileMode.Open,FileAccess.Read,FileShare.Read)) {
    if(file.Length!=CandidateBytes)throw new InvalidOperationException("Native candidate size changed");
    raw=new byte[(int)CandidateBytes];int offset=0,n;
    while(offset<raw.Length&&(n=file.Read(raw,offset,raw.Length-offset))>0)offset+=n;
    if(offset!=raw.Length||file.ReadByte()!=-1)throw new InvalidOperationException("Native candidate changed during review");
   }
   if(CalibrationInstallation.Sha256(raw)!=CandidateSha256)throw new InvalidOperationException("Native candidate SHA256 changed");
   string text=CalibrationJson.Utf8.GetString(raw);truncated=text.Length>65536;return truncated?text.Substring(0,65536):text;
  }
 }
}
