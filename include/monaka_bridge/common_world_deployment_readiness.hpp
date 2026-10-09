#pragma once
#include "common_world_production_activation.hpp"

namespace monaka_bridge {
enum class CommonWorldReadinessState { NotConfigured, InvalidConfiguration, ArtifactInvalid,
 DependencyMissing, PersistentStateNotReady, RuntimeNotReady, TransportConfigurationInvalid,
 ReadyForControlledActivation };
const char* commonWorldReadinessStateName(CommonWorldReadinessState);
struct CommonWorldPreflightRequest {
 std::filesystem::path artifact, config, source, logs, reports;
 std::string expectedCommit, expectedOwnerEpoch; // optional epoch is a recovery expectation, never a grant
};
struct CommonWorldReadinessResult {
 CommonWorldReadinessState state = CommonWorldReadinessState::NotConfigured;
 std::uint64_t generation = 0;
 std::int64_t observedAtNs = 0;
 std::string reason, json;
};
class CommonWorldReadinessTestRegistration;
/** Observation only. No activation API, token, receipt, socket, or persistent write.
 * Caller coordinates object lifetime. Invalidate retires an in-flight observation;
 * shutdown/emergency are terminal for this observer. Every run rechecks input bytes. */
class CommonWorldDeploymentReadiness {
 struct Dependencies {
  std::function<std::unique_ptr<CommonWorldRevisionStorage>(const std::filesystem::path&)> storage;
  std::function<void(const std::string&)> before;
 };
public:
 CommonWorldDeploymentReadiness();
 CommonWorldReadinessResult preflight(const CommonWorldPreflightRequest&);
 CommonWorldReadinessResult snapshot() const;
 void invalidate(const std::string& reason);
 void shutdown();
 void emergencyRevoke();
private:
 friend class CommonWorldReadinessTestRegistration;
 explicit CommonWorldDeploymentReadiness(Dependencies);
 mutable std::mutex mutex_;
 Dependencies dependencies_;
 std::uint64_t generation_ = 0;
 bool running_ = false, terminal_ = false;
 CommonWorldReadinessResult result_;
 void retire(const std::string&,bool terminal);
};
// Diagnostics are copies, never eligibility. No secret-bearing arbitrary config dump.
std::string commonWorldRuntimeDiagnostics(const CommonWorldActivationSnapshot&,
 const CommonWorldActivationConfig&, const std::string& commit);
// Local revoke first, then teardown. Terminal shutdown prevents old-session restart.
void emergencyRevokeCommonWorld(CommonWorldProductionActivation&, std::int64_t at);
std::string commonWorldBuildCommit();
std::string commonWorldFileSha256(const std::filesystem::path&);
}
