#include "monaka_bridge/common_world_publisher.hpp"
#include "common_world_facts.hpp"
#include <stdexcept>

namespace monaka_bridge {
namespace d = common_world_detail;
namespace w = authority_wire;
CommonWorldPublisher::CommonWorldPublisher(std::string id,std::size_t cap,std::int64_t limit)
 : publisher_(std::move(id)),capacity_(cap),limit_(limit) {
 if(!d::validId(publisher_) || !cap || limit<1) throw std::invalid_argument("valid publisher, positive capacity and bootstrap sequence space required");
}
bool CommonWorldPublisher::beginSession(const std::string& session,const std::string& clock) {
 if(exhausted_ || sessions_.count(session) || clocks_.count(clock)) return false;
 w::TrustedHmdSourceAuthority validation;
 validation.source_space={"source","token","stage",0}; validation.source_space_kind="openxr_view_in_stage";
 validation.source_time_domain_id="domain"; d::header(validation,publisher_,session,clock,0,0);
 std::string bytes; w::Error error; if(!w::EncodeEnvelope(validation,bytes,error)) return false;
 if(sessions_.size()>=capacity_) { exhausted_=true; active_=false; return false; }
 sessions_.insert(session); clocks_.insert(clock); session_=session; clock_=clock;
 lastSequence_.reset(); lastTime_.reset(); active_=true; return true;
}
std::vector<w::Envelope> CommonWorldPublisher::publish(const CommonWorldEvents& events,std::int64_t at) {
 if(events.empty() || !sessionActive()) return {};
 if(at<0 || (lastTime_ && at<*lastTime_)) { active_=false; return {}; }
 if(needsBootstrap() && (events.size()<2 || !std::holds_alternative<w::CommonWorldAuthorityPublication>(events[0]) ||
     !std::holds_alternative<w::CommonWorldMappingPublication>(events[1]))) return {};
 if(needsBootstrap()) {
  const auto& world=std::get<w::CommonWorldAuthorityPublication>(events[0]);
  const auto& map=std::get<w::CommonWorldMappingPublication>(events[1]);
  if(d::world(world.world)!=d::world(map.mapping.world) || d::source(world.anchor_source)!=d::source(map.mapping.source_space)) {
   active_=false; return {};
  }
 }
 const auto available=lastSequence_?static_cast<std::uint64_t>(limit_-*lastSequence_):static_cast<std::uint64_t>(limit_)+1;
 // Entire transitions are atomic, including world+mapping at the last two sequence slots.
 if(events.size()>available) { active_=false; return {}; }
 auto seq=lastSequence_?*lastSequence_+1:0;
 std::vector<w::Envelope> output;
 for(const auto& event:events) {
  const auto owner=std::visit([](const auto& m)->std::string {
   using T=std::decay_t<decltype(m)>;
   if constexpr(std::is_same_v<T,w::CommonWorldAuthorityPublication> || std::is_same_v<T,w::CommonWorldRevocation>)
    return m.world.owner_id;
   else return m.mapping.world.owner_id;
  },event);
  if(owner!=publisher_) { active_=false; return {}; }
  auto value=std::visit([&](auto m)->w::Envelope { d::header(m,publisher_,session_,clock_,seq,at); return m; },event);
  std::string bytes; w::Error error;
  if(!w::EncodeEnvelope(value,bytes,error)) { active_=false; return {}; }
  output.push_back(std::move(value));
  if(seq<limit_) ++seq; // never evaluate INT64_MAX+1
 }
 lastSequence_=std::visit([](const auto& m){return m.sequence;},output.back()); lastTime_=at;
 if(*lastSequence_==limit_) active_=false; // next event needs a fresh session; owner untouched
 return output;
}
}
