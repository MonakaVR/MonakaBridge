#include "monaka_bridge/calibration_session.hpp"
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
bool sameIdentity(const InputWitness& a,const InputWitness& b){return a.source==b.source&&a.device==b.device&&a.session==b.session&&a.clock==b.clock&&a.space==b.space&&a.convention==b.convention&&a.revision==b.revision&&a.peer==b.peer&&a.referenceSerial==b.referenceSerial&&a.referenceRole==b.referenceRole&&a.referenceGeneration==b.referenceGeneration&&a.standing==b.standing;}
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
struct Counter {
 std::optional<std::int64_t> high;
 std::deque<std::pair<std::int64_t,std::string>> recent;
 bool accept(std::int64_t sequence,const std::string& bytes){
  for(const auto& [s,b]:recent)if(s==sequence){need(b==bytes,"SEQUENCE_CONTENT_CONFLICT");return false;}
  if(high&&sequence<=*high)return false;
  high=sequence;recent.emplace_back(sequence,bytes);if(recent.size()>128)recent.pop_front();return true;
 }
};
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

struct CalibrationSession::Impl {
 CaptureProvider& provider;SessionEnvironment env;DraftStore drafts;
 std::atomic<Phase> state{NEW};std::atomic<bool> cancel{false},eof{false};
 const std::string session=opaqueId(),run=opaqueId();
 std::int64_t created,lastNow,confirmationDeadline=0,resultDeadline=0,reviewDeadline=0;
 std::string original,configHash,configPath,confirmation,review,resultId,candidate,candidateHash,referenceRole;
 Config config;std::unique_ptr<const FrozenMeasurementTarget> frozen;
 std::unique_ptr<const InputWitness> input;
 std::unique_ptr<const TrajectoryResult> result;
 std::unique_ptr<TrajectoryRecorder> recorder;
 Counter poses,states;std::int64_t lastValidPose=-1,referenceAt=-1,lastValidReference=-1,lastPoseReceipt=-1,lastStateReceipt=-1;
 bool poseValid=false,referenceValid=false,readonly=true,evidenceStale=false,createdDraft=false;
 std::size_t solves=0,commits=0,cacheBytes=0,ordinaryIds=0,reservedIds=0;
 struct Entry {std::string canonical;Json outcome;};std::map<std::string,Entry> cache;std::mutex cacheMutex;
 explicit Impl(CaptureProvider& p,SessionEnvironment e):provider(p),env(std::move(e)),drafts(env.draftRoot),created(env.clock()),lastNow(created){}
 std::int64_t now(){auto t=env.clock();if(t<lastNow){if(state==COMMITTING)evidenceStale=true;else{state=INSPECT_ONLY;confirmation.clear();review.clear();}throw std::runtime_error("MONOTONIC_CLOCK_ROLLBACK");}lastNow=t;return t;}
 void invalidate(const char* reason){if(state==COMMITTING){evidenceStale=true;return;}state=INSPECT_ONLY;confirmation.clear();review.clear();throw std::runtime_error(reason);}
 void checkCancel(){if(cancel&&state!=COMMITTING){state=eof?CLOSED:CANCELLED;confirmation.clear();review.clear();throw std::runtime_error(eof?"EOF_CLOSED":"CANCELLED");}}
 void current(){
  requirePlainPath(std::filesystem::u8path(env.helperPath));if(sha256(readConfigBytes(std::filesystem::u8path(env.helperPath)))!=env.helperSha256)invalidate("HELPER_HASH_CHANGED");
  requirePlainPath(std::filesystem::u8path(configPath));
  auto raw=readConfigBytes(std::filesystem::u8path(configPath));
  if(raw!=original)invalidate("CONFIG_EXACT_BYTES_CHANGED");
  auto c=parseConfigDocument(raw);validateFrozenMeasurementTarget(c,*frozen);
 }
 void validateBatchIdentity(const ProviderBatch& b){
  if(!sameIdentity(*input,b.identity)||b.eventOverflow)invalidate("INPUT_REFERENCE_FRAME_CONFLICT");
  if(b.absent)invalidate("EXPLICIT_ABSENCE");
  auto t=now();if(b.referenceReceiptNs>t||b.referenceReceiptNs<0||(referenceAt>=0&&b.referenceReceiptNs<referenceAt))invalidate("REFERENCE_RECEIPT_CLOCK_CHANGED");
  referenceAt=b.referenceReceiptNs;referenceValid=b.referenceValid;
  if(referenceValid&&t-referenceAt<=50000000){if(lastValidReference>=0&&referenceAt-lastValidReference>=second)invalidate("REFERENCE_LIVENESS_GAP");lastValidReference=referenceAt;}
 }
 void consume(const ProviderBatch& b,bool recording){
  need(b.frames.size()<=128&&b.references.size()<=128,"PROVIDER_BATCH_LIMIT");
  validateBatchIdentity(b);if(state!=COMMITTING)current();
  const auto& selected=frozen->target().binding;
  const auto& declaration=config.contracts.at({selected.source,selected.device}).inputDeclaration;
  for(const auto& f:b.frames){
   auto handle=[&](const auto& p,bool pose){
    if(p.source_id!=input->source||p.device_id!=input->device)return;
    if(p.session_id!=input->session||p.clock_id!=input->clock||p.coordinate_space.id!=input->space||p.coordinate_space.convention!=input->convention||p.coordinate_space.revision!=input->revision||f.peer!=input->peer)invalidate("SELECTED_LIFETIME_PEER_SPACE_CONFLICT");
    std::string raw;c1::Error error;need(c1::EncodeEnvelope(f.envelope,raw,error),"INVALID_PROVIDER_ENVELOPE");
    need(raw.size()<=4096,"PROVIDER_ENVELOPE_LIMIT");
    if(monaka::input::CapabilitySet(p.capabilities)!=monaka::input::CapabilitySet(declaration))invalidate("DECLARATION_SCOPE_CHANGED");
    auto& counter=pose?poses:states;if(!counter.accept(p.sequence,raw))return;
    auto t=now();need(f.receiptNs>=0&&f.receiptNs<=t,"INVALID_RECEIPT_TIME");
    auto& receipt=pose?lastPoseReceipt:lastStateReceipt;if(f.receiptNs<receipt)invalidate("RECEIPT_CLOCK_BACKWARD");receipt=f.receiptNs;
    const auto age=p.sent_at_ns-p.timestamp_ns;
    bool fresh=p.timestamp_ns>=0&&p.sent_at_ns>=p.timestamp_ns&&age<=50000000&&t-f.receiptNs<=50000000;
    using P=std::decay_t<decltype(p)>;
    if constexpr(std::is_same_v<P,c1::TrackerObservation>){
     bool valid=fresh&&p.modality=="full"&&p.tracking_state=="tracked"&&p.validity.position&&p.validity.orientation;
     if(valid&&lastValidPose>=0&&f.receiptNs-lastValidPose>=second)invalidate("SOURCE_LIVENESS_GAP");
     poseValid=valid;if(valid)lastValidPose=f.receiptNs;
     if(recording){auto recorded=p;if(!valid){recorded.validity.position=false;recorded.validity.orientation=false;}recorder->observation(recorded,f.receiptNs);}
    }else{
     if(p.presence=="absent")invalidate("EXPLICIT_ABSENCE");
     if(recording)recorder->deviceState(p);
    }
   };
   if(auto p=std::get_if<c1::TrackerObservation>(&f.envelope))handle(*p,true);
   else if(auto p=std::get_if<c1::ObservationDeviceState>(&f.envelope))handle(*p,false);
  }
  if(recording)for(auto r:b.references){auto t=now();need(r.timeNs>=0&&r.timeNs<=t,"INVALID_REFERENCE_SAMPLE_TIME");r.valid=r.valid&&t-r.timeNs<=50000000;recorder->reference(r,input->referenceSerial);}
  live(false);
 }
 void live(bool fresh){
  auto t=now();checkCancel();
  if(t-created>=20*60*second)invalidate("WORKER_TTL_EXPIRED");
  if(lastValidPose<0||t-lastValidPose>=second||lastValidReference<0||t-lastValidReference>=second)invalidate("LIVENESS_GAP");
  if(result&&t>=resultDeadline)invalidate("RESULT_REVIEW_EXPIRED");
  if(fresh)need(poseValid&&referenceValid&&t-lastValidPose<=50000000&&t-referenceAt<=50000000,"NOT_READY_STALE");
 }
 void guards(bool fresh){consume(provider.guard(),false);live(fresh);}
 void artifactCheck(){
  drafts.verify();
  need(readConfigBytes(drafts.path("expected.json"))==original,"EXPECTED_ARTIFACT_CHANGED");
  need(readConfigBytes(drafts.path("result.json"))==resultJson().dump(),"RESULT_ARTIFACT_CHANGED");
  if(!candidate.empty())need(readConfigBytes(drafts.path("candidate.json"))==candidate,"CANDIDATE_ARTIFACT_CHANGED");
 }
 Json resultJson()const{
  need(bool(result),"NO_RESULT");return {{"result_id",resultId},{"world",rigid(result->world)},{"extrinsic_not_mount",rigid(result->extrinsic)},{"pairs",result->pairs},{"inliers",result->inliers},{"rms_position",result->residuals.rmsPosition},{"rms_orientation",result->residuals.rmsOrientation},{"initial_pose_floor",input->initialPoseFloor},{"initial_state_floor",input->initialStateFloor?Json(*input->initialStateFloor):Json(nullptr)},{"config_sha256",configHash},{"association","operator_declared / process_owner_unverified"},{"review_deadline_ns",resultDeadline}};
 }
 Json action(const Json& j){
  auto op=text(j,"op");auto t=now();if(op!="cancel"&&op!="close")checkCancel();
  if(t-created>=20*60*second&&state!=COMMITTING)invalidate("WORKER_TTL_EXPIRED");
  if(op=="open"){
   need(state==NEW,"STATE_REJECTED");need(j.at("helper_path")==env.helperPath&&j.at("helper_sha256")==env.helperSha256,"HELPER_MISMATCH");
   auto path=std::filesystem::u8path(text(j,"config_path"));requirePlainPath(path);auto installation=std::filesystem::u8path(text(j,"installation_path"));requirePlainPath(installation);need(std::filesystem::is_directory(installation),"INSTALLATION_UNAVAILABLE");
   need(j.at("association")=="operator_declared_process_owner_unverified"&&j.at("mirror_address")=="127.0.0.1"&&j.at("mirror_port").get<std::uint64_t>()>0&&j.at("mirror_port").get<std::uint64_t>()<=65535,"ASSOCIATION_UNSUPPORTED");
   need(j.at("reference_role")=="left"||j.at("reference_role")=="right"||j.at("reference_role")=="hmd","REFERENCE_ROLE_UNSUPPORTED");
   original=readConfigBytes(path);config=loadConfig(path);configPath=path.u8string();configHash=sha256(original);
   need(j.at("bridge_id")==config.bridgeId&&j.at("input_revision").get<std::uint64_t>()<=UINT32_MAX,"TARGET_OWNER_REVISION_MISMATCH");
   auto target=freezeMeasurementTarget(config,text(j,"source"),text(j,"device"),text(j,"tracker"),text(j,"input_space"),j.at("input_revision").get<std::uint32_t>());
   frozen=std::make_unique<const FrozenMeasurementTarget>(target);readonly=j.at("readonly").get<bool>();referenceRole=text(j,"reference_role");state=OPEN;
   return {{"status","OPEN"},{"config_sha256",configHash},{"capture_supported",provider.supported()},{"readonly",readonly},{"association","operator_declared / process_owner_unverified"}};
  }
  need(text(j,"session_id")==session&&text(j,"run_id")==run,"WRONG_SESSION_RUN");
  if(op=="close"||op=="cancel"){need(state!=COMMITTING,"COMMIT_OUTCOME_PENDING");if(terminalPhase(state))return {{"status",name(state)},{"closed",true}};cancel=true;state=op=="close"?CLOSED:CANCELLED;confirmation.clear();review.clear();if(createdDraft)drafts.terminal(name(state));return {{"status",name(state)}};}
  need(!terminalPhase(state),"INSPECT_ONLY_NEW_MEASUREMENT_REQUIRED");
  if(t-created>=20*60*second)invalidate("WORKER_TTL_EXPIRED");
  if(op=="capture_point")throw std::runtime_error("STATIC_CAPTURE_UNSUPPORTED_B4_REQUIRED");
  if(op=="discover"){
   need(state==OPEN,"STATE_REJECTED");need(provider.supported(),"CAPTURE_UNSUPPORTED_B3_REQUIRED");current();
   auto batch=provider.discover(cancel);need(now()-t<10*second,"DISCOVER_TIMEOUT");checkCancel();
   const auto& b=frozen->target().binding;need(config.contracts.count({b.source,b.device}),"EXPLICIT_DECLARATION_REQUIRED");
   auto witness=batch.identity;
   need(witness.source==b.source&&witness.device==b.device&&witness.space==b.inputSpace&&witness.revision==b.inputRevision&&witness.convention==frozen->target().profile.convention&&!witness.session.empty()&&!witness.clock.empty()&&!witness.peer.empty()&&!witness.referenceSerial.empty()&&!witness.standing.empty()&&witness.referenceRole==referenceRole,"DISCOVERY_WITNESS_MISMATCH");
   need(batch.frames.size()<=128,"PROVIDER_BATCH_LIMIT");
   std::int64_t floor=-1;std::optional<std::int64_t> stateFloor;
   for(const auto& f:batch.frames){if(auto p=std::get_if<c1::TrackerObservation>(&f.envelope))if(p->source_id==b.source&&p->device_id==b.device)floor=std::max(floor,p->sequence);if(auto p=std::get_if<c1::ObservationDeviceState>(&f.envelope))if(p->source_id==b.source&&p->device_id==b.device)stateFloor=std::max(stateFloor.value_or(-1),p->sequence);}
   need(floor>=0,"FRESH_POSE_REQUIRED");witness.initialPoseFloor=floor;witness.initialStateFloor=stateFloor;
   input=std::make_unique<const InputWitness>(witness);poses.high.reset();states.high.reset();consume(batch,false);live(true);
   need(poses.high&&*poses.high==floor&&states.high==stateFloor,"INITIAL_FLOOR_MISMATCH");state=DISCOVERED;
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
   provider.capture([&](const ProviderBatch& b){need(state==RECORDING,"LATE_PROVIDER_CALLBACK");checkCancel();consume(b,true);if(env.progress&&(progressAt<0||now()-progressAt>=second/4)){progressAt=now();env.progress({{"status","PROGRESS"},{"phase","RECORDING"},{"session_id",session},{"run_id",run},{"samples",recorder->sourceSamples().size()}});}},cancel);checkCancel();guards(true);
   Json raw={{"source",Json::array()},{"reference",Json::array()}};
   for(const auto& s:recorder->sourceSamples())raw["source"].push_back({{"time_ns",s.timeNs},{"valid",s.valid},{"pose",rigid(s.pose)}});
   for(const auto& r:recorder->referenceSamples())raw["reference"].push_back({{"time_ns",r.timeNs},{"valid",r.valid},{"pose",rigid(r.pose)}});
   drafts.write("raw.json",raw.dump());
   state=SOLVING;auto solveStart=now();++solves;
   auto solved=recorder->solve();checkCancel();need(now()-solveStart<10*second,"SOLVE_TIMEOUT");guards(true);
   result=std::make_unique<const TrajectoryResult>(std::move(solved));resultId=opaqueId();resultDeadline=now()+10*60*second;
   drafts.write("result.json",resultJson().dump());state=RESULT_READY;
   return {{"status","RESULT_READY"},{"result",resultJson()}};
  }
  need(state==RESULT_READY||state==PREPARED,"STATE_REJECTED");
  need(text(j,"result_id")==resultId,"WRONG_RESULT");
  try{guards(true);}catch(...){review.clear();if(state==PREPARED)state=RESULT_READY;throw;}
  try{artifactCheck();}catch(...){invalidate("DRAFT_ARTIFACT_CHANGED");}
  if(op=="prepare_commit"){
   need(!readonly,"READONLY_COMMIT_REJECTED");
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
  need(op=="commit"&&state==PREPARED&&!readonly,"COMMIT_NOT_PREPARED_OR_READONLY");
  need(!review.empty()&&text(j,"review_token")==review&&text(j,"candidate_sha256")==candidateHash,"REVIEW_TOKEN_CANDIDATE_MISMATCH");
  if(now()>=reviewDeadline){review.clear();state=RESULT_READY;throw std::runtime_error("REVIEW_TOKEN_EXPIRED_REPREPARE");}
  checkCancel();state=COMMITTING;review.clear();auto started=now();++commits;
  try{
   commitConfigDocument(std::filesystem::u8path(configPath),original,candidate,[&](const char* stage){if(env.transactionHook)env.transactionHook(stage);if(cancel)evidenceStale=true;try{consume(provider.guard(),false);}catch(...){evidenceStale=true;}});
   if(env.afterReplace)env.afterReplace();
   try{consume(provider.guard(),false);live(true);}catch(...){evidenceStale=true;}
   if(now()-started>=15*second){state=UNKNOWN;drafts.terminal("UNKNOWN");return {{"status","UNKNOWN"},{"reason","COMMIT_TIMEOUT_NO_RETRY"}};}
   if(cancel)evidenceStale=true;
   if(readConfigBytes(std::filesystem::u8path(configPath))!=candidate)evidenceStale=true;
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
void CalibrationSession::tick(){auto& x=*impl_;if(terminalPhase(x.state))return;try{x.checkCancel();if(x.now()-x.created>=20*60*second)x.invalidate("WORKER_TTL_EXPIRED");if(x.state>=DISCOVERED&&x.state<=PREPARED)x.guards(false);}catch(...){if(!terminalPhase(x.state))x.state=INSPECT_ONLY;if(x.createdDraft)try{x.drafts.terminal(name(x.state));}catch(...){}}}
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
std::pair<std::int64_t,std::optional<std::int64_t>> CalibrationSession::highwaters()const{return {impl_->poses.high.value_or(-1),impl_->states.high};}
std::size_t CalibrationSession::solveCalls()const{return impl_->solves;}
std::size_t CalibrationSession::commitCalls()const{return impl_->commits;}
} // namespace mb::local_calibration
