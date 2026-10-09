#include "monaka_bridge/trusted_hmd_source_stream.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace monaka_bridge {
namespace w = authority_wire;
namespace {
template<class T> constexpr bool sourceMessage = std::is_same_v<T,w::TrustedHmdSourceAuthority> ||
 std::is_same_v<T,w::TrustedHmdSourcePose> || std::is_same_v<T,w::TrustedHmdSourceUnavailable> ||
 std::is_same_v<T,w::TrustedHmdSourceRevocation>;
auto sourceFacts(const w::SourceSpaceAuthority& s) {
 return std::make_tuple(s.source_id,s.source_authority_session_epoch,s.source_space_id,s.source_space_generation);
}
auto sourceKey(const w::SourceSpaceAuthority& s) { return std::make_pair(s.source_id,s.source_authority_session_epoch); }
auto validityFacts(const w::HmdValidityEvidence& v) {
 return std::make_tuple(v.position_valid,v.orientation_valid,v.position_tracked,v.orientation_tracked,
                        v.view_position_valid,v.view_orientation_valid);
}
auto validityFacts(const std::optional<w::HmdValidityEvidence>& v) {
 return std::make_pair(v.has_value(),validityFacts(v.value_or(w::HmdValidityEvidence{})));
}
template<std::size_t N> auto bits(const std::array<double,N>& a) {
 std::array<std::uint64_t,N> result{};
 for (std::size_t i=0;i<N;++i) std::memcpy(&result[i],&a[i],sizeof(double));
 return result;
}
auto payload(const w::TrustedHmdSourceAuthority& m) {
 return std::make_tuple(sourceFacts(m.source_space),m.source_space_kind,m.source_time_domain_id);
}
auto payload(const w::TrustedHmdSourcePose& m) {
 return std::make_tuple(sourceFacts(m.source.source_space),m.source.observation_id,m.source.source_locate_time_ns,
  m.source.source_time_domain_id,bits(m.position),bits(m.orientation),validityFacts(m.validity));
}
auto payload(const w::TrustedHmdSourceUnavailable& m) {
 return std::make_tuple(sourceFacts(m.source_space),m.reason,validityFacts(m.validity));
}
auto payload(const w::TrustedHmdSourceRevocation& m) { return std::make_tuple(sourceFacts(m.source_space),m.reason); }
template<class T> auto immutable(const T& m) {
 // All known header facts except resend-mutable sent_at_ns. Compare typed fields, never JSON bytes.
 return std::make_tuple(m.version.major,m.version.minor,m.publisher_id,m.session_id,m.clock_id,
                        m.sequence,m.timestamp_ns,m.timestamp_kind,payload(m));
}
bool sameMessage(const w::Envelope& a,const w::Envelope& b) {
 if(a.index()!=b.index()) return false;
 return std::visit([&](const auto& x) {
  using T=std::decay_t<decltype(x)>;
  if constexpr(sourceMessage<T>) return immutable(x)==immutable(std::get<T>(b));
  else return false;
 },a);
}
}

TrustedHmdSourceStream::TrustedHmdSourceStream(std::string expected,std::size_t capacity)
 : expected_publisher_(std::move(expected)),capacity_(capacity) {
 if(expected_publisher_.find_first_not_of(" \t\r\n")==std::string::npos || !capacity_)
  throw std::invalid_argument("explicit publisher and positive retention capacity required");
}
void TrustedHmdSourceStream::invalidate() { valid_=false; authority_.reset(); candidate_.reset(); }
void TrustedHmdSourceStream::exhaust() { exhausted_=true; invalidate(); }
bool TrustedHmdSourceStream::beginAuthorizedPublisherSession(const std::string& publisher,
 const std::string& session,const std::string& clock) {
 if(exhausted_ || publisher!=expected_publisher_ || session.empty() || clock.empty()) return false;
 if(sessions_.count(session)) return false; // no same-session repair/reassociation
 if(sessions_.size()>=capacity_) { exhaust(); return false; }
 sessions_.insert(session); session_=session; clock_=clock;
 authority_.reset(); candidate_.reset(); last_message_.reset(); last_sequence_.reset(); valid_=true;
 return true;
}
void TrustedHmdSourceStream::onTransportDisconnected() { invalidate(); }
std::string TrustedHmdSourceStream::receiveBytes(const std::string& bytes,std::int64_t at) {
 w::Envelope value; w::Error error;
 if(!w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),value,error)) {
  invalidate(); return "malformed";
 }
 return receive(value,at);
}
std::string TrustedHmdSourceStream::receive(const w::Envelope& value,std::int64_t at) {
 return std::visit([&](const auto& m)->std::string {
  using T=std::decay_t<decltype(m)>;
  if constexpr(!sourceMessage<T>) return "wrong-protocol";
  else {
   if(m.publisher_id!=expected_publisher_) return "unauthorized";
   if(m.session_id!=session_) return sessions_.count(m.session_id)?"retired":"unauthorized";
   if(!streamValid()) return "closed";
   std::string unused; w::Error error;
   if(!w::EncodeEnvelope(value,unused,error)) { invalidate(); return "malformed"; }
   if(last_sequence_) {
    if(m.sequence<*last_sequence_) return "old";
    if(m.sequence==*last_sequence_) {
     if(sameMessage(value,*last_message_)) return "duplicate";
     invalidate(); return "conflict";
    }
    if(*last_sequence_==std::numeric_limits<std::int64_t>::max() || m.sequence!=*last_sequence_+1) {
     invalidate(); return "gap";
    }
   } else if(m.sequence!=0) { invalidate(); return "gap"; }
   if(m.clock_id!=clock_) { invalidate(); return "conflict"; }
   last_sequence_=m.sequence; last_message_=value;
   if(m.sequence==0 && !std::is_same_v<T,w::TrustedHmdSourceAuthority>) {
    invalidate(); return "bootstrap-failed";
   }
   if constexpr(std::is_same_v<T,w::TrustedHmdSourcePose>) return apply(m,at);
   else return apply(m);
  }
 },value);
}
std::string TrustedHmdSourceStream::apply(const w::TrustedHmdSourceAuthority& m) {
 auto key=sourceKey(m.source_space);
 auto it=sources_.find(key);
 if(it==sources_.end()) {
  if(sources_.size()>=capacity_) { exhaust(); return "closed"; }
  it=sources_.emplace(key,SourceHistory{m,false,false,std::nullopt}).first;
 } else {
  auto& h=it->second;
  const auto generation=m.source_space.source_space_generation;
  const auto high=h.publication.source_space.source_space_generation;
  if(h.token_retired || generation<high || (generation==high && h.generation_retired)) return "untrusted";
  if(generation==high && payload(m)!=payload(h.publication)) {
   h.generation_retired=true; authority_.reset(); candidate_.reset(); return "untrusted";
  }
  // One locate-clock domain per source token. A changed domain requires a new source token.
  if(m.source_time_domain_id!=h.publication.source_time_domain_id) {
   h.token_retired=true; authority_.reset(); candidate_.reset(); return "untrusted";
  }
  h.publication=m; h.generation_retired=false;
 }
 // Selecting another source token retires the prior token; it cannot ABA back into this receiver.
 if(selected_source_ && *selected_source_!=key) sources_.at(*selected_source_).token_retired=true;
 selected_source_=key;
 authority_=m; candidate_.reset(); return "authority";
}
std::string TrustedHmdSourceStream::apply(const w::TrustedHmdSourcePose& m,std::int64_t at) {
 if(!authority_ || sourceFacts(m.source.source_space)!=sourceFacts(authority_->source_space) ||
    m.source.source_time_domain_id!=authority_->source_time_domain_id) return "untrusted";
 auto& h=sources_.at(sourceKey(m.source.source_space));
 if(h.observation) {
  if(m.source.observation_id<h.observation->source.observation_id) return "old-observation";
  if(m.source.observation_id==h.observation->source.observation_id) {
   if(payload(m)==payload(*h.observation)) return "duplicate-observation";
   h.generation_retired=true; authority_.reset(); candidate_.reset(); return "observation-conflict";
  }
 }
 h.observation=m;
 candidate_=TrustedHmdSourceObservationSnapshot{m,at}; return "pose";
}
std::string TrustedHmdSourceStream::apply(const w::TrustedHmdSourceUnavailable& m) {
 if(!authority_ || sourceFacts(m.source_space)!=sourceFacts(authority_->source_space)) return "untrusted";
 candidate_.reset(); return "unavailable";
}
std::string TrustedHmdSourceStream::apply(const w::TrustedHmdSourceRevocation& m) {
 if(!authority_ || sourceFacts(m.source_space)!=sourceFacts(authority_->source_space)) return "untrusted";
 auto& h=sources_.at(sourceKey(m.source_space));
 h.generation_retired=true;
 if(m.reason=="source_session_loss" || m.reason=="event_stream_lost") h.token_retired=true;
 authority_.reset(); candidate_.reset(); return "revoked";
}
}
