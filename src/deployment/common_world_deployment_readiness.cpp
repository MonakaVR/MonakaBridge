#include "monaka_bridge/common_world_deployment_readiness.hpp"
#include "monaka_bridge/network.hpp"
#include "common_world_build_identity.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#endif

namespace monaka_bridge {
namespace fs=std::filesystem; namespace w=authority_wire; using J=nlohmann::json;
namespace {
void require(bool ok,const std::string& message) { if(!ok) throw std::runtime_error(message); }
std::string read(const fs::path& path,std::size_t max=65536) {
 std::ifstream f(path,std::ios::binary); require(bool(f),"file unavailable");
 std::string out; char b[4096];
 while(f.read(b,sizeof(b)) || f.gcount()) { out.append(b,static_cast<std::size_t>(f.gcount())); require(out.size()<=max,"file oversized"); }
 require(!f.bad(),"file read failure"); return out;
}
J strictJson(const std::string& bytes) {
 std::vector<std::set<std::string>> keys;
 return J::parse(bytes,[&](int depth,J::parse_event_t e,J& v) {
  require(depth<=12,"JSON nesting too deep");
  if(e==J::parse_event_t::object_start) keys.emplace_back();
  if(e==J::parse_event_t::key) require(keys.back().insert(v.get<std::string>()).second,"duplicate JSON field");
  if(e==J::parse_event_t::object_end) keys.pop_back();
  return true;
 });
}
std::string sha(const std::string& bytes) {
#ifdef _WIN32
 BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
 require(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider unavailable");
 unsigned char out[32]{};
 const auto created=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0);
 const auto fed=created>=0?BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0):created;
 const auto done=fed>=0?BCryptFinishHash(hash,out,32,0):fed;
 if(hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0);
 require(done>=0,"SHA256 failed"); const char* hex="0123456789abcdef"; std::string s;
 for(auto b:out) { s+=hex[b>>4]; s+=hex[b&15]; } return s;
#else
 throw std::runtime_error("Windows x64 / local NTFS required");
#endif
}
bool within(const fs::path& child,const fs::path& root) {
 auto c=child.begin(); for(auto r=root.begin();r!=root.end();++r,++c) {
  if(c==child.end()) return false;
#ifdef _WIN32
  if(_wcsicmp(c->c_str(),r->c_str())!=0) return false;
#else
  if(*c!=*r) return false;
#endif
 } return true;
}
fs::path localPath(const fs::path& path) {
 require(!path.empty(),"explicit path required"); auto p=fs::absolute(path).lexically_normal();
 for(auto a=p;!a.empty();a=a.parent_path()) {
  require(!fs::is_symlink(fs::symlink_status(a)),"symlink forbidden");
#ifdef _WIN32
  const auto attrs=GetFileAttributesW(a.c_str());
  require(attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_REPARSE_POINT),"reparse path forbidden");
#endif
  if(a==a.parent_path()) break;
 }
 return p;
}
void writable(const fs::path& dir) {
 require(fs::is_directory(dir),"writable directory must already exist");
#ifdef _WIN32
 // Exclusive CREATE_NEW probe, immediately delete on close. Never overwrites a file.
 const auto p=dir/(".preflight-"+mb::uuid());
 HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
 require(h!=INVALID_HANDLE_VALUE,"directory write probe failed"); require(CloseHandle(h)!=0,"probe close failed");
#else
 throw std::runtime_error("Windows write probe unavailable");
#endif
}
// Only a private copy can acquire an epoch/commit. No receipt escapes the observer.
class VolatileStorage final:public CommonWorldRevisionStorage {
 std::optional<std::string> bytes_;
public:
 explicit VolatileStorage(std::optional<std::string> b):bytes_(std::move(b)) {}
 std::optional<std::string> readCommitted() override { return bytes_; }
 void atomicWrite(const std::string& b,const std::atomic_bool*) override { bytes_=b; }
};
class OfflineConstructionTransport final:public CommonWorldDatagramTransport {
public:
 bool connect() override { throw std::runtime_error("preflight transport cannot connect"); }
 void close() noexcept override {}
 DatagramSubmissionResult submit(std::string_view) override { throw std::runtime_error("preflight transport cannot submit"); }
};
J worldJson(const w::CommonWorldReference& world) {
 return {{"owner_id",world.owner_id},{"world_epoch",world.world_epoch},{"space_id",world.coordinate_space.id},
 {"convention",world.coordinate_space.convention},{"revision",world.coordinate_space.revision}};
}
J senderJson(const CommonWorldSenderSnapshot& s) {
 J out={{"connected",s.connected},{"enabled",s.enabled},{"disposed",s.disposed},{"awaiting_completion",s.awaitingCompletion},
 {"publisher_generation",s.publisherGeneration},{"transport_generation",s.transportGeneration},{"queue_depth",s.queued},
 {"queue_capacity",s.capacity},{"head_attempts",s.headAttempts},{"source_generation",s.ingress.sourceGeneration},
 {"source_active",s.ingress.sourceActive},{"owner_approved",s.ingress.ownerApproved},{"persistence_busy",s.ingress.persistenceBusy},
 {"runtime_disposed",s.ingress.runtime.disposed},{"pending_transition",bool(s.ingress.runtime.pending)}};
 out["owner_epoch_observed_from_world_token"]=nullptr;
 if(s.ingress.runtime.world) out["world"]=worldJson(s.ingress.runtime.world->world);
 if(s.ingress.runtime.world) {
  const auto& publication=*s.ingress.runtime.world;
  // The unchanged 5AD token factory is epoch + ".world." + nonwrapping counter.
  // This observed prefix is diagnostic only; the exact Store gate grants authority.
  const auto marker=publication.world.world_epoch.rfind(".world.");
  out["owner_epoch_observed_from_world_token"]=marker==std::string::npos?J(nullptr):J(publication.world.world_epoch.substr(0,marker));
  out["source_authority"]={{"source_id",publication.anchor_source.source_id},
   {"session_epoch",publication.anchor_source.source_authority_session_epoch},{"space_id",publication.anchor_source.source_space_id},
   {"generation",publication.anchor_source.source_space_generation}};
 }
 const auto& high=s.ingress.runtime.highWater;
 const auto& runtime=s.ingress.runtime;
 out["owner_epoch"]=runtime.ownerEpoch;
 out["source_publisher"]=runtime.sourcePublisher; out["source_session"]=runtime.sourceSession; out["source_clock"]=runtime.sourceClock;
 out["source_type"]="trusted-wire-2.1-local-stream";
 out["source_sequence_high_water"]=runtime.sourceSequence?J(*runtime.sourceSequence):J(nullptr);
 out["source_lifecycle"]=s.ingress.disposed?"Disposed":s.ingress.sourceActive?"Connected":"Disconnected";
 out["owner_lifecycle"]=s.ingress.ownerApproved?"Authorized":runtime.pending?"Pending":"Unavailable";
 out["publisher_state"]=s.enabled?"Enabled":"Disabled"; out["queue_high_water"]=s.queueHighWater;
 out["last_local_accepted_sequence"]=s.lastLocalAcceptedSequence?J(*s.lastLocalAcceptedSequence):J(nullptr);
 out["sender_lifecycle"]=s.disposed?"Disposed":s.enabled?"Publishing":"Disabled";
 out["transport_lifecycle"]=s.connected?"Connected":"Disconnected";
 out["last_source_observation"]=nullptr;
 if(runtime.sourceObservation) {
  std::string bytes; w::Error error;
  if(w::EncodeEnvelope(runtime.sourceObservation->observation,bytes,error))
   out["last_source_observation"]={{"envelope",J::parse(bytes)},{"received_at_monotonic_ns",runtime.sourceObservation->received_at_ns}};
 }
 out["current_source_authority"]=nullptr;
 if(runtime.sourceAuthority) { std::string bytes; w::Error error;
  if(w::EncodeEnvelope(*runtime.sourceAuthority,bytes,error)) out["current_source_authority"]=J::parse(bytes);
 }
 out["local_submission_record"]=nullptr;
 if(s.lastSubmission) { const auto& r=*s.lastSubmission;
  out["local_submission_record"]={{"owner_epoch",r.ownerEpoch},{"owner_id",r.ownerId},{"world_epoch",r.worldEpoch},
   {"world_id",r.worldId},{"world_revision",r.worldRevision},{"mapping_revision",r.mappingRevision},
   {"source_publisher",r.sourcePublisher},{"source_id",r.sourceId},{"transport_generation",r.transportGeneration},
   {"publisher_generation",r.publisherGeneration},{"ordering_position",r.orderingPosition},{"sequence",r.sequence},
   {"monotonic_ns",r.monotonicNs},{"result",r.result},{"boundary","entry/result of nonblocking local datagram API; no peer receipt"}};
 }
 out["persistent_high_water"]={{"world_revision",high.lastWorldRevision?J(*high.lastWorldRevision):J(nullptr)},{"mapping_revision",high.lastMappingRevision}};
 return out;
}
}
std::string commonWorldBuildCommit() { return COMMON_WORLD_BUILD_COMMIT; }
std::string commonWorldFileSha256(const fs::path& p) { return sha(read(p,64*1024*1024)); }
const char* commonWorldReadinessStateName(CommonWorldReadinessState s) {
 switch(s) {
 case CommonWorldReadinessState::NotConfigured:return "NotConfigured";
 case CommonWorldReadinessState::InvalidConfiguration:return "InvalidConfiguration";
 case CommonWorldReadinessState::ArtifactInvalid:return "ArtifactInvalid";
 case CommonWorldReadinessState::DependencyMissing:return "DependencyMissing";
 case CommonWorldReadinessState::PersistentStateNotReady:return "PersistentStateNotReady";
 case CommonWorldReadinessState::RuntimeNotReady:return "RuntimeNotReady";
 case CommonWorldReadinessState::TransportConfigurationInvalid:return "TransportConfigurationInvalid";
 case CommonWorldReadinessState::ReadyForControlledActivation:return "ReadyForControlledActivation";
 } return "RuntimeNotReady";
}
CommonWorldDeploymentReadiness::CommonWorldDeploymentReadiness():CommonWorldDeploymentReadiness([] {
 Dependencies d; d.storage=[](const fs::path& p) { return makeCommonWorldFileStorage(p); }; return d;
}()) {}
CommonWorldDeploymentReadiness::CommonWorldDeploymentReadiness(Dependencies d):dependencies_(std::move(d)) {}
CommonWorldReadinessResult CommonWorldDeploymentReadiness::snapshot() const { std::lock_guard<std::mutex> l(mutex_); return result_; }
void CommonWorldDeploymentReadiness::retire(const std::string& reason,bool terminal) {
 std::lock_guard<std::mutex> l(mutex_);
 terminal_=terminal_ || terminal;
 if(generation_==UINT64_MAX) terminal_=true; else ++generation_;
 result_={CommonWorldReadinessState::RuntimeNotReady,generation_,mb::monotonicNs(),reason,{}};
 result_.json=J{{"state","RuntimeNotReady"},{"reason",reason},{"activation_enabled",false},{"authorization",false}}.dump(2);
}
void CommonWorldDeploymentReadiness::invalidate(const std::string& reason) { retire(reason,false); }
void CommonWorldDeploymentReadiness::shutdown() { retire("shutdown",true); }
void CommonWorldDeploymentReadiness::emergencyRevoke() { retire("emergency-revoke",true); }
CommonWorldReadinessResult CommonWorldDeploymentReadiness::preflight(const CommonWorldPreflightRequest& req) {
 std::uint64_t ticket;
 {
  std::lock_guard<std::mutex> l(mutex_);
  if(terminal_ || running_) { auto r=result_; r.state=CommonWorldReadinessState::RuntimeNotReady; r.reason=terminal_?"observer-retired":"preflight-in-progress";
   r.json=J{{"state","RuntimeNotReady"},{"reason",r.reason},{"activation_enabled",false},{"authorization",false}}.dump(2); return r; }
  if(generation_==UINT64_MAX) { terminal_=true; return result_; }
  ticket=++generation_; running_=true;
  result_={CommonWorldReadinessState::RuntimeNotReady,ticket,mb::monotonicNs(),"preflight-in-progress",{}};
 }
 CommonWorldReadinessResult r; r.generation=ticket; J diag={{"authorization",false},{"activation_enabled",false},
 {"production_transport_connected",false},{"persistent_writes",0},{"delivery_semantics","ordered best-effort local submission"}};
 auto stage=[&](const std::string& name,CommonWorldReadinessState failure) {
  r.state=failure; diag["last_check"]=name;
  if(dependencies_.before) dependencies_.before(name);
  std::lock_guard<std::mutex> l(mutex_); require(ticket==generation_ && !terminal_,"preflight-retired");
 };
 try {
  stage("configuration",CommonWorldReadinessState::NotConfigured);
  require(fs::is_regular_file(req.config),"configuration missing"); const auto configBytes=read(req.config);
  r.state=CommonWorldReadinessState::InvalidConfiguration; auto j=strictJson(configBytes); require(j.is_object(),"configuration object required");
  require(j.contains("enabled") && j["enabled"].is_boolean() && !j["enabled"].get<bool>(),"preflight requires explicit activation OFF");
  // 5AG intentionally short-circuits OFF configs. Validate the complete future
  // contract in a value copy without changing the file or enabling any runtime.
  j["enabled"]=true; auto config=parseCommonWorldActivationConfig(j.dump(),req.config);
  if(!config.valid) {
   r.state=(config.error.find("endpoint")!=std::string::npos || config.error.find("transport")!=std::string::npos || config.error.find("IPv4")!=std::string::npos)?
    CommonWorldReadinessState::TransportConfigurationInvalid:CommonWorldReadinessState::InvalidConfiguration;
   throw std::runtime_error(config.error);
  }
  r.state=CommonWorldReadinessState::TransportConfigurationInvalid;
  require(config.mode==CommonWorldExecutionMode::Production,"preflight production registration rejects test mode"); config.enabled=false;
  diag["mode"]="production"; diag["transport_class"]="udp-ipv4"; diag["codec"]=config.codec;
  diag["config_fingerprint_sha256"]=sha(configBytes); diag["endpoint"]=config.endpoint;
  stage("artifact",CommonWorldReadinessState::ArtifactInvalid);
  const auto artifact=localPath(req.artifact); const auto manifestPath=artifact/"artifact-manifest.json"; const auto manifestBytes=read(manifestPath,4*1024*1024);
  const auto m=strictJson(manifestBytes);
  require(m.at("schema")=="monaka-common-world-artifact-1" && m.at("activation_default")==false && m.at("architecture")=="x64" && m.at("runtime")=="windows-local-ntfs","manifest contract mismatch");
  require(m.at("commit")==req.expectedCommit && req.expectedCommit==commonWorldBuildCommit(),"artifact/build/expected commit mismatch");
  require(m.at("build_input_sha256")==COMMON_WORLD_BUILD_INPUT_SHA256,"artifact/build input mismatch");
  require(m.at("build_configuration")=="Release","Release artifact required");
  std::set<std::string> listed;
  for(const auto& row:m.at("files")) {
   const auto name=row.at("path").get<std::string>(); const fs::path rel(name);
   require(!rel.empty() && !rel.is_absolute() && name.find('\\')==std::string::npos && name.find(':')==std::string::npos,"invalid manifest path");
   const auto p=localPath(artifact/rel); require(within(p,artifact) && p!=manifestPath && listed.insert(name).second,"manifest traversal/duplicate");
   if(!fs::is_regular_file(p)) { r.state=CommonWorldReadinessState::DependencyMissing; throw std::runtime_error("artifact dependency missing: "+name); }
   require(fs::file_size(p)==row.at("size").get<std::uintmax_t>() && commonWorldFileSha256(p)==row.at("sha256"),"artifact hash mismatch: "+name);
  }
  for(const auto& name:{"bin/monaka_common_world_runtime.exe","bin/monaka_common_world_preflight.exe","templates/production.activation.json","dependency-inventory.json"}) require(listed.count(name),"required artifact entry missing");
  for(const auto& entry:fs::recursive_directory_iterator(artifact)) if(entry.is_regular_file()) {
   const auto name=entry.path().lexically_relative(artifact).generic_string();
   require(name=="artifact-manifest.json" || name=="artifact-manifest.sha256" || listed.count(name),"unmanifested artifact file");
  }
  require(read(artifact/"artifact-manifest.sha256")==sha(manifestBytes)+"\n","manifest digest mismatch");
  diag["commit"]=req.expectedCommit; diag["build_input_sha256"]=COMMON_WORLD_BUILD_INPUT_SHA256; diag["manifest_sha256"]=sha(manifestBytes);
  stage("dependencies",CommonWorldReadinessState::DependencyMissing);
  const auto dep=strictJson(read(artifact/"dependency-inventory.json"));
  require(dep.at("schema")=="monaka-common-world-dependencies-1" && dep.at("codec")=="monaka-wire-2.1","dependency contract mismatch");
#ifdef _WIN32
  for(const auto& dll:dep.at("system_dlls")) {
   const auto name=dll.get<std::string>(); require(name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.")==std::string::npos,"invalid dependency name");
   HMODULE h=LoadLibraryExA(name.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32); require(h!=nullptr,"system dependency unavailable: "+name); FreeLibrary(h);
  }
#else
  throw std::runtime_error("Windows runtime required");
#endif
  stage("paths",CommonWorldReadinessState::RuntimeNotReady);
  const auto state=localPath(config.revisionPath),logs=localPath(req.logs),reports=localPath(req.reports),cfg=localPath(req.config),source=localPath(req.source);
  for(const auto& p:{state,logs,reports,cfg,source}) require(!within(p,artifact),"mutable/operator inputs must be outside immutable artifact");
  require(state!=cfg && state!=source && !within(state,logs) && !within(state,reports),"persistent/log/report paths overlap");
  writable(state.parent_path()); writable(logs); writable(reports);
  stage("persistent-recovery",CommonWorldReadinessState::PersistentStateNotReady);
  auto storage=dependencies_.storage(state); require(bool(storage),"persistent storage unavailable"); const auto recoveredBytes=storage->readCommitted();
  auto dryStore=std::make_shared<CommonWorldRevisionStore>(std::make_unique<VolatileStorage>(recoveredBytes),CommonWorldPersistentHighWater{config.ownerId,config.worldSpaceId,{},0});
  const auto recovered=dryStore->open();
  require(req.expectedOwnerEpoch.empty() || recovered.currentAuthorizationEpoch==req.expectedOwnerEpoch,"stale expected owner epoch");
  diag["persistent_store"]={{"path",state.string()},{"recovered",true},{"existing_record",bool(recoveredBytes)},
   {"current_owner_epoch",recovered.currentAuthorizationEpoch},{"latest_durable_world_revision",recovered.highWater.lastWorldRevision?J(*recovered.highWater.lastWorldRevision):J(nullptr)},
   {"latest_durable_mapping_revision",recovered.highWater.lastMappingRevision}};
  stage("source-codec",CommonWorldReadinessState::RuntimeNotReady);
  const auto sourceBytes=read(source,4*1024*1024); const auto first=sourceBytes.substr(0,sourceBytes.find('\n')); w::Envelope bootstrap; w::Error err;
  require(w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(first.data()),first.size(),bootstrap,err),"source codec/bootstrap unavailable");
  const auto* a=std::get_if<w::TrustedHmdSourceAuthority>(&bootstrap); require(a && a->publisher_id==config.sourcePublisher,"source identity/readiness mismatch");
  std::string encoded; require(w::EncodeEnvelope(bootstrap,encoded,err),"production codec unavailable");
  diag["source"]={{"publisher_id",a->publisher_id},{"session_id",a->session_id},{"clock_id",a->clock_id},
   {"source_space_id",a->source_space.source_space_id},{"source_space_generation",a->source_space.source_space_generation},
   {"source_authority_epoch",a->source_space.source_authority_session_epoch}};
  stage("owner-publisher-sender-construction",CommonWorldReadinessState::RuntimeNotReady);
  const auto prospectiveEpoch=mb::uuid();
  CommonWorldLiveSender dry(dryStore,prospectiveEpoch,config.sourcePublisher,std::make_unique<OfflineConstructionTransport>(),config.queueCapacity,config.retryLimit);
  const auto at=mb::monotonicNs(); const auto callback=dry.startSource(at);
  require(dry.authorizeSource(callback,a->publisher_id,a->session_id,a->clock_id,bootstrap,at).status=="authority","owner/source unavailable");
  const auto committed=dry.commitSource(callback,at); require(committed.eligible && committed.receipt,"volatile owner/publisher construction failed");
  const auto s=dry.snapshot(); require(s.ingress.ownerApproved && s.ingress.runtime.world && !s.connected && !s.enabled && !s.queued,"dry runtime unexpectedly eligible");
  diag["construction_only"]=senderJson(s); diag["prospective_owner_epoch_not_authorization"]=prospectiveEpoch;
  stage("final-input-revalidation",CommonWorldReadinessState::RuntimeNotReady);
  require(read(req.config)==configBytes && read(source,4*1024*1024)==sourceBytes && read(manifestPath,4*1024*1024)==manifestBytes,"inputs changed during preflight");
  require(storage->readCommitted()==recoveredBytes,"persistent state changed during preflight");
  r.state=CommonWorldReadinessState::ReadyForControlledActivation; r.reason="software prerequisites passed; fresh 5AG preparation and explicit activation still required";
 } catch(const std::exception& e) {
  r.reason=dynamic_cast<const nlohmann::json::exception*>(&e)?"JSON syntax/schema/type validation failed":e.what();
 }
 r.observedAtNs=mb::monotonicNs();
 std::lock_guard<std::mutex> l(mutex_); running_=false;
 if(ticket!=generation_ || terminal_) return result_;
 diag["state"]=commonWorldReadinessStateName(r.state); diag["reason"]=r.reason; diag["generation"]=ticket; diag["observed_at_monotonic_ns"]=r.observedAtNs;
 r.json=diag.dump(2); result_=r; return r;
}
std::string commonWorldRuntimeDiagnostics(const CommonWorldActivationSnapshot& s,const CommonWorldActivationConfig& c,const std::string& commit) {
 J j={{"commit",commit},{"observed_at_monotonic_ns",s.observedAtNs},{"activation_enabled",c.enabled},
 {"gate_state",commonWorldActivationStatusName(s.status)},{"activation_generation",s.generation},{"shutdown",s.shutdown},
 {"recovery_completed",s.recoveryCompleted},{"last_reason",s.reason},{"mode",c.mode==CommonWorldExecutionMode::Production?"production":"test"},
 {"transport_class",c.transport},{"codec",c.codec},{"delivery_semantics","ordered best-effort local submission"}};
 j["schema"]="monaka-common-world-runtime-snapshot-2"; j["phase"]="2B-5AH-A2";
 j["observed_at_unix_ms"]=s.observedAtUnixMs;
 j["runtime_instance"]=s.runtimeInstance; j["control_session"]=s.controlSession; j["owner_epoch"]=s.ownerEpoch;
 j["source_lease"]=s.sourceLease; j["readiness"]={{"valid",s.readinessValid},{"generation",s.readinessGeneration},{"token",s.readinessToken}};
 j["emergency_revoked"]=s.emergencyRevoked; j["owner_revoked"]=s.ownerRevoked;
 j["last_control_operation"]=s.lastOperation; j["last_control_result"]=s.lastControlResult;
 j["production_publication_enabled"]=s.status==CommonWorldActivationStatus::Active && s.sender && s.sender->enabled;
 j["last_activation_transition"]={{"state",commonWorldActivationStatusName(s.status)},{"monotonic_ns",s.lastTransitionNs},{"reason",s.reason}};
 j["last_accepted_source_event"]={{"result",s.lastAcceptedSourceResult},{"monotonic_ns",s.lastAcceptedSourceNs},{"sequence",s.lastAcceptedSourceSequence?J(*s.lastAcceptedSourceSequence):J(nullptr)}};
 j["last_rejected_source_event"]={{"result",s.lastRejectedSourceResult},{"monotonic_ns",s.lastRejectedSourceNs},{"sequence",s.lastRejectedSourceSequence?J(*s.lastRejectedSourceSequence):J(nullptr)}};
 j["last_source_result"]=s.lastSourceResult; j["last_reconnect_result"]=s.lastReconnectResult;
 j["endpoint_fingerprint_sha256"]=sha(c.endpoint); j["store_path_fingerprint_sha256"]=sha(c.revisionPath.string());
 j["store"]={{"status",s.storeError.empty()?(s.durableState?"Observed":"NotOpened"):(s.storeError=="store-busy"?"Busy":"Unavailable")},{"recovery_completed",s.recoveryCompleted},
  {"current_authorization_epoch",nullptr},{"receipt_matches_current_runtime",s.durableReceiptCurrent},{"latest_durable_revision",nullptr},{"receipt_identity",nullptr}};
 if(s.durableState) {
  const auto& store=*s.durableState;
  j["store"]["current_authorization_epoch"]=store.currentAuthorizationEpoch;
  if(store.latest) {
   const auto& receipt=*store.latest;
   j["store"]["latest_durable_revision"]={{"world",receipt.worldRevision},{"mapping",receipt.mappingRevision}};
   j["store"]["receipt_identity"]={{"authorization_epoch",receipt.authorizationEpoch},{"operation",static_cast<int>(receipt.operation)},
    {"world_revision",receipt.worldRevision},{"mapping_revision",receipt.mappingRevision}};
  }
 }
 if(s.sender) {
  j["sender"]=senderJson(*s.sender);
  j["sender"]["current_eligible_revision"]=s.status==CommonWorldActivationStatus::Active && s.durableReceiptCurrent &&
   s.sender->enabled && s.sender->ingress.ownerApproved && s.sender->ingress.sourceActive?j["store"]["latest_durable_revision"]:J(nullptr);
 } else j["sender"]=nullptr;
 return j.dump(2);
}
void emergencyRevokeCommonWorld(CommonWorldProductionActivation& r,std::int64_t at) { r.revokeOwner(at); r.emergencyRevoke(at); r.shutdown(at); }
}
