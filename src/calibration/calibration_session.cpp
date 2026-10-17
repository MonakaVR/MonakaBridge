#include "monaka_bridge/calibration_session.hpp"
#include "monaka_bridge/calibration_capture_monitor.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include <algorithm>
#include <set>

namespace mb::local_calibration {
std::string diagnostic(const std::exception& error){std::string s;const char* p=error.what();for(std::size_t i=0;i<512&&p[i];++i)s+=p[i];return Json::parse(Json(s).dump(-1,' ',false,Json::error_handler_t::replace)).get<std::string>();}
namespace {
void need(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
enum Phase {NEW,OPEN,DISCOVERED,CONFIRMED,RECORDING,SOLVING,RESULT_READY,PREPARED,COMMITTING,SAVED,SAVED_STALE,CANCELLED,CLOSED,FAILED,INSPECT_ONLY,UNKNOWN};
const char* name(Phase p){return std::array<const char*,16>{"NEW","OPEN","DISCOVERED","CONFIRMED","RECORDING","SOLVING","RESULT_READY","PREPARED","COMMITTING","SAVED","SAVED_CONFIG_EVIDENCE_STALE","CANCELLED","CLOSED","FAILED","INSPECT_ONLY","UNKNOWN"}.at(p);}
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
 }else{
  need(std::set<std::string>{"discover","confirm_target","begin","capture_point","cancel","prepare_commit","commit","close"}.count(op),"UNSUPPORTED_OPERATION");
  for(auto f:{"session_id","run_id"}){text(j,f);allowed.insert(f);}
  if(op=="begin"){text(j,"confirmation_token");allowed.insert("confirmation_token");}
  if(op=="prepare_commit"||op=="commit"){text(j,"result_id");allowed.insert("result_id");}
  if(op=="commit")for(auto f:{"review_token","candidate_sha256"}){text(j,f);allowed.insert(f);}
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
 std::string original,confirmation,review,resultId,candidate,candidateHash;
 Config config;std::shared_ptr<const ProviderOpenOptions> providerOptions;
 const FrozenMeasurementTarget* frozen=nullptr; // Alias into the owning options.
 std::unique_ptr<const InputWitness> input;
 std::unique_ptr<const TrajectoryResult> result;
 std::unique_ptr<TrajectoryRecorder> recorder;
 std::unique_ptr<CaptureOwner> owner;
 bool evidenceStale=false,createdDraft=false;
 std::size_t solves=0,commits=0,cacheBytes=0,ordinaryIds=0,reservedIds=0;
 struct Entry {std::string canonical;Json outcome;};std::map<std::string,Entry> cache;std::mutex cacheMutex;
 explicit Impl(CaptureProvider& p,SessionEnvironment e):provider(p),env(std::move(e)),drafts(env.draftRoot),created(env.clock()),lastNow(created){}
 std::int64_t now(){auto t=env.clock();if(t<lastNow){if(state==COMMITTING)evidenceStale=true;else{state=INSPECT_ONLY;confirmation.clear();review.clear();}throw std::runtime_error("MONOTONIC_CLOCK_ROLLBACK");}lastNow=t;return t;}
 ~Impl(){cancel=true;if(owner)owner->shutdown();}
 void invalidate(const char* reason){if(owner)owner->invalidate(reason);if(state==COMMITTING){evidenceStale=true;return;}state=INSPECT_ONLY;confirmation.clear();review.clear();throw std::runtime_error(reason);}
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
 void live(bool fresh){
  auto t=now();checkCancel();
  if(t-created>=20*60*second)invalidate("WORKER_TTL_EXPIRED");
  if(result&&t>=resultDeadline)invalidate("RESULT_REVIEW_EXPIRED");
  if(owner)try{owner->check(t,fresh);}catch(const std::exception& e){if(!owner->snapshot().fault.empty()){auto reason=diagnostic(e);invalidate(reason.c_str());}throw;}
 }
 void guards(bool fresh,bool scope=true){
  if(scope)try{current();}catch(const std::exception& e){auto reason=diagnostic(e);invalidate(reason.c_str());}
  if(owner)try{owner->barrier(opaqueId());}catch(const std::exception& e){auto reason=diagnostic(e);invalidate(reason.c_str());}
  live(fresh);
 }
 void artifactCheck(){
  drafts.verify();
  need(readConfigBytes(drafts.path("expected.json"))==original,"EXPECTED_ARTIFACT_CHANGED");
  need(readConfigBytes(drafts.path("result.json"))==resultJson().dump(),"RESULT_ARTIFACT_CHANGED");
  if(!candidate.empty())need(readConfigBytes(drafts.path("candidate.json"))==candidate,"CANDIDATE_ARTIFACT_CHANGED");
 }
 Json resultJson()const{
  need(bool(result),"NO_RESULT");return {{"result_id",resultId},{"world",rigid(result->world)},{"extrinsic_not_mount",rigid(result->extrinsic)},{"pairs",result->pairs},{"inliers",result->inliers},{"rms_position",result->residuals.rmsPosition},{"rms_orientation",result->residuals.rmsOrientation},{"initial_pose_floor",input->initialPoseFloor},{"initial_state_floor",input->initialStateFloor?Json(*input->initialStateFloor):Json(nullptr)},{"config_sha256",providerOptions->configSha256()},{"association","operator_declared / process_owner_unverified"},{"review_deadline_ns",resultDeadline}};
 }
 Json action(const Json& j){
  auto op=text(j,"op");auto t=now();if(op!="cancel"&&op!="close")checkCancel();
  if(t-created>=20*60*second&&state!=COMMITTING)invalidate("WORKER_TTL_EXPIRED");
  if(op=="open"){
   need(state==NEW,"STATE_REJECTED");need(j.at("helper_path")==env.helperPath&&j.at("helper_sha256")==env.helperSha256,"HELPER_MISMATCH");
   auto path=std::filesystem::u8path(text(j,"config_path"));requirePlainPath(path);auto installation=std::filesystem::u8path(text(j,"installation_path"));requirePlainPath(installation);need(std::filesystem::is_directory(installation),"INSTALLATION_UNAVAILABLE");
   need(j.at("association")=="operator_declared_process_owner_unverified"&&j.at("mirror_address")=="127.0.0.1"&&j.at("mirror_port").get<std::uint64_t>()>0&&j.at("mirror_port").get<std::uint64_t>()<=65535,"ASSOCIATION_UNSUPPORTED");
   need(j.at("reference_role")=="left"||j.at("reference_role")=="right"||j.at("reference_role")=="hmd","REFERENCE_ROLE_UNSUPPORTED");
   auto nextOriginal=readConfigBytes(path);auto nextConfig=parseConfigDocument(nextOriginal);nextConfig.sourcePath=std::filesystem::weakly_canonical(path);
   need(j.at("bridge_id")==nextConfig.bridgeId&&j.at("input_revision").get<std::uint64_t>()<=UINT32_MAX,"TARGET_OWNER_REVISION_MISMATCH");
   auto target=freezeMeasurementTarget(nextConfig,text(j,"source"),text(j,"device"),text(j,"tracker"),text(j,"input_space"),j.at("input_revision").get<std::uint32_t>());
   auto options=CalibrationSession::makeProviderOptions(nextConfig,std::move(target),{path,installation,sha256(nextOriginal),text(j,"mirror_address"),text(j,"reference_role"),j.at("mirror_port").get<std::uint16_t>(),j.at("readonly").get<bool>()});
   Json reply={{"status","OPEN"},{"config_sha256",options->configSha256()},{"capture_supported",provider.supported()},{"readonly",options->readOnly()},{"association",options->association()}};
   try{provider.configure(options);}catch(...){state=FAILED;throw;}
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
  if(op=="capture_point")throw std::runtime_error("STATIC_CAPTURE_UNSUPPORTED_B4_REQUIRED");
  if(op=="discover"){
   need(state==OPEN,"STATE_REJECTED");need(provider.supported(),"CAPTURE_UNSUPPORTED_B3_REQUIRED");current();
   owner=std::make_unique<CaptureOwner>(provider,env.clock,cancel,created,run);
   InputWitness witness;try{witness=owner->discover();}catch(const std::exception& e){auto reason=diagnostic(e);invalidate(reason.c_str());}
   need(now()-t<10*second,"DISCOVER_TIMEOUT");checkCancel();
   input=std::make_unique<const InputWitness>(witness);live(true);
   const auto floor=witness.initialPoseFloor;const auto stateFloor=witness.initialStateFloor;state=DISCOVERED;
   return {{"status","DISCOVERED"},{"initial_pose_floor",floor},{"initial_state_floor",stateFloor?Json(*stateFloor):Json(nullptr)},{"reference_serial",witness.referenceSerial},{"source_session",witness.session},{"source_clock",witness.clock}};
  }
  if(op=="confirm_target"){
   need(state==DISCOVERED,"STATE_REJECTED");guards(true);confirmation=opaqueId();confirmationDeadline=now()+10*second;state=CONFIRMED;
   return {{"status","CONFIRMED"},{"confirmation_token",confirmation},{"confirmation_deadline_ns",confirmationDeadline}};
  }
  if(op=="begin"){
   need(state==CONFIRMED&&text(j,"confirmation_token")==confirmation&&!confirmation.empty(),"CONFIRMATION_TOKEN_INVALID");
   if(now()>=confirmationDeadline)invalidate("CONFIRMATION_EXPIRED");guards(true);
   drafts.begin(run);createdDraft=true;drafts.write("expected.json",original);
   confirmation.clear();recorder=std::make_unique<TrajectoryRecorder>(config,*frozen,input->referenceSerial);state=RECORDING;
   std::int64_t progressAt=-1;
   try{owner->record([&](const RecordingDelivery& delivery){checkCancel();record(delivery);if(env.progress&&(progressAt<0||now()-progressAt>=second/4)){progressAt=now();env.progress({{"status","PROGRESS"},{"phase","RECORDING"},{"session_id",session},{"run_id",run},{"samples",recorder->sourceSamples().size()}});}});}catch(const std::exception&){checkCancel();auto fault=owner->snapshot().fault;if(!fault.empty())invalidate(fault.c_str());throw;}checkCancel();guards(true);

   Json raw={{"source",Json::array()},{"reference",Json::array()}};
   for(const auto& s:recorder->sourceSamples())raw["source"].push_back({{"time_ns",s.timeNs},{"valid",s.valid},{"pose",rigid(s.pose)}});
   for(const auto& r:recorder->referenceSamples())raw["reference"].push_back({{"time_ns",r.timeNs},{"valid",r.valid},{"pose",rigid(r.pose)}});
   drafts.write("raw.json",raw.dump());
   state=SOLVING;auto solveStart=now();++solves;
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
    auto proposed=worldCalibrationCandidate(config,frozen->target(),result->world,true);
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
  checkCancel();current();
  auto beforeBarrier=now();auto remaining=std::min({10*second,reviewDeadline-beforeBarrier,resultDeadline-beforeBarrier,created+20*60*second-beforeBarrier});
  need(remaining>0,"REVIEW_TOKEN_EXPIRED_REPREPARE");
  auto commitBarrier=owner->barrier(text(j,"request_id"),remaining);
  if(env.ioHook)env.ioHook("precommit-barrier-acked");
  auto admissionAt=now();need(admissionAt<reviewDeadline,"REVIEW_TOKEN_EXPIRED_REPREPARE");need(admissionAt<resultDeadline&&admissionAt-created<20*60*second,"RESULT_REVIEW_EXPIRED");
  try{auto admission=owner->admit(commitBarrier,admissionAt);state=COMMITTING;}catch(const std::exception& e){if(!owner->snapshot().fault.empty()){auto reason=diagnostic(e);invalidate(reason.c_str());}throw;}
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
  bool warning=false;if(!terminalPhase(x.state)){try{x.invalidate("WORKER_TTL_EXPIRED");}catch(...){}if(x.createdDraft)try{x.drafts.terminal(name(x.state));}catch(...){warning=true;}}
  Json out={{"status","REJECTED"},{"reason","WORKER_TTL_EXPIRED"},{"phase",phase()},{"eligible",false}};if(warning)out["artifact_warning"]="TERMINAL_IO_FAILED";return out;
 }
 std::unique_lock<std::mutex> cacheLock(x.cacheMutex);
 auto hit=x.cache.find(id);
 if(hit!=x.cache.end()){
  if(hit->second.canonical!=canonical)return {{"status","REJECTED"},{"reason","REQUEST_ID_REUSE_MISMATCH"},{"phase",phase()},{"eligible",false}};
  if(hit->second.outcome.is_null())return {{"status","IN_PROGRESS"},{"phase",phase()},{"replayed",true}};
  auto cached=hit->second.outcome;cacheLock.unlock();
  bool eligible=false;if(x.state>=DISCOVERED&&x.state<=PREPARED)try{x.guards(true);eligible=x.state==PREPARED&&x.now()<x.reviewDeadline&&!x.cancel;}catch(...){}
  auto out=cached;out["replayed"]=true;out["current_phase"]=phase();out["eligible"]=eligible;if(expired)out["worker_lifetime_expired"]=true;return out;
 }
 if(expired)return {{"status","REJECTED"},{"reason","WORKER_TTL_EXPIRED"},{"phase",phase()},{"eligible",false}};
 bool reserved=op=="cancel"||op=="close";
 if((reserved?x.reservedIds>=2:x.ordinaryIds>=126)||canonical.size()>64*KiB||x.cacheBytes+canonical.size()+16*KiB>10*MiB)return {{"status","REJECTED"},{"reason","REQUEST_CACHE_LIMIT"},{"phase",phase()}};
 if(reserved)++x.reservedIds;else ++x.ordinaryIds;
 x.cacheBytes+=canonical.size()+16*KiB;x.cache.emplace(id,Impl::Entry{canonical,nullptr});cacheLock.unlock();
 Json out;
 try{out=x.action(j);}catch(const std::exception& e){out={{"status","REJECTED"},{"reason",diagnostic(e)}};if(x.state==RECORDING||x.state==SOLVING)x.state=FAILED;}
 out["session_id"]=x.session;out["run_id"]=x.run;out["phase"]=phase();out["replayed"]=false;
 if(x.createdDraft)try{x.drafts.journal({{"request_id",id},{"op",op},{"phase",phase()},{"status",out.at("status")},{"monotonic_ns",x.now()}});if(terminalPhase(x.state))x.drafts.terminal(name(x.state));}catch(...){if(terminalPhase(x.state))out["artifact_warning"]="JOURNAL_IO_FAILED";else{x.state=INSPECT_ONLY;out={{"status","REJECTED"},{"reason","JOURNAL_IO_FAILED"},{"phase",phase()}};}}
 if(out.dump().size()>16*KiB){if(x.state==SAVED||x.state==SAVED_STALE||x.state==UNKNOWN)out={{"status",name(x.state)},{"reason","OUTCOME_LIMIT"},{"runtime","UNCONFIRMED"},{"output_approval","REQUIRED"}};else{x.state=INSPECT_ONLY;out={{"status","REJECTED"},{"reason","OUTCOME_LIMIT"},{"phase",phase()}};}}
 cacheLock.lock();x.cache.at(id).outcome=out;return out;
}
void CalibrationSession::tick(){auto& x=*impl_;if(terminalPhase(x.state))return;try{x.checkCancel();if(x.now()-x.created>=20*60*second)x.invalidate("WORKER_TTL_EXPIRED");if(x.state>=DISCOVERED&&x.state<=PREPARED)x.guards(false,false);}catch(...){if(!terminalPhase(x.state))x.state=INSPECT_ONLY;if(x.createdDraft)try{x.drafts.terminal(name(x.state));}catch(...){}}}
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
std::size_t CalibrationSession::solveCalls()const{return impl_->solves;}
std::size_t CalibrationSession::commitCalls()const{return impl_->commits;}
} // namespace mb::local_calibration
