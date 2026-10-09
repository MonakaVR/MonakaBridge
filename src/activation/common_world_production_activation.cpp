#include "monaka_bridge/common_world_production_activation.hpp"
#include "monaka_bridge/network.hpp"
#include "../authority/common_world_facts.hpp"
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#endif

namespace monaka_bridge {
namespace w=authority_wire;
namespace d=common_world_detail;
namespace {
// Literal IPv4, audited nonblocking UDP only. Construction is inert; no DNS,
// background connect/reconnect, completion thread, peer ACK or implicit retry.
class ProductionDatagram final : public CommonWorldDatagramTransport {
 std::uint32_t ip_; std::uint16_t port_;
#ifdef _WIN32
 SOCKET socket_ = INVALID_SOCKET; bool winsock_ = false;
#endif
public:
 explicit ProductionDatagram(const CommonWorldActivationConfig& c):ip_(c.ipv4),port_(c.port) {
  if(!c.valid || !c.enabled || c.mode!=CommonWorldExecutionMode::Production || c.transport!="udp-ipv4" ||
   c.codec!="monaka-wire-2.1" || !ip_ || !port_) throw std::invalid_argument("production transport registration rejected");
 }
 ~ProductionDatagram() override { close(); }
 bool connect() override {
  close();
#ifdef _WIN32
  WSADATA data{}; if(WSAStartup(MAKEWORD(2,2),&data)) return false; winsock_=true;
  socket_=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); if(socket_==INVALID_SOCKET) { close(); return false; }
  u_long nonblocking=1; if(ioctlsocket(socket_,FIONBIO,&nonblocking)) { close(); return false; }
  // No send occurs here. An unconnected datagram socket is a local submission endpoint.
  return true;
#else
  return false; // persistence and production registration are Windows/local NTFS only
#endif
 }
 void close() noexcept override {
#ifdef _WIN32
  if(socket_!=INVALID_SOCKET) { closesocket(socket_); socket_=INVALID_SOCKET; }
  if(winsock_) { WSACleanup(); winsock_=false; }
#endif
 }
 DatagramSubmissionResult submit(std::string_view bytes) override {
#ifdef _WIN32
  if(socket_==INVALID_SOCKET || bytes.empty() || bytes.size()>4096) return {DatagramSubmission::Rejected,0};
  sockaddr_in target{}; target.sin_family=AF_INET; target.sin_addr.s_addr=htonl(ip_); target.sin_port=htons(port_);
  const auto count=sendto(socket_,bytes.data(),static_cast<int>(bytes.size()),0,reinterpret_cast<const sockaddr*>(&target),sizeof(target));
  if(count==static_cast<int>(bytes.size())) return {DatagramSubmission::Accepted,bytes.size()};
  // Match 5AF conservative bool UDP error semantics: no ambiguous retransmission.
  return {DatagramSubmission::Ambiguous,0};
#else
  (void)bytes; return {DatagramSubmission::Rejected,0};
#endif
 }
};
}
class CommonWorldProductionActivation::StagedTransport final : public CommonWorldDatagramTransport {
 std::unique_ptr<CommonWorldDatagramTransport> inner_;
 bool staged_ = false, active_ = false;
public:
 void attach(std::unique_ptr<CommonWorldDatagramTransport> transport) {
  if(!transport || inner_) throw std::invalid_argument("transport unavailable/conflicting registration");
  inner_=std::move(transport);
 }
 bool ready() const { return bool(inner_) && !active_; }
 bool connect() override { staged_=bool(inner_); return staged_; } // readiness ONLY, zero I/O
 bool activate() {
  if(!staged_ || !inner_ || active_) return false;
  active_=inner_->connect(); return active_;
 }
 void close() noexcept override { staged_=false; active_=false; if(inner_) inner_->close(); }
 DatagramSubmissionResult submit(std::string_view bytes) override {
  if(!active_ || !inner_) return {DatagramSubmission::Rejected,0};
  return inner_->submit(bytes);
 }
};
struct CommonWorldProductionActivation::Session {
 CommonWorldActivationConfig config;
 std::shared_ptr<CommonWorldRevisionStore> store;
 std::unique_ptr<CommonWorldLiveSender> sender;
 CommonWorldLiveSender::Source source;
 StagedTransport* transport = nullptr; // owned by sender, session-scoped, never exposed
 std::optional<CommonWorldDurableReceipt> receipt;
 std::string epoch;
 bool recovered = false;
 ~Session() { if(sender) sender->dispose(); }
};
const char* commonWorldActivationStatusName(CommonWorldActivationStatus s) {
 switch(s) {
 case CommonWorldActivationStatus::DisabledByDefault:return "DisabledByDefault";
 case CommonWorldActivationStatus::ConfigInvalid:return "ConfigInvalid";
 case CommonWorldActivationStatus::RecoveryPending:return "RecoveryPending";
 case CommonWorldActivationStatus::OwnerUnavailable:return "OwnerUnavailable";
 case CommonWorldActivationStatus::ReadyButInactive:return "ReadyButInactive";
 case CommonWorldActivationStatus::Activating:return "Activating";
 case CommonWorldActivationStatus::Active:return "Active";
 case CommonWorldActivationStatus::Deactivating:return "Deactivating";
 case CommonWorldActivationStatus::Faulted:return "Faulted";
 case CommonWorldActivationStatus::Shutdown:return "Shutdown";
 }
 return "Faulted";
}
CommonWorldProductionActivation::CommonWorldProductionActivation():CommonWorldProductionActivation([] {
 Dependencies deps;
 deps.storage=[](const std::filesystem::path& p) { return makeCommonWorldFileStorage(p); };
 deps.transport=[](const CommonWorldActivationConfig& c) { return std::make_unique<ProductionDatagram>(c); };
 deps.freshEpoch=[] { return mb::uuid(); };
 return deps;
}()) {}
CommonWorldProductionActivation::CommonWorldProductionActivation(Dependencies deps):dependencies_(std::move(deps)) {}
CommonWorldProductionActivation::~CommonWorldProductionActivation() { shutdown(); }
void CommonWorldProductionActivation::increment() {
 if(generation_==UINT64_MAX) { shutdown_=true; status_=CommonWorldActivationStatus::Faulted; retirePublication(); throw std::runtime_error("activation generation exhausted"); }
 ++generation_;
}
void CommonWorldProductionActivation::retirePublication() {
 if(session_ && session_->sender) {
  // DEACTIVATION LINEARIZATION: the unchanged sender clears eligibility/queue/attempt
  // under its lifecycle mutex. Activation mutex already excludes every pump/complete.
  session_->sender->disablePublisher();
  session_->sender->disconnect();
 }
}
std::string CommonWorldProductionActivation::prepare(const std::optional<std::filesystem::path>& path,const w::Envelope& bootstrap,std::int64_t at) {
 std::uint64_t ticket;
 {
  std::lock_guard<std::mutex> lock(mutex_);
  if(shutdown_) return "shutdown";
  if(preparing_) return "startup-in-progress";
  if(session_) return "already-prepared"; // no runtime reconfiguration/implicit world replacement
  increment(); ticket=generation_; preparing_=true; stages_.clear(); reason_.clear();
 }
 auto candidate=std::make_shared<Session>();
 const auto stage=[&](CommonWorldStartupStage value,CommonWorldActivationStatus status) {
  {
   std::lock_guard<std::mutex> lock(mutex_);
   if(ticket!=generation_ || shutdown_) throw std::runtime_error("startup-retired");
   stages_.push_back(value); status_=status;
  }
  if(dependencies_.beforeStage) dependencies_.beforeStage(value);
  std::lock_guard<std::mutex> lock(mutex_);
  if(ticket!=generation_ || shutdown_) throw std::runtime_error("startup-retired");
 };
 try {
  stage(CommonWorldStartupStage::ConfigLoad,CommonWorldActivationStatus::DisabledByDefault);
  candidate->config=loadCommonWorldActivationConfig(path);
  stage(CommonWorldStartupStage::Validation,CommonWorldActivationStatus::ConfigInvalid);
  if(!candidate->config.valid) throw std::invalid_argument(candidate->config.error);
  if(!candidate->config.enabled) {
   std::lock_guard<std::mutex> lock(mutex_); if(ticket!=generation_ || shutdown_) { preparing_=false; return "startup-retired"; }
   preparing_=false; status_=CommonWorldActivationStatus::DisabledByDefault; return "disabled";
  }
  if(candidate->config.mode!=dependencies_.mode) throw std::invalid_argument("test/production registration mismatch");
  stage(CommonWorldStartupStage::Recovery,CommonWorldActivationStatus::RecoveryPending);
  candidate->store=std::make_shared<CommonWorldRevisionStore>(dependencies_.storage(candidate->config.revisionPath),
   CommonWorldPersistentHighWater{candidate->config.ownerId,candidate->config.worldSpaceId,{},0});
  candidate->store->open(); candidate->recovered=true; // corruption/identity mismatch throws, never guessed recovery
  stage(CommonWorldStartupStage::OwnerInitialization,CommonWorldActivationStatus::OwnerUnavailable);
  candidate->epoch=dependencies_.freshEpoch();
  auto transport=std::make_unique<StagedTransport>(); candidate->transport=transport.get();
  // 5AF constructs the dormant owner, source receiver and ordered stamper together.
  // Preserve that composition: the endpoint is still unattached/inert at this stage.
  candidate->sender=std::make_unique<CommonWorldLiveSender>(candidate->store,candidate->epoch,candidate->config.sourcePublisher,
   std::move(transport),candidate->config.queueCapacity,candidate->config.retryLimit);
  stage(CommonWorldStartupStage::SourceReadiness,CommonWorldActivationStatus::OwnerUnavailable);
  const auto* authority=std::get_if<w::TrustedHmdSourceAuthority>(&bootstrap);
  if(!authority || authority->publisher_id!=candidate->config.sourcePublisher) throw std::runtime_error("source identity mismatch");
  candidate->source=candidate->sender->startSource(at);
  const auto authorized=candidate->sender->authorizeSource(candidate->source,authority->publisher_id,authority->session_id,authority->clock_id,bootstrap,at);
  if(authorized.status!="authority") throw std::runtime_error("source/owner unavailable: "+authorized.status);
  auto committed=candidate->sender->commitSource(candidate->source,at);
  if(!committed.eligible || !committed.receipt) throw std::runtime_error("persistent/current owner readiness failed: "+committed.status+" "+committed.error);
  candidate->receipt=committed.receipt;
  stage(CommonWorldStartupStage::SenderConstruction,CommonWorldActivationStatus::OwnerUnavailable);
  candidate->transport->attach(dependencies_.transport(candidate->config));
  stage(CommonWorldStartupStage::Gate,CommonWorldActivationStatus::OwnerUnavailable);
  const auto snap=candidate->sender->snapshot();
  if(!snap.ingress.runtime.world) throw std::runtime_error("owner unavailable");
  const auto denied=gate(*candidate,snap.ingress.runtime.world->world);
  if(!denied.empty()) throw std::runtime_error(denied);
  std::lock_guard<std::mutex> lock(mutex_);
  if(ticket!=generation_ || shutdown_) { preparing_=false; return "startup-retired"; }
  session_=std::move(candidate); preparing_=false; status_=CommonWorldActivationStatus::ReadyButInactive; return "ready-inactive";
 } catch(const std::exception& e) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(ticket!=generation_ || shutdown_) { preparing_=false; return "startup-retired"; }
  preparing_=false; reason_=e.what();
  status_=stages_.size()<=2?CommonWorldActivationStatus::ConfigInvalid:CommonWorldActivationStatus::Faulted;
  return "startup-failed";
 }
}
std::string CommonWorldProductionActivation::gate(const Session& s,const w::CommonWorldReference& expected) const {
 if(!s.config.valid || !s.config.enabled || s.config.mode!=dependencies_.mode) return "config-invalid";
 if(!s.recovered || !s.store) return "recovery-pending";
 if(!s.sender) return "publisher-unavailable";
 const auto snap=s.sender->snapshot(); const auto& live=snap.ingress;
 if(snap.disposed || live.disposed || !live.ownerApproved || !live.sourceActive || live.persistenceBusy ||
  live.runtime.disposed || live.runtime.pending || !live.runtime.world || !live.runtime.mapping) return "owner-unavailable";
 const auto& world=live.runtime.world->world;
 if(world.owner_id!=s.config.ownerId || world.coordinate_space.id!=s.config.worldSpaceId ||
  d::world(world)!=d::world(expected) || d::world(live.runtime.mapping->mapping.world)!=d::world(world)) return "world-identity-mismatch";
 if(!s.receipt || s.receipt->committed.authorizationEpoch!=s.epoch || !s.store->matchesCurrentCommit(*s.receipt)) return "stale-epoch-or-receipt";
 if(!s.transport || !s.transport->ready()) return "transport-unavailable-or-stale-session";
 if(snap.enabled || snap.queued || snap.awaitingCompletion) return "stale-publisher-session";
 return {};
}
std::string CommonWorldProductionActivation::activate(const w::CommonWorldReference& expected,std::int64_t at) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(shutdown_) return "shutdown";
 if(!session_) return preparing_?"startup-in-progress":"not-ready";
 try {
 if(status_==CommonWorldActivationStatus::Active) {
  const auto snap=session_->sender->snapshot();
  if(!snap.ingress.runtime.world || d::world(snap.ingress.runtime.world->world)!=d::world(expected)) return "world-identity-mismatch";
  if(snap.enabled && snap.connected && snap.ingress.ownerApproved && session_->store->matchesCurrentCommit(*session_->receipt)) return "already-active";
  retirePublication(); status_=CommonWorldActivationStatus::Faulted; reason_="activation-authority-retired"; return reason_;
 }
 if(status_!=CommonWorldActivationStatus::ReadyButInactive) return "not-ready";
 status_=CommonWorldActivationStatus::Activating;
  const auto denied=gate(*session_,expected);
  if(!denied.empty()) { status_=CommonWorldActivationStatus::ReadyButInactive; reason_=denied; return denied; }
  stages_.push_back(CommonWorldStartupStage::ExplicitEnable);
  if(dependencies_.beforeStage) dependencies_.beforeStage(CommonWorldStartupStage::ExplicitEnable);
  // Staging reports sender connection readiness; it does not touch the real endpoint.
  if(!session_->sender->connect()) throw std::runtime_error("sender readiness failed");
  const auto enabled=session_->sender->enablePublisher(*session_->receipt,mb::uuid(),mb::uuid(),at);
  if(enabled!="queued") throw std::runtime_error("publisher enable rejected: "+enabled);
  stages_.push_back(CommonWorldStartupStage::TransportActivation);
  if(dependencies_.beforeStage) dependencies_.beforeStage(CommonWorldStartupStage::TransportActivation);
  bool connected=false;
  // External epoch handover is excluded by the exact Store gate. Local owner revoke,
  // deactivate, shutdown and source mutation are excluded by this activation mutex.
  const bool current=session_->store->acceptCurrentCommit(*session_->receipt,[&] {
   connected=session_->transport->activate();
   if(connected) status_=CommonWorldActivationStatus::Active; // activation linearization under CURRENT store gate
  });
  if(!current || !connected) throw std::runtime_error(current?"transport activation failed":"stale epoch before transport activation");
  // ACTIVATION LINEARIZATION: all gates + explicit enable + endpoint activation have
  // succeeded, while both authorization and orchestration boundaries are serialized.
  // Every pump revalidates current authority at the unchanged 5AF send entry point.
  reason_.clear(); return "active";
 } catch(const std::exception& e) {
  retirePublication(); status_=CommonWorldActivationStatus::Faulted; reason_=e.what(); return "activation-failed";
 }
}
void CommonWorldProductionActivation::deactivate() {
 std::lock_guard<std::mutex> lock(mutex_); if(shutdown_) return;
 if(!preparing_ && (status_==CommonWorldActivationStatus::ReadyButInactive || status_==CommonWorldActivationStatus::DisabledByDefault)) return;
 status_=CommonWorldActivationStatus::Deactivating; increment(); // invalidates delayed startup/completion
 retirePublication();
 status_=session_?CommonWorldActivationStatus::ReadyButInactive:CommonWorldActivationStatus::DisabledByDefault;
 reason_="explicitly-deactivated";
}
void CommonWorldProductionActivation::shutdown(std::int64_t at) {
 std::lock_guard<std::mutex> lock(mutex_); if(shutdown_) return;
 status_=CommonWorldActivationStatus::Deactivating; increment(); retirePublication(); // revoke first
 shutdown_=true;
 if(session_) { session_->sender->stopSource(session_->source,at); session_->sender->revokeOwner(at); session_->sender->dispose(); }
 status_=CommonWorldActivationStatus::Shutdown; reason_="shutdown";
}
void CommonWorldProductionActivation::revokeOwner(std::int64_t at) {
 std::lock_guard<std::mutex> lock(mutex_); if(shutdown_) return;
 increment(); retirePublication();
 if(session_) session_->sender->revokeOwner(at);
 status_=CommonWorldActivationStatus::OwnerUnavailable; reason_="owner-revoked";
}
void CommonWorldProductionActivation::observeSenderFault() {
 if(status_!=CommonWorldActivationStatus::Active || !session_) return;
 const auto s=session_->sender->snapshot();
 if(!s.enabled || !s.connected || !s.ingress.ownerApproved || !s.ingress.sourceActive) {
  increment(); retirePublication(); status_=CommonWorldActivationStatus::Faulted; reason_="sender-or-authority-retired";
 }
}
CommonWorldSenderSourceResult CommonWorldProductionActivation::receive(const w::Envelope& event,std::int64_t at) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(shutdown_ || !session_) return {{shutdown_?"shutdown":"not-ready",{}},"publisher-disabled"};
 const auto before=session_->sender->snapshot();
 auto result=session_->source.receive(event,at);
 const auto after=session_->sender->snapshot();
 // Do not implicitly commit a source transition into a new world. New preparation
 // requires a fresh runtime and explicit matching durable-world activation.
 if(before.ingress.runtime.world && (!after.ingress.runtime.world || after.ingress.runtime.pending)) {
  increment(); retirePublication(); status_=CommonWorldActivationStatus::OwnerUnavailable; reason_="source-world-transition-requires-explicit-startup";
 }
 observeSenderFault(); return result;
}
CommonWorldProductionActivation::Submission CommonWorldProductionActivation::pump(const std::atomic_bool* cancel) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(shutdown_ || status_!=CommonWorldActivationStatus::Active || !session_) return {"activation-inactive",{}};
 auto result=session_->sender->pump(cancel); Completion completion;
 if(result.completion) { completion.session_=session_; completion.generation_=generation_; completion.attempt_=result.completion; }
 observeSenderFault(); return {std::move(result.status),std::move(completion)};
}
std::string CommonWorldProductionActivation::complete(const Completion& c,DatagramSubmissionResult result) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(shutdown_ || status_!=CommonWorldActivationStatus::Active || !session_ || c.session_.lock()!=session_ || c.generation_!=generation_) return "stale-completion";
 auto status=session_->sender->complete(c.attempt_,result); observeSenderFault(); return status;
}
CommonWorldActivationSnapshot CommonWorldProductionActivation::snapshot() const {
 std::lock_guard<std::mutex> lock(mutex_);
 return {status_.load(),generation_,session_ && session_->recovered,shutdown_,reason_,stages_,
  session_?std::optional<CommonWorldSenderSnapshot>(session_->sender->snapshot()):std::nullopt};
}
}
