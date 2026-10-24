#include "monaka_bridge/output_approval_authority.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include <limits>

namespace mb::local_calibration {
std::int64_t utcMilliseconds(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
namespace {
void require(bool b,const char* code){if(!b)throw std::runtime_error(code);}
Json strictJson(const std::string& bytes){
 std::vector<std::set<std::string>> keys;
 return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){
  require(depth<=24,"APPROVAL_JSON_DEPTH");
  if(event==Json::parse_event_t::object_start)keys.emplace_back();
  if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(value.get<std::string>()).second,"APPROVAL_DUPLICATE_KEY");
  if(event==Json::parse_event_t::object_end)keys.pop_back();return true;
 });
}
std::string string(const Json& j,const char* key){const auto& v=j.at(key);require(v.is_string()&&!v.get_ref<const std::string&>().empty(),"APPROVAL_FIELD_INVALID");return v.get<std::string>();}
double number(const Json& j,const char* key){const auto& v=j.at(key);require(v.is_number(),"APPROVAL_NUMBER_REQUIRED");double n=v.get<double>();require(std::isfinite(n)&&n>=0,"APPROVAL_NUMBER_INVALID");return n;}
std::int64_t integer(const Json& j,const char* key){const auto& v=j.at(key);require(v.is_number_integer()&&v>=0&&v<=INT64_MAX,"APPROVAL_INTEGER_INVALID");return v.get<std::int64_t>();}
bool hash(const std::string& s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
Json rigid(const Rigid& r){return {{"rotation_xyzw",r.rotation},{"translation_m",r.translation}};}
std::optional<std::int64_t> semanticTime(const Json& document,std::int64_t now){
 require(document.contains("lifetime")&&document.at("lifetime").is_object(),"APPROVAL_LIFETIME_REQUIRED");
 const auto& v=document.at("lifetime");require(v.contains("mode")&&v.at("mode")=="semantic","APPROVAL_LIFETIME_MODE_INVALID");
 for(auto it=v.begin();it!=v.end();++it)require(it.key()=="mode"||it.key()=="not_before_utc_ms"||it.key()=="not_after_utc_ms","APPROVAL_LIFETIME_FIELD_INVALID");
 require(!document.contains("expires_at_utc_ms")&&!document.contains("valid_from_utc_ms")&&!document.contains("valid_until_utc_ms"),"APPROVAL_MIXED_LIFETIME_SCHEMA");
 std::optional<std::int64_t> start,end;
 if(v.contains("not_before_utc_ms")&&!v.at("not_before_utc_ms").is_null())start=integer(v,"not_before_utc_ms");
 if(v.contains("not_after_utc_ms")&&!v.at("not_after_utc_ms").is_null())end=integer(v,"not_after_utc_ms");
 require(!start||!end||*start<*end,"APPROVAL_TIME_POLICY_RANGE");
 require(!start||now>=*start,"APPROVAL_TIME_POLICY_NOT_YET_VALID");require(!end||now<*end,"APPROVAL_TIME_POLICY_EXPIRED");return end;
}
void quality(const Json& result,const Json& policy){
 require(result.contains("quality")&&result.contains("measurement_options"),"APPROVAL_NATIVE_QUALITY_MISSING");
 const auto& q=result.at("quality");const auto& limits=policy.at("motion_quality");const auto& o=result.at("measurement_options");
 require(q.at("sufficient")==true&&integer(q,"samples")==integer(result,"inliers"),"APPROVAL_NATIVE_QUALITY_INVALID");
 for(const char* k:{"valid_ratio","span_seconds","accumulated_rotation_rad","axis_diversity"})require(number(q,k)>=number(limits,k),"APPROVAL_MOTION_QUALITY_FAILED");
 require(number(q,"valid_ratio")<=1&&q.at("extent_m").is_array()&&q.at("extent_m").size()==3&&limits.at("extent_m").is_array()&&limits.at("extent_m").size()==3,"APPROVAL_NATIVE_QUALITY_INVALID");
 for(int i=0;i<3;++i){Json a={{"v",q.at("extent_m").at(i)}},b={{"v",limits.at("extent_m").at(i)}};require(number(a,"v")>=number(b,"v"),"APPROVAL_MOTION_QUALITY_FAILED");}
 for(const char* k:{"recording_seconds","relative_motion_min_rad","relative_motion_max_rad","inlier_position_m","inlier_orientation_rad"})require(number(o,k)==number(policy.at("measurement_options"),k),"APPROVAL_MEASUREMENT_OPTIONS_MISMATCH");
}
}
Json evaluateEvidence(const OutputApprovalContext& c,const Json& e,const Json& policy,const Json& declaration,std::int64_t wallNow){
 const auto& b=c.target.target().binding;
 Json out={{"status","UNKNOWN"},{"reason","APPROVAL_EVIDENCE_UNEVALUATED"},{"policy_id",policy.value("id",Json(nullptr))},{"policy_version",policy.value("version",Json(nullptr))},{"policy_sha256",e.value("criteria_sha256",Json(nullptr))},
  {"evidence_id",e.value("id",Json(nullptr))},{"evidence_sha256",sha256(e.dump())},
  {"current",{{"source",c.input.source},{"device",c.input.device},{"space",c.input.space},{"revision",c.input.revision},{"session",c.input.session},{"clock",c.input.clock}}},
  {"saved",{{"source",e.value("source",Json(nullptr))},{"device",e.value("device",Json(nullptr))},{"space",e.value("input_space",Json(nullptr))},{"revision",e.value("input_revision",Json(nullptr))},{"session",e.value("source_session",Json(nullptr))},{"clock",e.value("source_clock",Json(nullptr))}}},
  {"world_revision",e.value("world_revision",Json(nullptr))},{"continuity_status",declaration.value("continuity",Json("UNKNOWN"))},{"current_witness_status","UNAVAILABLE"},{"time_policy_status","NOT_EVALUATED"}};
 try{
  require(c.currentWitnessAvailable,"APPROVAL_CURRENT_WITNESS_UNAVAILABLE");
  require(c.poseAgeNs>=0&&c.referenceAgeNs>=0&&c.poseAgeNs<=50000000&&c.referenceAgeNs<=50000000,"APPROVAL_CURRENT_WITNESS_STALE");out["current_witness_status"]="FRESH";
  require(c.input.source==b.source&&c.input.device==b.device&&c.input.space==b.inputSpace&&c.input.revision==b.inputRevision&&c.input.convention==c.config.profiles.at(b.profile).convention,"APPROVAL_CURRENT_WITNESS_MISMATCH");
  require(e.at("source")==b.source&&e.at("input_space")==b.inputSpace&&e.at("input_revision")==b.inputRevision,"APPROVAL_EVIDENCE_SOURCE_REVISION_MISMATCH");
  require(e.at("device")==b.device,"APPROVAL_EVIDENCE_DEVICE_MISMATCH");
  require(e.at("mapping_revision")==c.config.revision&&e.at("world_revision")==b.worldRevision&&e.at("world")==rigid(b.world),"APPROVAL_EVIDENCE_WORLD_MISMATCH");
  require(e.at("config_candidate_sha256")==c.configSha256,"APPROVAL_EVIDENCE_CONFIG_MISMATCH");
  require(e.at("criteria_id")==policy.at("id")&&e.at("criteria_version")==policy.at("version"),"APPROVAL_EVIDENCE_CRITERIA_MISMATCH");
  require(e.at("reference_serial")==c.input.referenceSerial&&e.at("reference_standing")==c.input.standing&&e.at("reference_generation")==c.input.referenceGeneration,"APPROVAL_EVIDENCE_REFERENCE_MISMATCH");
  semanticTime(policy,wallNow);auto end=semanticTime(e,wallNow);const auto measured=integer(e,"measured_at_utc_ms");
  require(measured<=wallNow&&(!end||measured<*end),"APPROVAL_EVIDENCE_EXPIRED_OR_FUTURE");out["time_policy_status"]="SATISFIED";
  require(declaration.value("schema",std::string{})=="monaka.evidence-continuity.v1","APPROVAL_CONTINUITY_DECLARATION_MISSING");
  const auto continuity=string(declaration,"continuity");
  require(continuity!="UNKNOWN","APPROVAL_CONTINUITY_UNKNOWN");require(continuity!="CHANGED","APPROVAL_CONTINUITY_CHANGED");require(continuity=="SAME","APPROVAL_CONTINUITY_INVALID");
  require(declaration.contains("operator")&&declaration.at("operator").is_string()&&!declaration.at("operator").get_ref<const std::string&>().empty(),"APPROVAL_OPERATOR_DECLARATION_REQUIRED");
  for(const char* k:{"source","device","input_space","input_revision"})require(declaration.at(k)==e.at(k),"APPROVAL_CONTINUITY_IDENTITY_MISMATCH");
  require(declaration.at("current_session")==c.input.session&&declaration.at("current_clock")==c.input.clock,"APPROVAL_CURRENT_DECLARATION_SESSION_MISMATCH");
  require(declaration.at("config_sha256")==c.configSha256&&declaration.at("policy_sha256")==e.at("criteria_sha256"),"APPROVAL_DECLARATION_PROVENANCE_MISMATCH");
  require(e.contains("semantic_identity"),"APPROVAL_SAVED_SEMANTICS_MISSING");const auto& saved=e.at("semantic_identity");
  require(saved.is_object()&&saved.contains("space_generation")&&saved.at("space_generation").is_string()&&saved.contains("attachment_relation")&&saved.at("attachment_relation").is_string(),"APPROVAL_SAVED_SEMANTICS_MISSING");
  require(string(saved,"space_generation")==string(declaration,"space_generation"),"APPROVAL_SPACE_GENERATION_CHANGED");
  require(string(saved,"input_convention")==c.input.convention,"APPROVAL_CONVENTION_CHANGED");
  require(string(saved,"reference_role")==c.input.referenceRole&&declaration.at("reference_role")==c.input.referenceRole,"APPROVAL_REFERENCE_ROLE_CHANGED");
  require(declaration.contains("attachment_unchanged")&&declaration.at("attachment_unchanged").is_boolean()&&declaration.contains("attachment_relation")&&declaration.at("attachment_relation").is_string(),"APPROVAL_ATTACHMENT_DECLARATION_MISSING");
  require(declaration.at("attachment_unchanged")==true,"APPROVAL_ATTACHMENT_CHANGED");
  require(string(saved,"attachment_relation")==string(declaration,"attachment_relation"),"APPROVAL_ATTACHMENT_CHANGED");
  require(declaration.at("reference_serial")==c.input.referenceSerial&&declaration.at("reference_standing")==c.input.standing&&declaration.at("reference_generation")==c.input.referenceGeneration,"APPROVAL_CURRENT_REFERENCE_MISMATCH");
  out["status"]="VALID";out["reason"]="APPROVAL_EVIDENCE_VALID";out["saved_semantic_identity"]=saved;
 }catch(const std::exception& error){
  const auto reason=diagnostic(error);out["reason"]=reason;
  out["status"]=(reason=="APPROVAL_CURRENT_WITNESS_UNAVAILABLE"||reason=="APPROVAL_CURRENT_WITNESS_STALE"||reason=="APPROVAL_CONTINUITY_UNKNOWN"||reason=="APPROVAL_CONTINUITY_DECLARATION_MISSING"||reason=="APPROVAL_SAVED_SEMANTICS_MISSING"||reason=="APPROVAL_ATTACHMENT_DECLARATION_MISSING"||reason=="APPROVAL_OPERATOR_DECLARATION_REQUIRED")?"UNKNOWN":"INVALID";
 }
 return out;
}
namespace {
class FileAuthority final:public OutputApprovalAuthority {
 std::filesystem::path root_;Clock wall_;const std::int64_t wallAnchor_,monoAnchor_;
 std::filesystem::path file(const std::string& name)const{
  require(!name.empty()&&name.size()<160&&name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")==std::string::npos&&name!="."&&name!="..","APPROVAL_FILE_NAME_INVALID");
  auto p=root_/name;requirePlainPath(p);return p;
 }
 std::string read(const std::string& name)const{return readConfigBytes(file(name));}
 Json pinned(const Json& pin)const{
  auto expected=string(pin,"sha256");require(hash(expected),"APPROVAL_HASH_INVALID");
  auto bytes=read(string(pin,"file"));require(sha256(bytes)==expected,"APPROVAL_HASH_MISMATCH");return strictJson(bytes);
 }
public:
 FileAuthority(std::filesystem::path root,Clock wall,Clock mono):root_(std::move(root)),wall_(std::move(wall)),wallAnchor_(wall_()),monoAnchor_(mono()){}
 OutputApprovalCriteria verify(const OutputApprovalContext& c)const override{
 Json detail={{"status","UNKNOWN"},{"reason","APPROVAL_EVIDENCE_UNEVALUATED"},{"policy_id",nullptr},{"policy_version",nullptr},{"policy_sha256",nullptr},{"evidence_id",nullptr},{"evidence_sha256",nullptr},{"saved",nullptr},{"world_revision",nullptr},{"continuity_status","UNKNOWN"},{"current_witness_status","NOT_EVALUATED"},{"time_policy_status","NOT_EVALUATED"},{"current",{{"source",c.input.source},{"device",c.input.device},{"space",c.input.space},{"revision",c.input.revision}}}};
 try{
  require(std::filesystem::exists(root_/"adoption.json"),"APPROVAL_POLICY_ABSENT");
  const auto manifest=strictJson(read("adoption.json"));
  require(manifest.at("schema")=="monaka.output-adoption.v1"&&manifest.at("enabled")==true,"APPROVAL_POLICY_DISABLED_OR_UNSUPPORTED");
  auto policy=pinned(manifest.at("policy"));
  const bool semantic=policy.at("schema")=="monaka.physical-criteria.v2";
  require((semantic||policy.at("schema")=="monaka.physical-criteria.v1")&&policy.at("enabled")==true,"APPROVAL_POLICY_INVALID_OR_DISABLED");
  const auto policyId=string(policy,"id"),version=string(policy,"version"),policyHash=string(manifest.at("policy"),"sha256");
  const auto now=wall_();std::optional<std::int64_t> expiry;
  if(semantic)expiry=semanticTime(policy,now);
  else{const auto from=integer(policy,"valid_from_utc_ms"),until=integer(policy,"valid_until_utc_ms");require(from<until&&now>=from&&now<until,"APPROVAL_POLICY_EXPIRED_OR_NOT_YET_VALID");expiry=until;}
  detail.update({{"policy_id",policyId},{"policy_version",version},{"policy_sha256",policyHash}});
  const auto& b=c.target.target().binding;const auto& scope=policy.at("scope");
  require(scope.at("source")==b.source&&scope.at("input_space")==b.inputSpace&&scope.at("profile")==b.profile&&scope.at("world_space")==b.worldSpace,"APPROVAL_POLICY_SCOPE_MISMATCH");
  require(string(policy,"measurement_mode")=="trajectory","APPROVAL_MEASUREMENT_MODE_UNSUPPORTED");
  const auto& limits=policy.at("limits");
  for(const char* k:{"rms_position","rms_orientation","max_position","max_orientation"})number(limits,k);
  if(semantic){require(policy.at("all_pair_max_mode")=="diagnostic","APPROVAL_ALL_PAIR_MODE_INVALID");require(!limits.contains("all_max_position")&&!limits.contains("all_max_orientation"),"APPROVAL_DIAGNOSTIC_LIMIT_CONFLICT");}
  else{number(limits,"all_max_position");number(limits,"all_max_orientation");}
  require(integer(limits,"min_pairs")>0&&integer(limits,"min_inliers")>0&&number(limits,"min_inlier_ratio")<=1,"APPROVAL_LIMITS_INVALID");
  require(manifest.at("evidence").is_array()&&!manifest.at("evidence").empty()&&manifest.at("evidence").size()<=256,"APPROVAL_EVIDENCE_ABSENT");
  OutputApprovalCriteria out;out.policyId=policyId+"/"+version+"/"+policyHash;
  Json evidencePins=Json::array(),evaluations=Json::array();
  Json declaration=Json::object();if(semantic&&manifest.contains("current_declaration")){declaration=pinned(manifest.at("current_declaration"));evidencePins.push_back(manifest.at("current_declaration"));}
  for(const auto& pin:manifest.at("evidence")){
   auto e=pinned(pin);detail.update({{"evidence_id",e.value("id",Json(nullptr))},{"evidence_sha256",pin.at("sha256")},{"saved",{{"source",e.value("source",Json(nullptr))},{"device",e.value("device",Json(nullptr))},{"space",e.value("input_space",Json(nullptr))},{"revision",e.value("input_revision",Json(nullptr))}}},{"world_revision",e.value("world_revision",Json(nullptr))}});
   require(e.at("schema")== (semantic?"monaka.physical-evidence.v2":"monaka.physical-evidence.v1"),"APPROVAL_EVIDENCE_SCHEMA");
   require(e.at("criteria_id")==policyId&&e.at("criteria_version")==version&&e.at("criteria_sha256")==policyHash,"APPROVAL_EVIDENCE_CRITERIA_MISMATCH");
   require(e.at("evidence_kind")=="physical_capture","APPROVAL_PHYSICAL_EVIDENCE_REQUIRED");
   require(e.at("source")==b.source&&e.at("input_space")==b.inputSpace&&e.at("input_revision")==b.inputRevision,"APPROVAL_EVIDENCE_SOURCE_REVISION_MISMATCH");
   const Key key{b.source,string(e,"device")};auto found=c.config.bindings.find(key);require(found!=c.config.bindings.end(),"APPROVAL_EVIDENCE_DEVICE_MISMATCH");const auto& member=found->second;
   require(member.inputSpace==b.inputSpace&&member.inputRevision==b.inputRevision&&member.profile==b.profile&&member.worldSpace==b.worldSpace,"APPROVAL_EVIDENCE_GROUP_MISMATCH");
   require(e.at("mapping_revision")==c.config.revision&&e.at("world_revision")==member.worldRevision&&e.at("world")==rigid(member.world),"APPROVAL_EVIDENCE_WORLD_MISMATCH");
   require(e.at("config_candidate_sha256")==c.configSha256,"APPROVAL_EVIDENCE_CONFIG_MISMATCH");
   if(!semantic)require(e.at("source_session")==c.input.session&&e.at("source_clock")==c.input.clock,"APPROVAL_EVIDENCE_SESSION_MISMATCH");
   require(e.at("reference_serial")==c.input.referenceSerial&&e.at("reference_standing")==c.input.standing&&e.at("reference_generation")==c.input.referenceGeneration,"APPROVAL_EVIDENCE_REFERENCE_MISMATCH");
   if(semantic){
    detail=evaluateEvidence(c,e,policy,declaration,now);detail["evidence_sha256"]=pin.at("sha256");
    if(detail.at("status")!="VALID")throw EvidenceEvaluationError(detail);
    require(declaration.at("evidence_sha256")==pin.at("sha256"),"APPROVAL_DECLARATION_EVIDENCE_MISMATCH");
    auto end=semanticTime(e,now);if(end)expiry=expiry?std::min(*expiry,*end):end;
   }else{const auto measured=integer(e,"measured_at_utc_ms"),end=integer(e,"expires_at_utc_ms");require(measured<=now&&measured<end&&now<end,"APPROVAL_EVIDENCE_EXPIRED_OR_FUTURE");expiry=std::min(*expiry,end);}
   require(e.at("measurement_mode")==policy.at("measurement_mode"),"APPROVAL_EVIDENCE_MODE_MISMATCH");
   require(hash(string(e,"tool_sha256"))&&!string(e,"build_identity").empty(),"APPROVAL_EVIDENCE_BUILD_IDENTITY");
   auto result=pinned(e.at("native_result"));auto preimage=pinned(e.at("config_preimage"));
   require(string(e,"config_preimage_sha256")==string(e.at("config_preimage"),"sha256")&&result.at("config_sha256")==e.at("config_preimage_sha256"),"APPROVAL_EVIDENCE_PREIMAGE_MISMATCH");
   require(result.at("result_id")==e.at("measurement_result_id")&&result.at("mode")=="trajectory"&&result.at("world")==e.at("world"),"APPROVAL_EVIDENCE_RESULT_MISMATCH");
   // Raw/result hashes are adopted as a set; changed raw or reconstructed result fails closed.
   auto raw=pinned(e.at("raw"));require(raw.at("source").is_array()&&raw.at("reference").is_array(),"APPROVAL_EVIDENCE_RAW_INVALID");
   require(preimage.at("mapping_revision").get<std::uint64_t>()+1==c.config.revision,"APPROVAL_EVIDENCE_PREIMAGE_REVISION_MISMATCH");
   const auto pairs=integer(result,"pairs"),inliers=integer(result,"inliers");
   require(pairs>=integer(limits,"min_pairs")&&inliers>=integer(limits,"min_inliers")&&inliers<=pairs&&double(inliers)/pairs>=number(limits,"min_inlier_ratio"),"APPROVAL_PAIR_CRITERIA_FAILED");
   for(const char* k:{"rms_position","rms_orientation","max_position","max_orientation"})require(number(result,k)<=number(limits,k),"APPROVAL_RESIDUAL_CRITERIA_FAILED");
   const auto allPosition=number(result.at("all_residuals"),"max_position"),allOrientation=number(result.at("all_residuals"),"max_orientation");
   if(semantic)quality(result,policy);
   else require(allPosition<=number(limits,"all_max_position")&&allOrientation<=number(limits,"all_max_orientation"),"APPROVAL_ALL_PAIR_MAX_CRITERIA_FAILED");
   detail["status"]="VALID";detail["reason"]="APPROVAL_EVIDENCE_VALID";detail["all_pair_max"]={{"position",allPosition},{"orientation",allOrientation}};evaluations.push_back(detail);
   require(out.eligible.insert(key).second,"APPROVAL_DUPLICATE_EVIDENCE_MEMBER");evidencePins.push_back(pin);
  }
  require(out.eligible.count({b.source,b.device})!=0,"APPROVAL_TARGET_EVIDENCE_ABSENT");
  out.expiresAt.reset();if(expiry){require(*expiry>wallAnchor_&&*expiry-wallAnchor_<(INT64_MAX-monoAnchor_)/1000000,"APPROVAL_EXPIRY_RANGE");out.expiresAt=monoAnchor_+(*expiry-wallAnchor_)*1000000;}
  out.evidenceId=string(manifest,"id");out.evidenceSha256=sha256(evidencePins.dump());out.evaluation={{"status","VALID"},{"reason","APPROVAL_EVIDENCE_VALID"},{"lifetime",semantic?"semantic":"legacy_timestamp"},{"members",evaluations}};return out;
 }catch(const EvidenceEvaluationError&){throw;}catch(const std::exception& error){detail["status"]="INVALID";detail["reason"]=diagnostic(error);throw EvidenceEvaluationError(detail);}
 }
};
}
std::shared_ptr<const OutputApprovalAuthority> makeProductionOutputApprovalAuthority(const std::filesystem::path& root,Clock wall,Clock monotonic){return std::make_shared<FileAuthority>(root,std::move(wall),std::move(monotonic));}
}
