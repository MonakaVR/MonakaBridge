// Control-pipe fault generator only. No tracking codec, observation, solver,
// normalizer, SDK or socket implementation. Never a product helper.
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
class CalibrationProtocolFaultFixture {
 static readonly JavaScriptSerializer json=new JavaScriptSerializer();static string mode;static long sequence;static Dictionary<string,object> opened;
 static void Emit(Dictionary<string,object> value){value["previous_emitted_sequence"]=sequence;value["event_sequence"]=++sequence;Console.WriteLine(json.Serialize(value));Console.Out.Flush();}
 static Dictionary<string,object> Response(Dictionary<string,object> request,string status){return new Dictionary<string,object>{{"request_id",request["request_id"]},{"status",status},{"phase",status},{"session_id","test-only-session"},{"run_id","test-only-run"},{"replayed",false}};}
 static int Main(string[] args){
  mode=Environment.GetEnvironmentVariable("MONAKA_CALIBRATION_TEST_MODE");
  if(args.Length==1&&args[0]=="--local-api-version"){
   if(mode=="hang-version"){Thread.Sleep(Timeout.Infinite);return 0;}
   var version=new{api_version=1,software_policy=1,gui_contract=1,capture_supported=true,static_supported=false,production_capture="OPENVR_BACKGROUND_MIRROR",standing_policy="EXACT_FINITE_NUMERIC",runtime_validation="NOT_RUN",output_approval=mode.StartsWith("approval-"),features=mode.StartsWith("approval-")?new[]{"idle_invalidation","phase_events","witness_details","immutable_result_commit","independent_output_review"}:mode=="old-version"?new[]{"idle_invalidation"}:new[]{"idle_invalidation","phase_events","witness_details","immutable_result_commit"}};
   Console.WriteLine(json.Serialize(version));return 0;
  }
  string line;
  while((line=Console.ReadLine())!=null){var request=json.Deserialize<Dictionary<string,object>>(line);string op=(string)request["op"];
   if(op=="open"){
    opened=request;
    if(mode=="utf8"){var stream=Console.OpenStandardOutput();byte[] bytes={0xc3,0x28,0x0a};stream.Write(bytes,0,bytes.Length);stream.Flush();Thread.Sleep(Timeout.Infinite);}
    if(mode=="oversize"){Console.WriteLine(new string('x',17*1024+1));Console.Out.Flush();Thread.Sleep(Timeout.Infinite);}
    if(mode=="stderr"){Console.Error.Write(new string('x',256*1024+1));Console.Error.Flush();Thread.Sleep(Timeout.Infinite);}
    var value=Response(request,"OPEN");value["config_sha256"]=new string('0',64);value["capture_supported"]=true;value["readonly"]=request["readonly"];
    if(mode=="chain"){sequence=1;}if(mode=="unrequested")value["request_id"]="foreign-id";Emit(value);
    if(mode=="idle-invalid"){Emit(new Dictionary<string,object>{{"status","INVALIDATED"},{"phase","INSPECT_ONLY"},{"eligible",false},{"session_id","test-only-session"},{"run_id","test-only-run"},{"reason","test-only idle lifetime invalidation"}});}
   }else if(op=="discover"){
    if(mode.StartsWith("approval-")){
     var value=Response(request,"DISCOVERED");value["witness"]=new Dictionary<string,object>{{"bridge_id",opened["bridge_id"]},{"source",opened["source"]},{"device",opened["device"]},{"tracker",opened["tracker"]},{"input_space",mode=="approval-space"?"current-frame":opened["input_space"]},{"input_revision",mode=="approval-revision"?2:opened["input_revision"]},{"reference_role",opened["reference_role"]},{"convention","fixture-native"},{"standing_witness","fixture-standing"},{"reference_serial","fixture-reference"}};Emit(value);continue;
    }
    var value=Response(request,"REJECTED");value["reason"]="test-only deliberate fault";if(mode=="foreign-session")value["session_id"]="foreign";Emit(value);
   }else if(op=="cancel"||op=="close"){Emit(Response(request,op=="close"?"CLOSED":"CANCELLED"));if(op=="close")return 0;}
  }
  return 0;
 }
}
