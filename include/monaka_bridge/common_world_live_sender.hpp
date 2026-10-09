#pragma once
#include "common_world_live_ingress.hpp"

namespace monaka_bridge {
enum class DatagramSubmission { Accepted, Rejected, Pending, Ambiguous, Cancelled, TimedOut };
struct DatagramSubmissionResult { DatagramSubmission status; std::size_t acceptedBytes = 0; };
/** Nonblocking local datagram submission ONLY. Implementations must not block, reenter
 * the controller/store, invoke completion inline, or start a hidden send after returning
 * Rejected. Pending may only defer completion of an attempt already entered during
 * submit, never schedule a future new send. close must also be nonblocking. Pending
 * means outcome unknown, not success. No peer/application ACK exists. */
class CommonWorldDatagramTransport {
public:
 virtual ~CommonWorldDatagramTransport() = default;
 virtual bool connect() = 0;
 virtual void close() noexcept = 0;
 virtual DatagramSubmissionResult submit(std::string_view bytes) = 0;
};
// Explicit test-only existing mb::Udp endpoint: IPv4 loopback, caller-owned test sink,
// nonzero non-production port; creates no socket until connect. No configurable IP.
std::unique_ptr<CommonWorldDatagramTransport> makeCommonWorldLoopbackDatagramTransport(std::uint16_t testPort);
struct CommonWorldSenderSnapshot {
 bool connected, enabled, disposed, awaitingCompletion;
 std::uint64_t publisherGeneration, transportGeneration;
 std::size_t queued, capacity, headAttempts;
 std::optional<std::int64_t> lastLocalAcceptedSequence; // volatile local API cursor, never ACK/checkpoint
 CommonWorldLiveSnapshot ingress;
};
struct CommonWorldSenderSourceResult { CommonWorldOperationResult source; std::string publication; };
/** Explicit software-only composition. Owns source/owner mutations, bounded queue and
 * final store/current-owner gate. No production wiring, worker, timers or default endpoint. */
class CommonWorldLiveSender {
 struct State;
 struct Plan;
public:
 class Source {
 public:
  Source() = default;
  CommonWorldSenderSourceResult receive(const authority_wire::Envelope&,std::int64_t at) const;
  std::uint64_t generation() const { return inner_.generation(); }
 private:
  friend class CommonWorldLiveSender;
  std::weak_ptr<State> state_;
  CommonWorldLiveIngress::Callback inner_;
 };
 class Attempt {
 public:
  Attempt() = default;
  explicit operator bool() const { return bool(plan_); }
 private:
  friend class CommonWorldLiveSender;
  std::shared_ptr<const Plan> plan_;
 };
 struct PumpResult { std::string status; Attempt completion; };
 CommonWorldLiveSender(std::shared_ptr<CommonWorldRevisionStore>,std::string freshEpoch,std::string sourcePublisher,
  std::unique_ptr<CommonWorldDatagramTransport>,std::size_t capacity=64,std::size_t retryLimit=3);
 ~CommonWorldLiveSender();
 CommonWorldLiveSender(const CommonWorldLiveSender&)=delete;
 CommonWorldLiveSender& operator=(const CommonWorldLiveSender&)=delete;
 Source startSource(std::int64_t at);
 CommonWorldOperationResult authorizeSource(const Source&,const std::string&,const std::string&,const std::string&,
  const authority_wire::Envelope&,std::int64_t at);
 CommonWorldLiveCommitResult commitSource(Source,std::int64_t at,const std::atomic_bool* cancel=nullptr);
 void stopSource(const Source&,std::int64_t at);
 void revokeOwner(std::int64_t at);
 bool connect(); // always retires publication; reconnect grants no authority
 void disconnect();
 std::string enablePublisher(const CommonWorldDurableReceipt&,const std::string& session,const std::string& clock,std::int64_t at);
 void disablePublisher();
 Attempt prepare(); // reserve head only; no serialization/send or queue pop
 PumpResult submit(Attempt,const std::atomic_bool* cancel=nullptr);
 PumpResult pump(const std::atomic_bool* cancel=nullptr);
 std::string complete(Attempt,DatagramSubmissionResult); // local transport completion ONLY, never ACK
 void dispose();
 CommonWorldSenderSnapshot snapshot() const;
private:
 std::shared_ptr<State> state_;
};
}
