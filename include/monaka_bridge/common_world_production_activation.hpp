#pragma once
#include "common_world_activation_config.hpp"
#include "common_world_live_sender.hpp"

namespace monaka_bridge {
enum class CommonWorldActivationStatus { DisabledByDefault, ConfigInvalid, RecoveryPending, OwnerUnavailable,
 ReadyButInactive, ArmedDisabled, Activating, Active, Deactivating, Faulted, Shutdown };
enum class CommonWorldStartupStage { ConfigLoad, Validation, Recovery, OwnerInitialization, SourceReadiness,
 SenderConstruction, Gate, ExplicitEnable, TransportActivation };
const char* commonWorldActivationStatusName(CommonWorldActivationStatus);
struct CommonWorldActivationSnapshot {
 CommonWorldActivationStatus status;
 std::uint64_t generation;
 bool recoveryCompleted, shutdown;
 std::string reason;
 std::vector<CommonWorldStartupStage> stages;
 std::optional<CommonWorldSenderSnapshot> sender;
 std::string runtimeInstance, controlSession, ownerEpoch, sourceLease, readinessToken;
 std::uint64_t readinessGeneration = 0;
 std::int64_t observedAtNs = 0, observedAtUnixMs = 0;
 bool durableReceiptCurrent = false;
 std::int64_t lastTransitionNs = 0, lastAcceptedSourceNs = 0, lastRejectedSourceNs = 0;
 std::string lastAcceptedSourceResult, lastRejectedSourceResult;
 std::optional<std::int64_t> lastAcceptedSourceSequence, lastRejectedSourceSequence;
 bool readinessValid = false, emergencyRevoked = false, ownerRevoked = false, configurationEnabled = false;
 std::optional<CommonWorldStoredState> durableState;
 std::string storeError, lastOperation, lastControlResult, lastSourceResult, lastReconnectResult;
};
struct CommonWorldControlRequest {
 std::string operationId, operation, runtimeInstance, controlSession, readinessToken, sourceLease;
 std::uint64_t sourceGeneration = 0;
 bool explicitEnable = false;
 std::optional<std::filesystem::path> config;
 std::optional<authority_wire::Envelope> envelope;
};
struct CommonWorldControlResult {
 std::string operationId, operation, result;
 bool accepted = false;
 CommonWorldActivationSnapshot before, after;
};
class CommonWorldActivationTestRegistration;
/** Production policy/orchestration boundary. No driver, listener, global config or timer.
 * Only the fixed production registration is exposed here. Source/attempt handles cannot
 * escape to the underlying sender. All publication/revoke entry points serialize here;
 * recovery and durable commit do not hold the activation/revoke mutex. */
class CommonWorldProductionActivation {
 class StagedTransport;
 struct Session;
 struct Dependencies {
  CommonWorldExecutionMode mode = CommonWorldExecutionMode::Production;
  std::function<std::unique_ptr<CommonWorldRevisionStorage>(const std::filesystem::path&)> storage;
  std::function<std::unique_ptr<CommonWorldDatagramTransport>(const CommonWorldActivationConfig&)> transport;
  std::function<std::string()> freshEpoch;
  std::function<void(CommonWorldStartupStage)> beforeStage;
 };
public:
 class Completion {
 public:
  Completion() = default;
  explicit operator bool() const { return bool(attempt_); }
 private:
  friend class CommonWorldProductionActivation;
  std::weak_ptr<Session> session_;
  std::uint64_t generation_ = 0;
  CommonWorldLiveSender::Attempt attempt_;
 };
 struct Submission { std::string status; Completion completion; };
 CommonWorldProductionActivation(); // inert, fixed production transport registration
 ~CommonWorldProductionActivation();
 CommonWorldProductionActivation(const CommonWorldProductionActivation&) = delete;
 CommonWorldProductionActivation& operator=(const CommonWorldProductionActivation&) = delete;
 // Loads an explicit file, recovers, initializes owner/source and commits the current
 // revision. Returning ReadyButInactive NEVER opens a production socket.
 std::string prepare(const std::optional<std::filesystem::path>& config,
  const authority_wire::Envelope& authorizedSourceBootstrap,std::int64_t at);
 // Caller must explicitly select the exact current world. A config flag is insufficient.
 std::string activate(const authority_wire::CommonWorldReference& expectedWorld,std::int64_t at);
 void deactivate();
 void shutdown(std::int64_t at = 0);
 void revokeOwner(std::int64_t at);
 CommonWorldSenderSourceResult receive(const authority_wire::Envelope&,std::int64_t at);
 Submission pump(const std::atomic_bool* cancel = nullptr);
 std::string complete(const Completion&,DatagramSubmissionResult);
 CommonWorldActivationStatus status() const noexcept { return status_.load(); } // observation only, no eligibility
 CommonWorldActivationSnapshot snapshot() const;
// Explicit local control; every mutation dispatches to production lifecycle APIs.
 CommonWorldControlResult control(const CommonWorldControlRequest&,std::int64_t at);
 std::string prepareArmed(const std::filesystem::path&,const authority_wire::Envelope&,std::int64_t at);
 std::string refreshReadiness();
 std::string activateReady(const std::string& token,bool explicitEnable,std::int64_t at);
 std::string reconnectSource(std::int64_t at);
 std::string authorizeReconnectedSource(const authority_wire::Envelope&,std::int64_t at,
  const std::string& expectedLease = {},std::uint64_t expectedGeneration = 0);
 std::string reconnectTransport(std::int64_t at);
 void disconnectSource(std::int64_t at);
 void emergencyRevoke(std::int64_t at);
 CommonWorldSenderSourceResult receiveCurrent(const std::string& lease,std::uint64_t generation,
  const authority_wire::Envelope&,std::int64_t at);
private:
 friend class CommonWorldActivationTestRegistration; // defined ONLY in tests, not production target
 explicit CommonWorldProductionActivation(Dependencies);
 mutable std::recursive_mutex mutex_;
 Dependencies dependencies_;
 std::atomic<CommonWorldActivationStatus> status_{CommonWorldActivationStatus::DisabledByDefault};
 std::uint64_t generation_ = 0;
 bool preparing_ = false, shutdown_ = false;
 std::string reason_;
 const std::string runtimeInstance_, controlSession_;
 std::string readinessToken_, sourceLease_, lastOperation_, lastControlResult_, lastSourceResult_, lastReconnectResult_;
 std::uint64_t readinessGeneration_ = 0;
 std::atomic_bool ownerRevoked_{false}, emergencyRevoked_{false};
 std::int64_t lastTransitionNs_ = 0, lastAcceptedSourceNs_ = 0, lastRejectedSourceNs_ = 0;
 std::string lastAcceptedSourceResult_, lastRejectedSourceResult_;
 std::optional<std::int64_t> lastAcceptedSourceSequence_, lastRejectedSourceSequence_;
 std::string prepareImpl(const std::optional<std::filesystem::path>&,const authority_wire::Envelope&,std::int64_t,bool);
 std::string activateImpl(const authority_wire::CommonWorldReference&,std::int64_t,bool);
 bool configurationCurrent(const Session&) const;
 std::vector<CommonWorldStartupStage> stages_;
 std::shared_ptr<Session> session_;
 void increment();
 void retirePublication();
 void observeSenderFault();
 std::string gate(const Session&,const authority_wire::CommonWorldReference&) const;
};
}
