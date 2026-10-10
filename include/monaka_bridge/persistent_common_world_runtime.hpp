#pragma once
#include "common_world_revision_store.hpp"
#include "trusted_hmd_common_world_coordinator.hpp"

namespace monaka_bridge {
struct CommonWorldPersistenceTicket {
 std::uint64_t authorizationGeneration = 0;
 CommonWorldRevisionCandidate candidate;
};
struct CommonWorldRuntimeSnapshot {
 CommonWorldPersistentHighWater highWater;
 std::optional<CommonWorldTransitionRequest> pending;
 std::optional<authority_wire::CommonWorldAuthorityPublication> world;
 std::optional<authority_wire::CommonWorldMappingPublication> mapping;
 bool publisherSessionActive = false, disposed = false;
 std::string ownerEpoch, sourcePublisher, sourceSession, sourceClock;
 std::optional<std::int64_t> sourceSequence;
 std::optional<authority_wire::TrustedHmdSourceAuthority> sourceAuthority;
 std::optional<TrustedHmdSourceObservationSnapshot> sourceObservation;
};
/** Dormant adapter: no thread, sender, live source or production wiring.
 * persist may run outside the runtime lock. All owner operations and delivery serialize here.
 * Source/session APIs are exclusively synthetic/in-process seams in this phase. */
class PersistentCommonWorldRuntime {
public:
 PersistentCommonWorldRuntime(std::shared_ptr<CommonWorldRevisionStore>,std::string freshAuthorizationEpoch,
  std::string sourcePublisher,std::size_t capacity = 128);
 CommonWorldRuntimeSnapshot snapshot() const;
 CommonWorldOperationResult beginSourceSession(const std::string&,const std::string&,const std::string&,
  const authority_wire::Envelope&,std::int64_t at);
 CommonWorldOperationResult receiveSource(const authority_wire::Envelope&,std::int64_t at);
 std::vector<authority_wire::Envelope> disconnect(std::int64_t at);
 std::vector<authority_wire::Envelope> revokeWorld(std::int64_t at);
 std::vector<authority_wire::Envelope> revokeMapping(std::int64_t at);
 CommonWorldOperationResult beginDryRunPublisherSession(const std::string&,const std::string&,std::int64_t at);
 CommonWorldPersistenceTicket prepareTransition();
 CommonWorldPersistenceTicket prepareMappingUpdate(std::uint32_t revision);
 CommonWorldPersistenceTicket prepareMappingReestablishment(std::uint32_t revision);
 CommonWorldDurableReceipt persist(const CommonWorldPersistenceTicket&,const std::atomic_bool* cancel = nullptr);
 std::vector<authority_wire::Envelope> deliver(const CommonWorldPersistenceTicket&,const CommonWorldDurableReceipt&,std::int64_t at);
 void dispose();
private:
 mutable std::mutex mutex_;
 std::shared_ptr<CommonWorldRevisionStore> store_;
 std::string epoch_;
 std::string sourcePublisher_, sourceSession_, sourceClock_;
 std::unique_ptr<TrustedHmdCommonWorldCoordinator> coordinator_;
 std::uint64_t generation_ = 1;
 bool disposed_ = false;
 void ready() const;
 void invalidate();
 void changedRequest(const std::optional<CommonWorldTransitionRequest>& before);
 bool guardedGate(const std::function<void()>&, std::int64_t at, const CommonWorldDurableReceipt* = nullptr);
};
}
