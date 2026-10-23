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
  require(std::filesystem::exists(root_/"adoption.json"),"APPROVAL_POLICY_ABSENT");
  const auto manifest=strictJson(read("adoption.json"));
  require(manifest.at("schema")=="monaka.output-adoption.v1"&&manifest.at("enabled")==true,"APPROVAL_POLICY_DISABLED_OR_UNSUPPORTED");
  auto policy=pinned(manifest.at("policy"));
  require(policy.at("schema")=="monaka.physical-criteria.v1"&&policy.at("enabled")==true,"APPROVAL_POLICY_INVALID_OR_DISABLED");
  const auto policyId=string(policy,"id"),version=string(policy,"version"),policyHash=string(manifest.at("policy"),"sha256");
  const auto now=wall_(),validFrom=integer(policy,"valid_from_utc_ms"),validUntil=integer(policy,"valid_until_utc_ms");
  require(validFrom<validUntil&&now>=validFrom&&now<validUntil,"APPROVAL_POLICY_EXPIRED_OR_NOT_YET_VALID");
  const auto& b=c.target.target().binding;const auto& scope=policy.at("scope");
  require(scope.at("source")==b.source&&scope.at("input_space")==b.inputSpace&&scope.at("profile")==b.profile&&scope.at("world_space")==b.worldSpace,"APPROVAL_POLICY_SCOPE_MISMATCH");
  require(string(policy,"measurement_mode")=="trajectory","APPROVAL_MEASUREMENT_MODE_UNSUPPORTED");
  const auto& limits=policy.at("limits");
  for(const char* k:{"rms_position","rms_orientation","max_position","max_orientation","all_max_position","all_max_orientation"})number(limits,k);
  require(integer(limits,"min_pairs")>0&&integer(limits,"min_inliers")>0&&number(limits,"min_inlier_ratio")<=1,"APPROVAL_LIMITS_INVALID");
  require(manifest.at("evidence").is_array()&&!manifest.at("evidence").empty()&&manifest.at("evidence").size()<=256,"APPROVAL_EVIDENCE_ABSENT");
  OutputApprovalCriteria out;out.policyId=policyId+"/"+version+"/"+policyHash;
  std::int64_t expires=validUntil;Json evidencePins=Json::array();
  for(const auto& pin:manifest.at("evidence")){
   auto e=pinned(pin);require(e.at("schema")=="monaka.physical-evidence.v1","APPROVAL_EVIDENCE_SCHEMA");
   require(e.at("criteria_id")==policyId&&e.at("criteria_version")==version&&e.at("criteria_sha256")==policyHash,"APPROVAL_EVIDENCE_CRITERIA_MISMATCH");
   require(e.at("evidence_kind")=="physical_capture","APPROVAL_PHYSICAL_EVIDENCE_REQUIRED");
   require(e.at("source")==b.source&&e.at("input_space")==b.inputSpace&&e.at("input_revision")==b.inputRevision,"APPROVAL_EVIDENCE_SOURCE_REVISION_MISMATCH");
   const Key key{b.source,string(e,"device")};auto found=c.config.bindings.find(key);require(found!=c.config.bindings.end(),"APPROVAL_EVIDENCE_DEVICE_MISMATCH");const auto& member=found->second;
   require(member.inputSpace==b.inputSpace&&member.inputRevision==b.inputRevision&&member.profile==b.profile&&member.worldSpace==b.worldSpace,"APPROVAL_EVIDENCE_GROUP_MISMATCH");
   require(e.at("mapping_revision")==c.config.revision&&e.at("world_revision")==member.worldRevision&&e.at("world")==rigid(member.world),"APPROVAL_EVIDENCE_WORLD_MISMATCH");
   require(e.at("config_candidate_sha256")==c.configSha256,"APPROVAL_EVIDENCE_CONFIG_MISMATCH");
   require(e.at("source_session")==c.input.session&&e.at("source_clock")==c.input.clock,"APPROVAL_EVIDENCE_SESSION_MISMATCH");
   require(e.at("reference_serial")==c.input.referenceSerial&&e.at("reference_standing")==c.input.standing&&e.at("reference_generation")==c.input.referenceGeneration,"APPROVAL_EVIDENCE_REFERENCE_MISMATCH");
   const auto measured=integer(e,"measured_at_utc_ms"),end=integer(e,"expires_at_utc_ms");
   require(measured<=now&&measured<end&&now<end,"APPROVAL_EVIDENCE_EXPIRED_OR_FUTURE");expires=std::min(expires,end);
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
   require(number(result.at("all_residuals"),"max_position")<=number(limits,"all_max_position")&&number(result.at("all_residuals"),"max_orientation")<=number(limits,"all_max_orientation"),"APPROVAL_ALL_PAIR_MAX_CRITERIA_FAILED");
   require(out.eligible.insert(key).second,"APPROVAL_DUPLICATE_EVIDENCE_MEMBER");evidencePins.push_back(pin);
  }
  require(out.eligible.count({b.source,b.device})!=0,"APPROVAL_TARGET_EVIDENCE_ABSENT");
  require(expires>wallAnchor_&&expires-wallAnchor_<(INT64_MAX-monoAnchor_)/1000000,"APPROVAL_EXPIRY_RANGE");
  out.expiresAt=monoAnchor_+(expires-wallAnchor_)*1000000;out.evidenceId=string(manifest,"id");out.evidenceSha256=sha256(evidencePins.dump());return out;
 }
};
}
std::shared_ptr<const OutputApprovalAuthority> makeProductionOutputApprovalAuthority(const std::filesystem::path& root,Clock wall,Clock monotonic){return std::make_shared<FileAuthority>(root,std::move(wall),std::move(monotonic));}
}
