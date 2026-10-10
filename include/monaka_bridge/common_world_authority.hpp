#pragma once
#include "trusted_hmd_source_stream.hpp"
#include <functional>

namespace monaka_bridge {
// Header-free owner events. Only the separate publisher assigns wire headers.
using CommonWorldEvent = std::variant<authority_wire::CommonWorldAuthorityPublication,
 authority_wire::CommonWorldMappingPublication, authority_wire::TrustedHmdCommonPose,
 authority_wire::TrustedHmdCommonUnavailable, authority_wire::CommonWorldMappingRevocation,
 authority_wire::CommonWorldRevocation>;
using CommonWorldEvents = std::vector<CommonWorldEvent>;

struct CommonWorldPersistentHighWater {
 std::string ownerId, worldSpaceId;
 std::optional<std::uint32_t> lastWorldRevision;
 std::uint32_t lastMappingRevision = 0;
 // Empty is explicitly legacy-only. A present canonical publisher selects 2.2.
 std::string sourcePublisher;
};
struct CommonWorldTransitionRequest {
 std::uint64_t requestId = 0; // owner-lifetime, nonwrapping; never an epoch
 std::string ownerId, worldSpaceId;
 authority_wire::TrustedHmdSourceAuthority anchor; // payload facts, not publisher lifetime
 std::uint32_t minimumWorldRevision = 0, minimumMappingRevision = 0;
 std::string reason;
};
struct CommonWorldTransitionReceipt {
 CommonWorldTransitionRequest request;
 std::uint32_t worldRevision = 0, mappingRevision = 0;
 bool persistentCommitConfirmed = false; // trusted external caller attestation, no I/O here
};
struct CommonWorldMappingReceipt {
 authority_wire::CommonMappingReference mapping;
 authority_wire::RigidTransform transform;
 bool persistentCommitConfirmed = false;
 std::string sourcePublisher;
};
struct CommonWorldMappingReestablishmentReceipt {
 authority_wire::CommonWorldReference world;
 authority_wire::TrustedHmdSourceAuthority anchor;
 std::uint32_t mappingRevision = 0;
 bool persistentCommitConfirmed = false;
};

/** Caller-serialized lifecycle owner. No persistence adapter or production trust capability. */
class CommonWorldAuthorityOwner {
public:
 using TokenFactory = std::function<std::string()>;
 CommonWorldAuthorityOwner(CommonWorldPersistentHighWater, TokenFactory worldToken,
                           TokenFactory calibrationToken, std::size_t capacity = 128);
 CommonWorldEvents reconcile(const TrustedHmdSourceStream&, const std::string& lossReason = "event_stream_lost");
 CommonWorldEvents commitWorldTransition(const CommonWorldTransitionReceipt&);
 CommonWorldEvents updateMapping(const CommonWorldMappingReceipt&);
 CommonWorldEvents reestablishMapping(const CommonWorldMappingReestablishmentReceipt&);
 CommonWorldEvents revokeMapping(const std::string& reason = "mapping_withdrawn");
 CommonWorldEvents revokeWorld(const std::string& reason = "explicit_world_replacement");
 CommonWorldEvents sourcePose(const TrustedHmdSourceObservationSnapshot&);
 CommonWorldEvents sourceUnavailable(const authority_wire::TrustedHmdSourceUnavailable&);
 CommonWorldEvents fullSnapshot() const;
 const auto& pendingTransition() const { return pending_; }
 const auto& currentWorld() const { return world_; }
 const auto& currentMapping() const { return mapping_; }
 const auto& highWater() const { return high_; }
 bool exhausted() const { return exhausted_; }
private:
 CommonWorldPersistentHighWater high_;
 TokenFactory worldToken_, calibrationToken_;
 std::size_t capacity_;
 bool exhausted_ = false;
 std::uint64_t requestId_ = 0;
 std::set<std::string> worldTokens_, calibrationTokens_;
 std::optional<authority_wire::TrustedHmdSourceAuthority> anchor_;
 std::optional<CommonWorldTransitionRequest> pending_;
 std::optional<authority_wire::CommonWorldAuthorityPublication> world_;
 std::optional<authority_wire::CommonWorldMappingPublication> mapping_;
 // Source-token observation high-water survives world/mapping/reconnect transitions.
 std::map<std::pair<std::string,std::string>,std::int64_t> observations_;
 CommonWorldEvents close();
 std::optional<std::string> mint(const TokenFactory&, std::set<std::string>&);
};
}
