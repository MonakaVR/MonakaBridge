#pragma once
#include <monaka/protocol/v2/codec.hpp>
#include <map>
#include <set>
#include <utility>

namespace monaka_bridge {
namespace authority_wire = monaka::protocol::v2;

/** Dormant immutable observation receipt. No common-space conversion or production output. */
struct TrustedHmdSourceObservationSnapshot {
 authority_wire::TrustedHmdSourcePose observation;
 std::int64_t received_at_ns;
};

/** Single caller-serialized, explicitly authorized channel; creates no transport/thread. */
class TrustedHmdSourceStream {
public:
 explicit TrustedHmdSourceStream(std::string expected_publisher_id, std::size_t capacity = 128);
 bool beginAuthorizedPublisherSession(const std::string& publisher, const std::string& session,
                                      const std::string& clock);
 std::string receiveBytes(const std::string& bytes, std::int64_t received_at_ns);
 std::string receive(const authority_wire::Envelope& decoded, std::int64_t received_at_ns);
 void onTransportDisconnected();
 bool streamValid() const { return valid_ && !exhausted_; }
 const std::optional<authority_wire::TrustedHmdSourceAuthority>& currentAuthority() const { return authority_; }
 const std::optional<TrustedHmdSourceObservationSnapshot>& candidate() const { return candidate_; }
 std::optional<std::int64_t> sequenceHighWater() const { return last_sequence_; }
 std::size_t publisherSessionsRetained() const { return sessions_.size(); }
private:
 struct SourceHistory {
  authority_wire::TrustedHmdSourceAuthority publication;
  bool generation_retired = false;
  bool token_retired = false;
  std::optional<authority_wire::TrustedHmdSourcePose> observation;
 };
 using SourceKey = std::pair<std::string,std::string>;
 std::string expected_publisher_, session_, clock_;
 std::size_t capacity_;
 bool valid_ = false, exhausted_ = false;
 std::set<std::string> sessions_; // all selected sessions, including current; never evict
 std::map<SourceKey,SourceHistory> sources_;
 std::optional<SourceKey> selected_source_; // selection survives publisher-session replacement
 std::optional<std::int64_t> last_sequence_;
 std::optional<authority_wire::Envelope> last_message_;
 std::optional<authority_wire::TrustedHmdSourceAuthority> authority_;
 std::optional<TrustedHmdSourceObservationSnapshot> candidate_;
 void invalidate();
 void exhaust();
 std::string apply(const authority_wire::TrustedHmdSourceAuthority&);
 std::string apply(const authority_wire::TrustedHmdSourcePose&, std::int64_t);
 std::string apply(const authority_wire::TrustedHmdSourceUnavailable&);
 std::string apply(const authority_wire::TrustedHmdSourceRevocation&);
};
}
