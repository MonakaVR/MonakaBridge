#include "monaka_bridge/persistent_common_world_runtime.hpp"
#include "common_world_facts.hpp"
#include <algorithm>
#include <stdexcept>

namespace monaka_bridge {
namespace d = common_world_detail;
namespace w = authority_wire;
namespace {
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
CommonWorldAuthorityOwner::TokenFactory tokenFactory(const std::string& epoch,const std::string& kind) {
 auto count=std::make_shared<std::uint64_t>(0);
 return [epoch,kind,count] { check(*count<UINT64_MAX,"token counter exhausted"); return epoch+"."+kind+"."+std::to_string(++*count); };
}
}
PersistentCommonWorldRuntime::PersistentCommonWorldRuntime(std::shared_ptr<CommonWorldRevisionStore> store,
 std::string epoch,std::string source,std::size_t capacity):store_(std::move(store)),epoch_(std::move(epoch)) {
 check(bool(store_),"store required");
 // Reserve the fresh lifetime BEFORE constructing owner; restore never creates active authority.
 const auto s=store_->acquireAuthorization(epoch_);
 coordinator_=std::make_unique<TrustedHmdCommonWorldCoordinator>(std::move(source),s.highWater,
  tokenFactory(epoch_,"world"),tokenFactory(epoch_,"calibration"),capacity);
}
void PersistentCommonWorldRuntime::ready() const { check(!disposed_,"runtime disposed"); }
void PersistentCommonWorldRuntime::invalidate() {
 if(generation_==UINT64_MAX) { coordinator_->onSourceTransportDisconnected(0); disposed_=true; throw std::runtime_error("runtime authorization generation exhausted"); }
 ++generation_;
}
bool PersistentCommonWorldRuntime::guardedGate(const std::function<void()>& callback,std::int64_t at,const CommonWorldDurableReceipt* receipt) {
 try { return receipt?store_->acceptCurrentCommit(*receipt,callback):store_->tryAuthorizationGate(epoch_,callback); }
 catch(...) {
  // A failed committed-state read cannot leave cached owner authorization usable.
  coordinator_->onSourceTransportDisconnected(at); invalidate(); throw;
 }
}
void PersistentCommonWorldRuntime::changedRequest(const std::optional<CommonWorldTransitionRequest>& before) {
 const auto& after=coordinator_->owner().pendingTransition();
 if(bool(before)!=bool(after) || (before && after && !d::exactRequest(*before,*after))) invalidate();
}
CommonWorldRuntimeSnapshot PersistentCommonWorldRuntime::snapshot() const {
 std::lock_guard<std::mutex> guard(mutex_); const auto& o=coordinator_->owner();
 return {o.highWater(),o.pendingTransition(),o.currentWorld(),o.currentMapping(),coordinator_->publisher().sessionActive(),disposed_};
}
CommonWorldOperationResult PersistentCommonWorldRuntime::beginSourceSession(const std::string& publisher,const std::string& session,
 const std::string& clock,const w::Envelope& bootstrap,std::int64_t at) {
 std::lock_guard<std::mutex> guard(mutex_); ready(); auto before=coordinator_->owner().pendingTransition();
 CommonWorldOperationResult result{"authorization-unavailable",{}};
 if(!guardedGate([&] { result=coordinator_->beginSourceSession(publisher,session,clock,bootstrap,at); },at)) {
  invalidate(); result.events=coordinator_->onSourceTransportDisconnected(at);
 }
 changedRequest(before); return result;
}
CommonWorldOperationResult PersistentCommonWorldRuntime::receiveSource(const w::Envelope& event,std::int64_t at) {
 std::lock_guard<std::mutex> guard(mutex_); ready(); auto before=coordinator_->owner().pendingTransition();
 CommonWorldOperationResult result{"authorization-unavailable",{}};
 if(!guardedGate([&] { result=coordinator_->receiveSource(event,at); },at)) {
  invalidate(); result.events=coordinator_->onSourceTransportDisconnected(at);
 }
 changedRequest(before); return result;
}
std::vector<w::Envelope> PersistentCommonWorldRuntime::disconnect(std::int64_t at) {
 std::lock_guard<std::mutex> guard(mutex_); ready(); invalidate(); return coordinator_->onSourceTransportDisconnected(at);
}
std::vector<w::Envelope> PersistentCommonWorldRuntime::revokeWorld(std::int64_t at) {
 std::lock_guard<std::mutex> guard(mutex_); ready(); invalidate(); return coordinator_->revokeWorld(at);
}
std::vector<w::Envelope> PersistentCommonWorldRuntime::revokeMapping(std::int64_t at) {
 std::lock_guard<std::mutex> guard(mutex_); ready(); invalidate(); return coordinator_->revokeMapping(at);
}
CommonWorldOperationResult PersistentCommonWorldRuntime::beginDryRunPublisherSession(const std::string& session,const std::string& clock,std::int64_t at) {
 std::lock_guard<std::mutex> guard(mutex_); ready(); CommonWorldOperationResult result{"authorization-unavailable",{}};
 if(!guardedGate([&] { result=coordinator_->beginPublisherSession(session,clock,at); },at)) {
  invalidate(); result.events=coordinator_->onSourceTransportDisconnected(at);
 }
 return result;
}
CommonWorldPersistenceTicket PersistentCommonWorldRuntime::prepareTransition() {
 const auto s=store_->open(); // potentially waits for I/O, NEVER holds the runtime/revoke lock
 std::lock_guard<std::mutex> guard(mutex_); ready(); const auto& pending=coordinator_->owner().pendingTransition(); check(bool(pending),"no pending transition");
 check(s.currentAuthorizationEpoch==epoch_,"superseded owner");
 check((!s.highWater.lastWorldRevision || *s.highWater.lastWorldRevision<UINT32_MAX) && s.highWater.lastMappingRevision<UINT32_MAX,"durable revisions exhausted");
 CommonWorldRevisionCandidate c; c.authorizationEpoch=epoch_; c.transition=*pending;
 c.worldRevision=std::max(pending->minimumWorldRevision,s.highWater.lastWorldRevision?*s.highWater.lastWorldRevision+1:0);
 c.mappingRevision=std::max(pending->minimumMappingRevision,s.highWater.lastMappingRevision+1);
 return {generation_,c};
}
CommonWorldPersistenceTicket PersistentCommonWorldRuntime::prepareMappingUpdate(std::uint32_t revision) {
 std::lock_guard<std::mutex> guard(mutex_); ready(); const auto& mapping=coordinator_->owner().currentMapping(); check(bool(mapping),"no current mapping");
 CommonWorldRevisionCandidate c; c.authorizationEpoch=epoch_; c.operation=RevisionOperation::MappingUpdate;
 c.mapping={mapping->mapping,mapping->transform,false}; c.mapping.mapping.mapping_revision=revision;
 c.worldRevision=mapping->mapping.world.coordinate_space.revision; c.mappingRevision=revision;
 return {generation_,c};
}
CommonWorldPersistenceTicket PersistentCommonWorldRuntime::prepareMappingReestablishment(std::uint32_t revision) {
 std::lock_guard<std::mutex> guard(mutex_); ready(); const auto& o=coordinator_->owner(); check(bool(o.currentWorld()) && !o.currentMapping(),"reestablishment requires world without mapping");
 CommonWorldRevisionCandidate c; c.authorizationEpoch=epoch_; c.operation=RevisionOperation::MappingReestablishment;
 // The current source authority contains the exact locate domain, absent from world reference.
 const auto anchor=coordinator_->sourceStream().currentAuthority(); check(bool(anchor),"no current source authority");
 c.reestablishment={o.currentWorld()->world,*anchor,revision,false}; c.worldRevision=o.currentWorld()->world.coordinate_space.revision; c.mappingRevision=revision;
 return {generation_,c};
}
CommonWorldDurableReceipt PersistentCommonWorldRuntime::persist(const CommonWorldPersistenceTicket& ticket,const std::atomic_bool* cancel) {
 {
  std::lock_guard<std::mutex> guard(mutex_); ready();
  check(ticket.authorizationGeneration==generation_ && ticket.candidate.authorizationEpoch==epoch_,"retired or foreign persistence ticket");
 }
 // No runtime lock during I/O: revoke never waits for write/flush/fault pauses.
 return store_->commit(ticket.candidate,cancel);
}
std::vector<w::Envelope> PersistentCommonWorldRuntime::deliver(const CommonWorldPersistenceTicket& ticket,const CommonWorldDurableReceipt& receipt,std::int64_t at) {
 std::lock_guard<std::mutex> guard(mutex_);
 if(disposed_ || ticket.authorizationGeneration!=generation_ || ticket.candidate.authorizationEpoch!=epoch_ || receipt.committed.authorizationEpoch!=epoch_) return {};
 // Candidate and receipt must name the same exact normalized persisted value.
 const auto& c=receipt.committed;
 if(!sameCommonWorldRevisionCandidate(ticket.candidate,c)) return {};
 std::vector<w::Envelope> result;
 guardedGate([&] {
  if(c.operation==RevisionOperation::Transition) {
   const auto& pending=coordinator_->owner().pendingTransition();
   if(!pending || !d::exactRequest(*pending,c.transition)) return;
   result=coordinator_->commitWorldTransition({c.transition,c.worldRevision,c.mappingRevision,true},at);
  } else if(c.operation==RevisionOperation::MappingUpdate) {
   const auto& map=coordinator_->owner().currentMapping();
   if(!map || d::world(map->mapping.world)!=d::world(c.mapping.mapping.world) ||
    d::source(map->mapping.source_space)!=d::source(c.mapping.mapping.source_space) ||
    map->mapping.calibration_epoch!=c.mapping.mapping.calibration_epoch || d::transform(map->transform)!=d::transform(c.mapping.transform)) return;
   auto r=c.mapping; r.persistentCommitConfirmed=true; result=coordinator_->updateMapping(r,at);
  } else {
   const auto& o=coordinator_->owner(); const auto anchor=coordinator_->sourceStream().currentAuthority();
   if(!o.currentWorld() || o.currentMapping() || !anchor || d::world(o.currentWorld()->world)!=d::world(c.reestablishment.world) || d::anchor(*anchor)!=d::anchor(c.reestablishment.anchor)) return;
   auto r=c.reestablishment; r.persistentCommitConfirmed=true; result=coordinator_->reestablishMapping(r,at);
  }
 },at,&receipt);
 return result;
}
void PersistentCommonWorldRuntime::dispose() {
 std::lock_guard<std::mutex> guard(mutex_); if(!disposed_) { coordinator_->onSourceTransportDisconnected(0); disposed_=true; }
}
}
