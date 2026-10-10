#pragma once
#include "trusted_hmd_source_stream.hpp"
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <set>

namespace monaka_bridge {
struct TrustedLiveSourceConfig {
 std::string sourcePublisherId, sourceId, sourceSpaceId;
 std::size_t capacity = 16;
};
/** One real ALVR/OpenXR accepted-evidence adapter. It cannot authorize A2 or write state.
 * The injectable UUID factory is an OS seam for deterministic tests, never a publisher ID. */
class TrustedLiveSourceProducer {
public:
 TrustedLiveSourceProducer(TrustedLiveSourceConfig, std::function<std::string()> freshUuid);
 std::string receiveExport(const std::string& frame, std::int64_t receiveNs);
 void noData(); // temporary no-data never creates EOF, sequence or freshness
 void disconnect(const std::string& reason);
 std::optional<authority_wire::Envelope> take();
 std::string snapshot(const std::string& commit, const std::string& runtimeInstance) const;
 const std::string& publisherId() const { return config_.sourcePublisherId; }
 std::size_t queueDepth() const { return queue_.size(); }
 bool connected() const { return connected_; }
private:
 struct Observation {
  std::int64_t id, generation, locate;
  std::array<std::uint32_t,7> bits;
  std::uint32_t validity;
  bool retired = false;
 };
 TrustedLiveSourceConfig config_;
 std::function<std::string()> fresh_;
 std::deque<authority_wire::Envelope> queue_;
 std::map<std::string,Observation> history_;
 std::set<std::string> exportSessions_, publicationSessions_;
 std::string exportSession_, exporterCommit_, publisherSession_, clock_, token_, lifecycle_ = "Created", lastFailure_;
 std::optional<authority_wire::SourceSpaceAuthority> authority_;
 std::optional<authority_wire::Envelope> lastEmitted_;
 std::optional<std::int64_t> sequence_, lastReceivedNs_, lastLocate_;
 std::uint64_t generation_ = 0, reconnects_ = 0, drops_ = 0, rejects_ = 0;
 std::uint64_t exportDrops_ = 0, exportRejects_ = 0;
 std::uint64_t exportSkips_ = 0, exportIntervalMs_ = 0;
 std::uint32_t validity_ = 0;
 bool connected_ = false;
 bool poseUsable_ = false;
 void freshPublication();
 bool enqueue(authority_wire::Envelope);
};
}
