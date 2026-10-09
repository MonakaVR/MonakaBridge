#pragma once
#include "common_world_activation_config.hpp"
#include "common_world_live_sender.hpp"

namespace monaka_bridge {
enum class CommonWorldActivationStatus { DisabledByDefault, ConfigInvalid, RecoveryPending, OwnerUnavailable,
 ReadyButInactive, Activating, Active, Deactivating, Faulted, Shutdown };
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
private:
 friend class CommonWorldActivationTestRegistration; // defined ONLY in tests, not production target
 explicit CommonWorldProductionActivation(Dependencies);
 mutable std::mutex mutex_;
 Dependencies dependencies_;
 std::atomic<CommonWorldActivationStatus> status_{CommonWorldActivationStatus::DisabledByDefault};
 std::uint64_t generation_ = 0;
 bool preparing_ = false, shutdown_ = false;
 std::string reason_;
 std::vector<CommonWorldStartupStage> stages_;
 std::shared_ptr<Session> session_;
 void increment();
 void retirePublication();
 void observeSenderFault();
 std::string gate(const Session&,const authority_wire::CommonWorldReference&) const;
};
}
