#pragma once
#include "common_world_publisher.hpp"

namespace monaka_bridge {
struct CommonWorldOperationResult {
 std::string status;
 std::vector<authority_wire::Envelope> events;
};
/** Dormant, serialized composition seam; returns each complete ordered transition immediately. */
class TrustedHmdCommonWorldCoordinator {
public:
 TrustedHmdCommonWorldCoordinator(std::string sourcePublisher,CommonWorldPersistentHighWater,
  CommonWorldAuthorityOwner::TokenFactory worldToken,CommonWorldAuthorityOwner::TokenFactory calibrationToken,
  std::size_t capacity = 128,std::int64_t sequenceLimit = std::numeric_limits<std::int64_t>::max());
 CommonWorldOperationResult beginSourceSession(const std::string& publisher,const std::string& session,
  const std::string& clock,const authority_wire::Envelope& bootstrap,std::int64_t at);
 CommonWorldOperationResult receiveSource(const authority_wire::Envelope&,std::int64_t at);
 CommonWorldOperationResult receiveSourceBytes(const std::string&,std::int64_t at);
 std::vector<authority_wire::Envelope> onSourceTransportDisconnected(std::int64_t at);
 CommonWorldOperationResult beginPublisherSession(const std::string& session,const std::string& clock,std::int64_t at);
 std::vector<authority_wire::Envelope> commitWorldTransition(const CommonWorldTransitionReceipt&,std::int64_t at);
 std::vector<authority_wire::Envelope> updateMapping(const CommonWorldMappingReceipt&,std::int64_t at);
 std::vector<authority_wire::Envelope> reestablishMapping(const CommonWorldMappingReestablishmentReceipt&,std::int64_t at);
 std::vector<authority_wire::Envelope> revokeMapping(std::int64_t at);
 std::vector<authority_wire::Envelope> revokeWorld(std::int64_t at);
 const auto& owner() const { return owner_; }
 const auto& publisher() const { return publisher_; }
 const auto& sourceStream() const { return source_; }
private:
 TrustedHmdSourceStream source_;
 CommonWorldAuthorityOwner owner_;
 CommonWorldPublisher publisher_;
 CommonWorldOperationResult afterSource(std::string,const authority_wire::Envelope*,std::int64_t);
 std::vector<authority_wire::Envelope> emit(CommonWorldEvents,std::int64_t);
};
}
