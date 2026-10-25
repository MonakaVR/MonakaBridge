#include "monaka_bridge/calibration_session.hpp"
#include "monaka_bridge/calibration_capture_monitor.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include "monaka_bridge/static_calibration_session.hpp"
#include <algorithm>
#include "monaka_bridge/trajectory_result_json.hpp"
#include <set>

namespace mb::local_calibration {
std::string diagnostic(const std::exception& error){std::string s;const char* p=error.what();for(std::size_t i=0;i<512&&p[i];++i)s+=p[i];return Json::parse(Json(s).dump(-1,' ',false,Json::error_handler_t::replace)).get<std::string>();}
namespace {
void need(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
enum Phase {NEW,OPEN,DISCOVERED,CONFIRMED,WAITING_POINT,RECORDING,SOLVING,APPROVAL_REVIEW,RESULT_READY,PREPARED,COMMITTING,SAVED,SAVED_STALE,CANCELLED,CLOSED,FAILED,INSPECT_ONLY,UNKNOWN};
const char* name(Phase p){return std::array<const char*,18>{"NEW","OPEN","DISCOVERED","CONFIRMED","WAITING_POINT","RECORDING","SOLVING","APPROVAL_REVIEW","RESULT_READY","PREPARED","COMMITTING","SAVED","SAVED_CONFIG_EVIDENCE_STALE","CANCELLED","CLOSED","FAILED","INSPECT_ONLY","UNKNOWN"}.at(p);}
bool terminalPhase(Phase p){return p>=SAVED;}
Json rigid(const Rigid& r){return {{"rotation_xyzw",r.rotation},{"translation_m",r.translation}};}
void fields(const Json& j,const std::set<std::string>& allowed){for(auto it=j.begin();it!=j.end();++it)need(allowed.count(it.key())!=0,"UNKNOWN_FIELD");}
std::string text(const Json& j,const char* key){need(j.contains(key)&&j.at(key).is_string(),"MISSING_OR_WRONG_TYPE");return j.at(key).get<std::string>();}
void schema(const Json& j){
 need(j.is_object()&&j.contains("api_version")&&j.at("api_version").is_number_integer()&&j.at("api_version")==1,"UNSUPPORTED_API_VERSION");
 auto id=text(j,"request_id");need(!id.empty()&&id.size()<=64,"REQUEST_ID_LIMIT");for(unsigned char c:id)need(c>=33&&c<=126,"REQUEST_ID_ASCII");
 auto op=text(j,"op");std::set<std::string> allowed={"api_version","request_id","op"};
 if(op=="open"){
  for(auto f:{"config_path","source","device","tracker","input_space","bridge_id","installation_path","helper_path","helper_sha256","association","mirror_address","reference_role"}){text(j,f);allowed.insert(f);}
  for(auto f:{"input_revision","mirror_port"}){need(j.contains(f)&&j.at(f).is_number_integer()&&j.at(f)>=0,"MISSING_OR_WRONG_TYPE");allowed.insert(f);}
  need(j.contains("readonly")&&j.at("readonly").is_boolean(),"MISSING_OR_WRONG_TYPE");allowed.insert("readonly");
  for(auto f:{"mode","purpose"})if(j.contains(f)){text(j,f);allowed.insert(f);}
  for(auto f:{"static_points","static_samples"})if(j.contains(f)){need(j.at(f).is_number_unsigned()||(j.at(f).is_number_integer()&&j.at(f)>=0),"STATIC_PLAN_TYPE");allowed.insert(f);}
 }else{
  need(std::set<std::string>{"discover","confirm_target","begin","capture_point","cancel","prepare_commit","commit","close","review_output","prepare_output","commit_output"}.count(op),"UNSUPPORTED_OPERATION");
  for(auto f:{"session_id","run_id"}){text(j,f);allowed.insert(f);}
  if(op=="begin"){text(j,"confirmation_token");allowed.insert("confirmation_token");}
  if(op=="prepare_commit"||op=="commit"){text(j,"result_id");allowed.insert("result_id");}
  if(op=="commit")for(auto f:{"review_token","candidate_sha256"}){text(j,f);allowed.insert(f);}
  if(op=="capture_point")for(auto f:{"point_token"}){if(j.contains(f))text(j,f);allowed.insert(f);}
  if(op=="prepare_output"||op=="commit_output"){text(j,"approval_id");allowed.insert("approval_id");}
  if(op=="prepare_output"){need(j.contains("members")&&j.at("members").is_array(),"APPROVAL_SELECTION_REQUIRED");for(const auto& member:j.at("members"))need(member.is_string(),"APPROVAL_MEMBER_TYPE");allowed.insert("members");}
  if(op=="commit_output")for(auto f:{"review_token","candidate_sha256"}){text(j,f);allowed.insert(f);}
 }
 fields(j,allowed);
}

}
Json parseRequest(const std::string& line){
 need(line.size()<=64*KiB,"LINE_LIMIT");std::vector<std::set<std::string>> keys;
 auto callback=[&](int depth,Json::parse_event_t event,Json& parsed){
  need(depth<=16,"DEPTH_LIMIT");
  if(event==Json::parse_event_t::object_start)keys.emplace_back();
  if(event==Json::parse_event_t::key)need(!keys.empty()&&keys.back().insert(parsed.get<std::string>()).second,"DUPLICATE_KEY");
  if(event==Json::parse_event_t::object_end)keys.pop_back();return true;
 };
 auto j=Json::parse(line,callback);std::size_t total=0;
 std::function<void(const Json&,const std::string&)> check=[&](const Json& v,const std::string& key){
  if(v.is_object()){need(v.size()<=64,"OBJECT_FIELD_LIMIT");total+=v.size();need(total<=256,"TOTAL_FIELD_LIMIT");for(auto it=v.begin();it!=v.end();++it){need(it.key().size()<=4*KiB,"STRING_LIMIT");check(it.value(),it.key());}}
  else if(v.is_array()){need(v.size()<=256,"ARRAY_LIMIT");for(const auto& x:v)check(x,key);}
  else if(v.is_string())need(v.get_ref<const std::string&>().size()<=(key.find("path")!=std::string::npos?32*KiB:4*KiB),"STRING_LIMIT");
  else if(v.is_number_float())need(std::isfinite(v.get<double>()),"NONFINITE_NUMBER");
 };
 check(j,"");schema(j);return j;
}

void CaptureProvider::configure(std::shared_ptr<const ProviderOpenOptions> options){
 need(bool(options),"PROVIDER_CONFIGURATION_REQUIRED");
 need(!configurationAttempted_,"PROVIDER_CONFIGURATION_ALREADY_ATTEMPTED");
 configurationAttempted_=true;validateConfiguration(*options);configuration_=std::move(options);
}
std::shared_ptr<const ProviderOpenOptions> CalibrationSession::makeProviderOptions(const Config& config,
 FrozenMeasurementTarget target,ProviderOpenOptions::Deployment deployment){
 const auto& b=target.target().binding;std::optional<CapabilityProfile> contract;
 auto found=config.contracts.find({b.source,b.device});if(found!=config.contracts.end())contract=found->second;
 return std::shared_ptr<const ProviderOpenOptions>(new ProviderOpenOptions(std::move(target),std::move(contract),std::move(deployment)));
}

struct CalibrationSession::Impl {
 CaptureProvider& provider;SessionEnvironment env;DraftStore drafts;
 std::atomic<Phase> state{NEW};std::atomic<bool> cancel{false},eof{false};
 const std::string session=opaqueId(),run=opaqueId();
 std::int64_t created,lastNow,confirmationDeadline=0,resultDeadline=0,reviewDeadline=0;
 std::string original,confirmation,review,resultId,candidate,candidateHash,firstFailure;
 void latchFailure(const std::string& reason){if(firstFailure.empty()){auto observed=owner?owner->snapshot().fault:std::string{};firstFailure=observed.empty()?reason:observed;}}
 Json failure()const{return {{"status",name(state)},{"phase",name(state)},{"reason","SESSION_INVALIDATED"},{"first_failure",firstFailure},{"session_id",session},{"run_id",run},{"eligible",false}};}
 void terminalArtifact(){if(!firstFailure.empty())drafts.write("terminal.json",failure().dump());drafts.terminal(name(state));}
 Config config;std::shared_ptr<const ProviderOpenOptions> providerOptions;
 const FrozenMeasurementTarget* frozen=nullptr; // Alias into the owning options.
 std::unique_ptr<const InputWitness> input;
 std::unique_ptr<const TrajectoryResult> result;
 std::unique_ptr<TrajectoryRecorder> recorder;
  bool staticMode=false,approvalPurpose=false;
  StaticOptions staticOptions;std::unique_ptr<StaticMeasurement> staticMeasurement;
  std::optional<RigidAlignmentResult> staticResult;
  std::int64_t staticStart=0,pointWaitDeadline=0;std::string pointToken;
  std::string approvalFingerprint;Json approvalDiff;
 std::unique_ptr<CaptureOwner> owner;
 bool evidenceStale=false,createdDraft=false;
 std::size_t solves=0,commits=0,cacheBytes=0,ordinaryIds=0,reservedIds=0;
 struct Entry {std::string canonical;Json outcome;};std::map<std::string,Entry> cache;std::mutex cacheMutex;
 explicit Impl(CaptureProvider& p,SessionEnvironment e):provider(p),env(std::move(e)),drafts(env.draftRoot),created(env.clock()),lastNow(created){}
 std::int64_t now(){auto t=env.clock();if(t<lastNow){if(state==COMMITTING)evidenceStale=true;else{state=INSPECT_ONLY;confirmation.clear();review.clear();}latchFailure("MONOTONIC_CLOCK_ROLLBACK");throw std::runtime_error("MONOTONIC_CLOCK_ROLLBACK");}lastNow=t;return t;}
 ~Impl(){cancel=true;if(owner)owner->shutdown();}
 void invalidate(const char* reason){latchFailure(reason);if(owner)owner->invalidate(firstFailure);if(state==COMMITTING){evidenceStale=true;return;}state=INSPECT_ONLY;confirmation.clear();review.clear();throw std::runtime_error(reason);}
 void checkCancel(){if(cancel&&state!=COMMITTING){state=eof?CLOSED:CANCELLED;confirmation.clear();review.clear();throw std::runtime_error(eof?"EOF_CLOSED":"CANCELLED");}}
 void current(){
  if(env.ioHook)env.ioHook("scope-before-read"); // Worker boundary only, never input owner.
  requirePlainPath(std::filesystem::u8path(env.helperPath));if(sha256(readConfigBytes(std::filesystem::u8path(env.helperPath)))!=env.helperSha256)invalidate("HELPER_HASH_CHANGED");
  requirePlainPath(providerOptions->configPath());
  auto raw=readConfigBytes(providerOptions->configPath());
  if(raw!=original)invalidate("CONFIG_EXACT_BYTES_CHANGED");
  auto c=parseConfigDocument(raw);validateFrozenMeasurementTarget(c,*frozen);
 }
 void record(const RecordingDelivery& delivery){
  const auto& b=delivery.values;
  for(std::size_t i=0;i<b.frames.size();++i){const auto& f=b.frames[i];
   if(auto p=std::get_if<c1::TrackerObservation>(&f.envelope)){auto value=*p;if(!delivery.eligible[i]){value.validity.position=false;value.validity.orientation=false;}recorder->observation(value,f.receiptNs);}
   else if(auto p=std::get_if<c1::ObservationDeviceState>(&f.envelope))recorder->deviceState(*p);
  }
  for(std::size_t i=0;i<b.references.size();++i){auto value=b.references[i];value.valid=delivery.referenceEligible[i];recorder->reference(value,input->referenceSerial);}
 }
 void abandonOutputReview(){review.clear();if(state==PREPARED)state=APPROVAL_REVIEW;}
 void live(bool fresh){
  auto t=now();checkCancel();
  if(t-created>=20*60*second)invalidate("WORKER_TTL_EXPIRED");
  if((result||staticResult||!approvalFingerprint.empty())&&t>=resultDeadline)invalidate("RESULT_REVIEW_EXPIRED");
  if(staticMeasurement&&!staticResult){if(t-staticStart>=5*60*second)invalidate("STATIC_RUN_TIMEOUT");if(state==WAITING_POINT&&t>=pointWaitDeadline)invalidate("STATIC_POINT_WAIT_TIMEOUT");}
  if(owner)try{owner->check(t,fresh);}catch(const std::exception& e){if(!owner->snapshot().fault.empty()){auto reason=diagnostic(e);invalidate(reason.c_str());}if(approvalPurpose)abandonOutputReview();throw;}
 }
 void guards(bool fresh,bool scope=true,std::int64_t confirmationLimit=0){
  if(scope)try{current();}catch(const std::exception& e){auto reason=diagnostic(e);invalidate(reason.c_str());}
  auto wait=10*second;
  if(confirmationLimit){auto t=now();checkCancel();if(t>=confirmationLimit)invalidate("CONFIRMATION_EXPIRED");if(t-created>=20*60*second)invalidate("WORKER_TTL_EXPIRED");wait=std::min({wait,confirmationLimit-t,created+20*60*second-t});}
  if(owner)try{owner->barrier(opaqueId(),wait);}catch(const std::exception& e){auto reason=diagnostic(e);invalidate(reason.c_str());}
  live(fresh);
 }
 void beginReady(){
  auto t=now();checkCancel();
  if(t-created>=20*60*second)invalidate("WORKER_TTL_EXPIRED");
  if(t>=confirmationDeadline)invalidate("CONFIRMATION_EXPIRED");
 }
 void artifactCheck(){
  drafts.verify();
  need(readConfigBytes(drafts.path("expected.json"))==original,"EXPECTED_ARTIFACT_CHANGED");
  need(readConfigBytes(drafts.path("result.json"))==resultJson().dump(),"RESULT_ARTIFACT_CHANGED");
  if(!candidate.empty())need(readConfigBytes(drafts.path("candidate.json"))==candidate,"CANDIDATE_ARTIFACT_CHANGED");
 }
 Json resultJson()const{
  if(staticResult)return {{"result_id",resultId},{"mode","static"},{"world",rigid(staticResult->transform)},{"extrinsic_not_mount",nullptr},{"mount_existing",rigid(frozen->target().binding.mount)},{"points",staticResult->pointCount},{"pairs_per_point",staticOptions.samples},{"rms_position",staticResult->rmsResidual},{"max_position",staticResult->maxResidual},{"rms_orientation",nullptr},{"quality","STATIC_RESIDUAL_ONLY_PHYSICAL_ACCURACY_UNVERIFIED"},{"initial_pose_floor",input->initialPoseFloor},{"initial_state_floor",input->initialStateFloor?Json(*input->initialStateFloor):Json(nullptr)},{"config_sha256",providerOptions->configSha256()},{"association",providerOptions->association()},{"review_deadline_ns",resultDeadline}};
  need(bool(result),"NO_RESULT");auto out=trajectoryResultJson(*result);
  out.update({{"result_id",resultId},{"mount_existing",rigid(frozen->target().binding.mount)},
   {"initial_pose_floor",input->initialPoseFloor},{"initial_state_floor",input->initialStateFloor?Json(*input->initialStateFloor):Json(nullptr)},
   {"config_sha256",providerOptions->configSha256()},{"association",providerOptions->association()},{"review_deadline_ns",resultDeadline}});
  return out;
 }
 Json waitingPoint(){pointToken=opaqueId();pointWaitDeadline=now()+60*second;state=WAITING_POINT;return {{"status","WAITING_POINT"},{"point_token",pointToken},{"point_index",staticMeasurement->pointCount()+1},{"points",staticOptions.points},{"pairs_required",staticOptions.samples},{"point_wait_deadline_ns",pointWaitDeadline},{"physical_stillness","NOT_VERIFIED"}};}
 OutputApprovalCriteria criteria(){
  need(bool(env.outputApprovalAuthority),"PHYSICAL_CRITERIA_NOT_ADOPTED");
  const auto witness=owner->snapshot();const auto checkedAt=now();
  auto c=env.outputApprovalAuthority->verify({*frozen,*input,config,providerOptions->configSha256(),checkedAt,
   witness.fault.empty()&&witness.poseValid&&witness.referenceValid,checkedAt-witness.lastValidPose,checkedAt-witness.referenceAt});
  need(!c.policyId.empty()&&c.policyId.size()<=4096&&!c.evidenceId.empty()&&c.evidenceId.size()<=4096&&c.evidenceSha256.size()==64&&(!c.expiresAt||*c.expiresAt>now()),"PHYSICAL_EVIDENCE_MISSING_OR_EXPIRED");
  for(char v:c.evidenceSha256)need((v>='0'&&v<='9')||(v>='a'&&v<='f'),"PHYSICAL_EVIDENCE_HASH_INVALID");
  const auto& b=frozen->target().binding;
  for(const auto& k:c.eligible){auto it=config.bindings.find(k);need(it!=config.bindings.end()&&it->second.source==b.source&&it->second.inputSpace==b.inputSpace&&it->second.inputRevision==b.inputRevision,"APPROVAL_CRITERIA_FOREIGN_MEMBER");}
  return c;
 }
 std::string fingerprint(const OutputApprovalCriteria& c){return Json({{"policy",c.policyId},{"evidence",c.evidenceId},{"sha256",c.evidenceSha256},{"expires",c.expiresAt?Json(*c.expiresAt):Json(nullptr)},{"eligible",c.eligible}}).dump();}
 Json outputAction(const Json& j){
  const auto op=text(j,"op");need(approvalPurpose,"OUTPUT_APPROVAL_SESSION_REQUIRED");
  if(op=="review_output"){
   need(state==CONFIRMED||state==APPROVAL_REVIEW,"STATE_REJECTED");const bool confirming=state==CONFIRMED;if(confirming)beginReady();guards(true,true,confirming?confirmationDeadline:0);
   std::optional<OutputApprovalCriteria> adopted;std::string reason;Json evaluation=Json::object();
   try{adopted=criteria();evaluation=adopted->evaluation;}catch(const EvidenceEvaluationError& e){reason=diagnostic(e);evaluation=e.evaluation;}catch(const std::exception& e){reason=diagnostic(e);evaluation={{"status","UNKNOWN"},{"reason",reason}};}
   if(confirming)beginReady();
   need(candidate.empty(),"APPROVAL_CANDIDATE_FIXED_REPREPARE_OR_NEW_SESSION");approvalFingerprint=adopted?fingerprint(*adopted):"UNADOPTED";resultId=opaqueId();resultDeadline=now()+10*60*second;state=APPROVAL_REVIEW;
   const auto& b=frozen->target().binding;Json members=Json::array();
   for(const auto& [k,v]:config.bindings)if(v.source==b.source&&v.inputSpace==b.inputSpace&&v.inputRevision==b.inputRevision)members.push_back({{"device",v.device},{"tracker",v.tracker},{"approved",v.spaceApproved},{"eligible",bool(adopted)&&adopted->eligible.count(k)!=0&&!v.spaceApproved&&!providerOptions->readOnly()},{"selected",false},{"mount_existing",rigid(v.mount)},{"world",rigid(v.world)},{"profile",v.profile},{"reason",providerOptions->readOnly()?"READONLY_APPROVAL_REJECTED":(!reason.empty()?reason:(v.spaceApproved?"ALREADY_APPROVED":(adopted&&adopted->eligible.count(k)?"ELIGIBLE_UNSELECTED":"MEMBER_PHYSICAL_EVIDENCE_MISSING")))}});
   return {{"status","APPROVAL_REVIEW"},{"approval_id",resultId},{"members",members},{"world",rigid(b.world)},{"mount_existing",rigid(b.mount)},{"extrinsic_not_mount",nullptr},{"config_sha256",providerOptions->configSha256()},{"source",b.source},{"input_space",b.inputSpace},{"input_revision",b.inputRevision},{"standing_witness",input->standing},{"criteria",adopted?adopted->policyId:"NOT_ADOPTED"},{"evidence",adopted?adopted->evidenceId:"MISSING"},{"evidence_sha256",adopted?adopted->evidenceSha256:""},{"evidence_evaluation",evaluation},{"reason",reason},{"runtime","UNCONFIRMED"}};
  }
  need((state==APPROVAL_REVIEW||state==PREPARED)&&text(j,"approval_id")==resultId,"APPROVAL_REVIEW_REQUIRED");
  need(!providerOptions->readOnly(),"READONLY_APPROVAL_REJECTED");OutputApprovalCriteria adopted;
  try{guards(true);adopted=criteria();need(fingerprint(adopted)==approvalFingerprint,"APPROVAL_CRITERIA_CHANGED_REVIEW_REQUIRED");}catch(...){abandonOutputReview();throw;}
  if(op=="prepare_output"){
   need(!j.at("members").empty(),"APPROVAL_SELECTION_REQUIRED");
   auto proposed=config;std::set<Key> selected;Json newDiff=Json::array();const auto& b=frozen->target().binding;
   for(const auto& item:j.at("members")){Key key{b.source,item.get<std::string>()};need(selected.insert(key).second,"APPROVAL_DUPLICATE_MEMBER");need(adopted.eligible.count(key),"APPROVAL_MEMBER_INELIGIBLE");auto& value=proposed.bindings.at(key);need(!value.spaceApproved,"APPROVAL_NO_CHANGE");value.spaceApproved=true;newDiff.push_back({{"source",key.first},{"device",key.second},{"space_approved_before",false},{"space_approved_after",true}});}
   if(!candidate.empty()){need(newDiff==approvalDiff,"APPROVAL_SELECTION_FIXED");drafts.verify();need(readConfigBytes(drafts.path("candidate.json"))==candidate,"APPROVAL_ARTIFACT_CHANGED");}
   else{
   approvalDiff=newDiff;
   need(proposed.revision<UINT32_MAX,"MAPPING_REVISION_EXHAUSTED");++proposed.revision;
   drafts.begin(run);createdDraft=true;drafts.write("expected.json",original);proposed.sourcePath.clear();drafts.publishCandidate(proposed,env.draftPublishHook);candidate=readConfigBytes(drafts.path("candidate.json"));candidateHash=sha256(candidate);
   }review=opaqueId();reviewDeadline=now()+30*second;state=PREPARED;
   return {{"status","OUTPUT_PREPARED"},{"approval_id",resultId},{"review_token",review},{"review_deadline_ns",reviewDeadline},{"candidate_sha256",candidateHash},{"candidate_bytes",candidate.size()},{"candidate_path",drafts.path("candidate.json").u8string()},{"diff",approvalDiff},{"world_extrinsic_mount","UNCHANGED"}};
  }
  need(op=="commit_output"&&state==PREPARED&&!review.empty()&&text(j,"review_token")==review&&text(j,"candidate_sha256")==candidateHash,"OUTPUT_COMMIT_NOT_PREPARED");
  if(now()>=reviewDeadline){review.clear();state=APPROVAL_REVIEW;throw std::runtime_error("APPROVAL_REVIEW_TOKEN_EXPIRED");}drafts.verify();need(readConfigBytes(drafts.path("expected.json"))==original&&readConfigBytes(drafts.path("candidate.json"))==candidate,"APPROVAL_ARTIFACT_CHANGED");
  try{
   checkCancel();const auto beforeBarrier=now();
   const auto remaining=std::min({10*second,reviewDeadline-beforeBarrier,resultDeadline-beforeBarrier,created+20*60*second-beforeBarrier});
   need(remaining>0,"APPROVAL_REVIEW_TOKEN_EXPIRED");
   auto barrier=owner->barrier(text(j,"request_id"),remaining);
   current();need(fingerprint(criteria())==approvalFingerprint,"APPROVAL_CRITERIA_CHANGED");
   checkCancel();const auto admissionAt=now();
   if(admissionAt-created>=20*60*second)invalidate("WORKER_TTL_EXPIRED");
   if(admissionAt>=resultDeadline)invalidate("RESULT_REVIEW_EXPIRED");
   need(admissionAt<reviewDeadline,"APPROVAL_REVIEW_TOKEN_EXPIRED");
   need(!adopted.expiresAt||admissionAt<*adopted.expiresAt,"PHYSICAL_EVIDENCE_MISSING_OR_EXPIRED");
   auto admission=owner->admit(barrier,admissionAt);state=COMMITTING;
  }catch(const std::exception& e){abandonOutputReview();if(!owner->snapshot().fault.empty()){auto reason=diagnostic(e);invalidate(reason.c_str());}throw;}
  review.clear();++commits;auto started=now();
  try{commitConfigDocument(providerOptions->configPath(),original,candidate,[&](const char* stage){if(env.transactionHook)env.transactionHook(stage);need(!cancel,"OUTPUT_APPROVAL_CANCELLED_BEFORE_REPLACE");guards(false,false);need(fingerprint(criteria())==approvalFingerprint,"APPROVAL_CRITERIA_CHANGED");});if(env.afterReplace)env.afterReplace();try{guards(true,false);need(fingerprint(criteria())==approvalFingerprint,"APPROVAL_CRITERIA_CHANGED");}catch(...){evidenceStale=true;}if(cancel||readConfigBytes(providerOptions->configPath())!=candidate)evidenceStale=true;state=now()-started>=15*second?UNKNOWN:(evidenceStale?SAVED_STALE:SAVED);}catch(const std::exception& e){state=UNKNOWN;drafts.terminal("UNKNOWN");return {{"status","UNKNOWN"},{"action","OUTPUT_APPROVAL"},{"reason",diagnostic(e)}};}
  Json saved={{"status",name(state)},{"action","OUTPUT_APPROVAL"},{"config_sha256",candidateHash},{"runtime","UNCONFIRMED"},{"message","OUTPUT APPROVAL CONFIG SAVED / RUNTIME UNCONFIRMED"}};try{drafts.write("terminal.json",saved.dump());drafts.terminal(name(state));}catch(...){saved["artifact_warning"]="TERMINAL_ARTIFACT_IO_FAILED";}return saved;
 }
 Json action(const Json& j){
  auto op=text(j,"op");auto t=now();if(op!="cancel"&&op!="close")checkCancel();
  if(t-created>=20*60*second&&state!=COMMITTING)invalidate("WORKER_TTL_EXPIRED");
  if(op=="open"){
   need(state==NEW,"STATE_REJECTED");need(j.at("helper_path")==env.helperPath&&j.at("helper_sha256")==env.helperSha256,"HELPER_MISMATCH");
   auto path=std::filesystem::u8path(text(j,"config_path"));requirePlainPath(path);auto installation=std::filesystem::u8path(text(j,"installation_path"));requirePlainPath(installation);need(std::filesystem::is_directory(installation),"INSTALLATION_UNAVAILABLE");
   need(j.at("association")=="operator_declared_process_owner_unverified"&&j.at("mirror_address")=="127.0.0.1"&&j.at("mirror_port").get<std::uint64_t>()>0&&j.at("mirror_port").get<std::uint64_t>()<=65535,"ASSOCIATION_UNSUPPORTED");
   need(j.at("reference_role")=="left"||j.at("reference_role")=="right"||j.at("reference_role")=="hmd","REFERENCE_ROLE_UNSUPPORTED");
   auto mode=j.value("mode",std::string("trajectory"));need(mode=="trajectory"||mode=="static","CAPTURE_MODE_UNSUPPORTED");const bool nextStaticMode=mode=="static";
   auto purpose=j.value("purpose",std::string("measurement"));need(purpose=="measurement"||purpose=="output_approval","PURPOSE_UNSUPPORTED");const bool nextApprovalPurpose=purpose=="output_approval";need(!nextApprovalPurpose||!nextStaticMode,"APPROVAL_MEASUREMENT_MODE_CONFLICT");
   StaticOptions nextStaticOptions;
   if(j.contains("static_points"))nextStaticOptions.points=j.at("static_points").get<std::size_t>();if(j.contains("static_samples"))nextStaticOptions.samples=j.at("static_samples").get<std::size_t>();nextStaticOptions.validate();need(nextStaticMode||(!j.contains("static_points")&&!j.contains("static_samples")),"STATIC_OPTIONS_REQUIRE_STATIC_MODE");
   auto nextOriginal=readConfigBytes(path);auto nextConfig=parseConfigDocument(nextOriginal);nextConfig.sourcePath=std::filesystem::weakly_canonical(path);
   need(j.at("bridge_id")==nextConfig.bridgeId&&j.at("input_revision").get<std::uint64_t>()<=UINT32_MAX,"TARGET_OWNER_REVISION_MISMATCH");
   auto target=freezeMeasurementTarget(nextConfig,text(j,"source"),text(j,"device"),text(j,"tracker"),text(j,"input_space"),j.at("input_revision").get<std::uint32_t>());
   auto options=CalibrationSession::makeProviderOptions(nextConfig,std::move(target),{path,installation,sha256(nextOriginal),text(j,"mirror_address"),text(j,"reference_role"),j.at("mirror_port").get<std::uint16_t>(),j.at("readonly").get<bool>(),nextStaticMode});
   Json reply={{"status","OPEN"},{"config_sha256",options->configSha256()},{"capture_supported",provider.supported()},{"readonly",options->readOnly()},{"association",options->association()}};
   try{provider.configure(options);}catch(...){state=FAILED;throw;}
   staticMode=nextStaticMode;approvalPurpose=nextApprovalPurpose;staticOptions=nextStaticOptions;
   original=std::move(nextOriginal);config=std::move(nextConfig);providerOptions=std::move(options);frozen=&providerOptions->measurementTarget();state=OPEN;
   return reply;
  }
  need(text(j,"session_id")==session&&text(j,"run_id")==run,"WRONG_SESSION_RUN");
  if(op=="close"||op=="cancel"){
   need(state!=COMMITTING,"COMMIT_OUTCOME_PENDING");bool already=terminalPhase(state);cancel=true;if(owner)owner->shutdown();
   if(!already){state=op=="close"?CLOSED:CANCELLED;confirmation.clear();review.clear();if(createdDraft)drafts.terminal(name(state));}
   Json reply={{"status",name(state)}};if(already)reply["closed"]=true;
   if(owner){auto warning=owner->snapshot().shutdownFault;if(!warning.empty())reply["shutdown_warning"]=warning;}return reply;
  }
  need(!terminalPhase(state),"INSPECT_ONLY_NEW_MEASUREMENT_REQUIRED");
  if(t-created>=20*60*second)invalidate("WORKER_TTL_EXPIRED");
  if(op=="review_output"||op=="prepare_output"||op=="commit_output")return outputAction(j);
  if(op=="capture_point"){
   need(staticMode,"STATIC_CAPTURE_UNSUPPORTED_B4_REQUIRED");need(state==WAITING_POINT&&j.contains("point_token")&&!pointToken.empty()&&text(j,"point_token")==pointToken,"STATIC_POINT_TOKEN_INVALID");
   guards(true);pointToken.clear();const auto start=now();staticMeasurement->beginPoint(start);state=RECORDING;
   owner->recordPoint([&](const RecordingDelivery& d){checkCancel();current();return staticMeasurement->consume(d);},std::min(staticStart+5*60*second,start+staticOptions.pointTimeout()));
   guards(true);staticMeasurement->finishPoint();
   if(staticMeasurement->pointCount()<staticOptions.points)return waitingPoint();
   drafts.write("raw.json",staticMeasurement->raw().dump());state=SOLVING;auto started=now();++solves;auto solved=staticMeasurement->solve();need(now()-started<10*second,"SOLVE_TIMEOUT");guards(true);staticResult=std::move(solved);resultId=opaqueId();resultDeadline=now()+10*60*second;drafts.write("result.json",resultJson().dump());current();auto barrier=owner->barrier("result:"+resultId);{auto admission=owner->admit(barrier,now());state=RESULT_READY;}return {{"status","RESULT_READY"},{"result",resultJson()}};
  }
  if(op=="discover"){
   need(state==OPEN,"STATE_REJECTED");need(provider.supported(),"CAPTURE_UNSUPPORTED_B3_REQUIRED");current();
   owner=std::make_unique<CaptureOwner>(provider,env.clock,cancel,created,run);
   InputWitness witness;try{witness=owner->discover();}catch(const std::exception& e){auto reason=diagnostic(e);invalidate(reason.c_str());}
   need(now()-t<10*second,"DISCOVER_TIMEOUT");checkCancel();
   input=std::make_unique<const InputWitness>(witness);live(true);
   const auto floor=witness.initialPoseFloor;const auto stateFloor=witness.initialStateFloor;state=DISCOVERED;
   Json detail={{"bridge_id",frozen->publisher()},{"source",witness.source},{"device",witness.device},{"tracker",frozen->target().binding.tracker},{"session",witness.session},{"clock",witness.clock},{"peer",witness.peer},{"input_space",witness.space},{"input_revision",witness.revision},{"convention",witness.convention},{"reference_role",witness.referenceRole},{"reference_serial",witness.referenceSerial},{"reference_generation",witness.referenceGeneration},{"standing_witness",witness.standing}};
   const auto& declaration=providerOptions->capabilityProfile();need(bool(declaration),"EXPLICIT_DECLARATION_REQUIRED");detail["capability_profile"]=declaration->id;detail["capabilities"]=declaration->inputDeclaration;
   return {{"status","DISCOVERED"},{"initial_pose_floor",floor},{"initial_state_floor",stateFloor?Json(*stateFloor):Json(nullptr)},{"reference_serial",witness.referenceSerial},{"source_session",witness.session},{"source_clock",witness.clock},{"witness",std::move(detail)}};
  }
  if(op=="confirm_target"){
   need(state==DISCOVERED,"STATE_REJECTED");guards(true);confirmation=opaqueId();confirmationDeadline=now()+10*second;state=CONFIRMED;
   return {{"status","CONFIRMED"},{"confirmation_token",confirmation},{"confirmation_deadline_ns",confirmationDeadline}};
  }
  if(op=="begin"){
   need(!approvalPurpose,"OUTPUT_APPROVAL_SESSION_NO_MEASUREMENT");
   need(state==CONFIRMED&&text(j,"confirmation_token")==confirmation&&!confirmation.empty(),"CONFIRMATION_TOKEN_INVALID");
   beginReady();guards(true,true,confirmationDeadline);
   if(env.ioHook)env.ioHook("begin-barrier-acked");beginReady();
   drafts.begin(run);createdDraft=true;
   if(env.ioHook)env.ioHook("begin-draft-created");beginReady();
   drafts.write("expected.json",original);
   if(env.ioHook)env.ioHook("begin-draft-written");beginReady();
   confirmation.clear();if(staticMode){staticStart=now();staticMeasurement=std::make_unique<StaticMeasurement>(*frozen,input->session,staticOptions);return waitingPoint();}
   recorder=std::make_unique<TrajectoryRecorder>(config,*frozen,input->referenceSerial);state=RECORDING;
   if(env.progress)env.progress({{"status","PHASE"},{"phase","RECORDING"},{"session_id",session},{"run_id",run}});
   std::int64_t progressAt=-1;
   try{owner->record([&](const RecordingDelivery& delivery){checkCancel();record(delivery);if(env.progress&&(progressAt<0||now()-progressAt>=second/4)){progressAt=now();env.progress({{"status","PROGRESS"},{"phase","RECORDING"},{"session_id",session},{"run_id",run},{"samples",recorder->sourceSamples().size()}});}});}catch(const std::exception&){checkCancel();auto fault=owner->snapshot().fault;if(!fault.empty())invalidate(fault.c_str());throw;}checkCancel();guards(true);

   Json raw={{"source",Json::array()},{"reference",Json::array()}};
   for(const auto& s:recorder->sourceSamples())raw["source"].push_back({{"time_ns",s.timeNs},{"valid",s.valid},{"pose",rigid(s.pose)}});
   for(const auto& r:recorder->referenceSamples())raw["reference"].push_back({{"time_ns",r.timeNs},{"valid",r.valid},{"pose",rigid(r.pose)}});
   drafts.write("raw.json",raw.dump());
   state=SOLVING;auto solveStart=now();++solves;
   if(env.progress)env.progress({{"status","PHASE"},{"phase","SOLVING"},{"session_id",session},{"run_id",run}});
   if(env.ioHook)env.ioHook("solve-before");
   auto solved=recorder->solve();checkCancel();need(now()-solveStart<10*second,"SOLVE_TIMEOUT");guards(true);
   result=std::make_unique<const TrajectoryResult>(std::move(solved));resultId=opaqueId();resultDeadline=now()+10*60*second;
   drafts.write("result.json",resultJson().dump());current();
   auto resultBarrier=owner->barrier("result:"+resultId);{auto admission=owner->admit(resultBarrier,now());state=RESULT_READY;}
   return {{"status","RESULT_READY"},{"result",resultJson()}};
  }
  need(state==RESULT_READY||state==PREPARED,"STATE_REJECTED");
  need(text(j,"result_id")==resultId,"WRONG_RESULT");
  try{guards(true);}catch(...){review.clear();if(state==PREPARED)state=RESULT_READY;throw;}
  try{artifactCheck();}catch(...){invalidate("DRAFT_ARTIFACT_CHANGED");}
  if(op=="prepare_commit"){
   need(!providerOptions->readOnly(),"READONLY_COMMIT_REJECTED");
   if(candidate.empty()){
    auto proposed=worldCalibrationCandidate(config,frozen->target(),staticResult?staticResult->transform:result->world,true);
    const auto& s=frozen->target().binding;
    for(auto& [key,b]:proposed.bindings)if(b.source==s.source&&b.inputSpace==s.inputSpace&&b.inputRevision==s.inputRevision)b.spaceApproved=false;
    proposed.sourcePath.clear();drafts.publishCandidate(proposed,env.draftPublishHook);
    candidate=readConfigBytes(drafts.path("candidate.json"));candidateHash=sha256(candidate);
   }
   review=opaqueId();reviewDeadline=now()+30*second;state=PREPARED;
   return {{"status","PREPARED"},{"result_id",resultId},{"review_token",review},{"review_deadline_ns",reviewDeadline},{"candidate_sha256",candidateHash},{"candidate_bytes",candidate.size()},{"candidate_path",drafts.path("candidate.json").u8string()},{"output_approval","REQUIRED"}};
  }
  need(op=="commit"&&state==PREPARED&&!providerOptions->readOnly(),"COMMIT_NOT_PREPARED_OR_READONLY");
  need(!review.empty()&&text(j,"review_token")==review&&text(j,"candidate_sha256")==candidateHash,"REVIEW_TOKEN_CANDIDATE_MISMATCH");
  if(now()>=reviewDeadline){review.clear();state=RESULT_READY;throw std::runtime_error("REVIEW_TOKEN_EXPIRED_REPREPARE");}
  try{
   checkCancel();current();
   auto beforeBarrier=now();auto remaining=std::min({10*second,reviewDeadline-beforeBarrier,resultDeadline-beforeBarrier,created+20*60*second-beforeBarrier});
   need(remaining>0,"REVIEW_TOKEN_EXPIRED_REPREPARE");
   auto commitBarrier=owner->barrier(text(j,"request_id"),remaining);
   if(env.ioHook)env.ioHook("precommit-barrier-acked");
   auto admissionAt=now();checkCancel();need(admissionAt<reviewDeadline,"REVIEW_TOKEN_EXPIRED_REPREPARE");need(admissionAt<resultDeadline&&admissionAt-created<20*60*second,"RESULT_REVIEW_EXPIRED");
   auto admission=owner->admit(commitBarrier,admissionAt);state=COMMITTING;
  }catch(const std::exception& e){
   review.clear();if(state==PREPARED)state=RESULT_READY;
   if(!owner->snapshot().fault.empty()){auto reason=diagnostic(e);invalidate(reason.c_str());}throw;
  }
  review.clear();auto started=now();++commits;
  try{
   commitConfigDocument(providerOptions->configPath(),original,candidate,[&](const char* stage){if(env.transactionHook)env.transactionHook(stage);if(cancel)evidenceStale=true;try{guards(false,false);}catch(...){evidenceStale=true;}});
   if(env.afterReplace)env.afterReplace();
   try{guards(true,false);}catch(...){evidenceStale=true;}
   if(now()-started>=15*second){state=UNKNOWN;drafts.terminal("UNKNOWN");return {{"status","UNKNOWN"},{"reason","COMMIT_TIMEOUT_NO_RETRY"}};}
   if(cancel)evidenceStale=true;
   if(readConfigBytes(providerOptions->configPath())!=candidate)evidenceStale=true;
   state=evidenceStale?SAVED_STALE:SAVED;
  }catch(const std::exception& e){state=UNKNOWN;try{drafts.terminal("UNKNOWN");}catch(...){}return {{"status","UNKNOWN"},{"reason",diagnostic(e)}};}
  Json saved={{"status",name(state)},{"config_sha256",candidateHash},{"output_approval","REQUIRED"},{"runtime","UNCONFIRMED"},{"message","SAVED CONFIG / OUTPUT REAPPROVAL REQUIRED / RUNTIME UNCONFIRMED"}};
  try{drafts.write("terminal.json",saved.dump());drafts.terminal(name(state));}catch(...){saved["artifact_warning"]="TERMINAL_ARTIFACT_IO_FAILED";}
  return saved;
 }
};
CalibrationSession::CalibrationSession(CaptureProvider& p,SessionEnvironment e):impl_(std::make_unique<Impl>(p,std::move(e))){}
CalibrationSession::~CalibrationSession()=default;
Json CalibrationSession::execute(const Json& j){
 auto& x=*impl_;schema(j);const auto id=text(j,"request_id"),canonical=j.dump(),op=text(j,"op");
 bool expired=false;try{expired=x.now()-x.created>=20*60*second;}catch(const std::exception& e){return {{"status","REJECTED"},{"reason",diagnostic(e)},{"phase",phase()},{"eligible",false}};}
 const bool savedOutcome=x.state==SAVED||x.state==SAVED_STALE||x.state==UNKNOWN;
 if(expired&&!savedOutcome){
  bool warning=false;if(!terminalPhase(x.state)){try{x.invalidate("WORKER_TTL_EXPIRED");}catch(...){}if(x.createdDraft)try{x.terminalArtifact();}catch(...){warning=true;}}
  Json out={{"status","REJECTED"},{"reason","WORKER_TTL_EXPIRED"},{"phase",phase()},{"eligible",false}};if(warning)out["artifact_warning"]="TERMINAL_IO_FAILED";return out;
 }
 std::unique_lock<std::mutex> cacheLock(x.cacheMutex);
 auto hit=x.cache.find(id);
 if(hit!=x.cache.end()){
  if(hit->second.canonical!=canonical)return {{"status","REJECTED"},{"reason","REQUEST_ID_REUSE_MISMATCH"},{"phase",phase()},{"eligible",false}};
  if(hit->second.outcome.is_null())return {{"status","IN_PROGRESS"},{"phase",phase()},{"replayed",true}};
  auto cached=hit->second.outcome;cacheLock.unlock();
  bool eligible=false;if(x.state>=DISCOVERED&&x.state<=PREPARED)try{x.guards(true);eligible=x.state==PREPARED&&x.now()<x.reviewDeadline&&!x.cancel;}catch(...){}
  auto out=cached;if(!x.firstFailure.empty())out["first_failure"]=x.firstFailure;out["replayed"]=true;out["current_phase"]=phase();out["eligible"]=eligible;if(expired)out["worker_lifetime_expired"]=true;return out;
 }
 if(expired)return {{"status","REJECTED"},{"reason","WORKER_TTL_EXPIRED"},{"phase",phase()},{"eligible",false}};
 bool reserved=op=="cancel"||op=="close";
 if((reserved?x.reservedIds>=2:x.ordinaryIds>=126)||canonical.size()>64*KiB||x.cacheBytes+canonical.size()+16*KiB>10*MiB)return {{"status","REJECTED"},{"reason","REQUEST_CACHE_LIMIT"},{"phase",phase()}};
 if(reserved)++x.reservedIds;else ++x.ordinaryIds;
 x.cacheBytes+=canonical.size()+16*KiB;x.cache.emplace(id,Impl::Entry{canonical,nullptr});cacheLock.unlock();
 Json out;
 try{out=x.action(j);}catch(const std::exception& e){out={{"status","REJECTED"},{"reason",diagnostic(e)}};if(x.state==RECORDING||x.state==SOLVING)x.state=FAILED;}
 if(!x.firstFailure.empty())out["first_failure"]=x.firstFailure;
 out["session_id"]=x.session;out["run_id"]=x.run;out["phase"]=phase();out["replayed"]=false;
 if(x.createdDraft)try{x.drafts.journal({{"request_id",id},{"op",op},{"phase",phase()},{"status",out.at("status")},{"monotonic_ns",x.now()}});if(terminalPhase(x.state))x.terminalArtifact();}catch(...){if(terminalPhase(x.state))out["artifact_warning"]="JOURNAL_IO_FAILED";else{x.state=INSPECT_ONLY;out={{"status","REJECTED"},{"reason","JOURNAL_IO_FAILED"},{"phase",phase()}};}}
 if(out.dump().size()>16*KiB){if(x.state==SAVED||x.state==SAVED_STALE||x.state==UNKNOWN)out={{"status",name(x.state)},{"reason","OUTCOME_LIMIT"},{"runtime","UNCONFIRMED"},{"output_approval","REQUIRED"}};else{x.state=INSPECT_ONLY;out={{"status","REJECTED"},{"reason","OUTCOME_LIMIT"},{"phase",phase()}};}}
 cacheLock.lock();x.cache.at(id).outcome=out;return out;
}
void CalibrationSession::tick(){auto& x=*impl_;if(terminalPhase(x.state))return;try{x.checkCancel();if(x.now()-x.created>=20*60*second)x.invalidate("WORKER_TTL_EXPIRED");if(x.state>=DISCOVERED&&x.state<=PREPARED)x.guards(false,false);}catch(...){if(!terminalPhase(x.state))x.state=INSPECT_ONLY;if(x.createdDraft)try{x.terminalArtifact();}catch(...){}}}
void CalibrationSession::requestCancel(bool eof){if(eof)impl_->eof=true;impl_->cancel=true;}
void CalibrationSession::latchControl(const Json& j){
 schema(j);auto& x=*impl_;need((j.at("op")=="cancel"||j.at("op")=="close")&&text(j,"session_id")==x.session&&text(j,"run_id")==x.run,"WRONG_SESSION_RUN");
 std::lock_guard<std::mutex> lock(x.cacheMutex);auto it=x.cache.find(text(j,"request_id"));
 if(it!=x.cache.end())need(it->second.canonical==j.dump(),"REQUEST_ID_REUSE_MISMATCH");
 else need(x.reservedIds<2,"REQUEST_CACHE_LIMIT");
 if(j.at("op")=="close")x.eof=true;x.cancel=true;
}
std::string CalibrationSession::phase()const{return name(impl_->state);}
bool CalibrationSession::terminal()const{return terminalPhase(impl_->state);}
bool CalibrationSession::lifetimeExpired()const{return impl_->env.clock()-impl_->created>=20*60*second;}
std::string CalibrationSession::sessionId()const{return impl_->session;}
std::string CalibrationSession::runId()const{return impl_->run;}
const InputWitness* CalibrationSession::frozenInput()const{return impl_->input.get();}
std::pair<std::int64_t,std::optional<std::int64_t>> CalibrationSession::highwaters()const{if(!impl_->owner)return {-1,{}};auto s=impl_->owner->snapshot();return {s.poseHigh,s.stateHigh};}
CaptureSnapshot CalibrationSession::captureSnapshot()const{return impl_->owner?impl_->owner->snapshot():CaptureSnapshot{};}
Json CalibrationSession::invalidationDiagnostic()const{return impl_->failure();}
std::size_t CalibrationSession::solveCalls()const{return impl_->solves;}
std::size_t CalibrationSession::commitCalls()const{return impl_->commits;}
} // namespace mb::local_calibration
