#pragma once
#include "persistent_common_world_runtime.hpp"

namespace monaka_bridge {
struct CommonWorldLiveCommitResult {
 std::string status;
 std::optional<CommonWorldDurableReceipt> receipt;
 std::vector<authority_wire::Envelope> events; // in-process dry-run values only
 bool eligible = false;
 std::string error;
};
struct CommonWorldLiveSnapshot {
 std::uint64_t sourceGeneration;
 bool sourceActive, ownerApproved, persistenceBusy, disposed;
 CommonWorldRuntimeSnapshot runtime;
};
/** Explicit in-process callback boundary for the existing typed source receiver.
 * No listener, worker, queue, sender or automatic activation. Lifecycle and owner
 * operations serialize briefly; persistence runs outside the lifecycle lock. */
class CommonWorldLiveIngress {
 struct State;
public:
 class Callback {
 public:
  Callback() = default;
  CommonWorldOperationResult receive(const authority_wire::Envelope&,std::int64_t at) const;
  std::uint64_t generation() const { return generation_; }
 private:
  friend class CommonWorldLiveIngress;
  Callback(const std::shared_ptr<State>& s,std::uint64_t g):state_(s),generation_(g) {}
  std::weak_ptr<State> state_;
  std::uint64_t generation_ = 0;
 };
 CommonWorldLiveIngress(std::shared_ptr<CommonWorldRevisionStore>,std::string freshAuthorizationEpoch,
  std::string sourcePublisher,std::size_t capacity = 128);
 ~CommonWorldLiveIngress();
 CommonWorldLiveIngress(const CommonWorldLiveIngress&) = delete;
 CommonWorldLiveIngress& operator=(const CommonWorldLiveIngress&) = delete;
 Callback start(std::int64_t at); // start/reconnect NEVER grants owner approval
 CommonWorldOperationResult authorize(const Callback&,const std::string& publisher,
  const std::string& session,const std::string& clock,const authority_wire::Envelope& bootstrap,std::int64_t at);
 CommonWorldLiveCommitResult commitPending(Callback,std::int64_t at,const std::atomic_bool* cancel = nullptr);
 void stop(const Callback&,std::int64_t at);
 void sourceFault(const Callback&,std::int64_t at);
 void revokeOwner(std::int64_t at); // source remains running, approval retires permanently for this generation
 void dispose();
 CommonWorldLiveSnapshot snapshot() const;
 // Explicit test-only observation seam: dormant publisher returns vectors, never performs I/O.
 CommonWorldOperationResult beginDryRunPublisherSession(const std::string&,const std::string&,std::int64_t at);
private:
 std::shared_ptr<State> state_;
};
}
