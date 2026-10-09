#pragma once
#include "common_world_authority.hpp"
#include <limits>

namespace monaka_bridge {
/** Ordered synchronous wire stamper. Owns no source/world/mapping authority or transport. */
class CommonWorldPublisher {
public:
 explicit CommonWorldPublisher(std::string publisherId,std::size_t capacity = 128,
  std::int64_t sequenceLimit = std::numeric_limits<std::int64_t>::max());
 bool beginSession(const std::string& session,const std::string& clock);
 std::vector<authority_wire::Envelope> publish(const CommonWorldEvents&,std::int64_t eventTimeNs);
 bool sessionActive() const { return active_ && !exhausted_; }
 bool needsBootstrap() const { return !lastSequence_; }
 bool exhausted() const { return exhausted_; }
 const auto& sequenceHighWater() const { return lastSequence_; }
private:
 std::string publisher_, session_, clock_;
 std::size_t capacity_;
 std::int64_t limit_;
 bool active_ = false, exhausted_ = false;
 std::set<std::string> sessions_, clocks_;
 std::optional<std::int64_t> lastSequence_, lastTime_;
};
}
