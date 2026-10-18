#include "monaka_bridge/discovery.hpp"
#include <algorithm>
namespace mb {
namespace {
struct Metadata {
 std::string source,device,session,clock,space,convention;
 std::uint32_t revision;std::int64_t sequence,timestamp,sent;bool pose;
 std::vector<std::string> capabilities;
};
std::optional<Metadata> metadata(const c1::Envelope& e){return std::visit([](const auto& m)->std::optional<Metadata>{
 using T=std::decay_t<decltype(m)>;
 if constexpr(std::is_same_v<T,c1::TrackerObservation>||std::is_same_v<T,c1::ObservationDeviceState>)
  return Metadata{m.source_id,m.device_id,m.session_id,m.clock_id,m.coordinate_space.id,m.coordinate_space.convention,m.coordinate_space.revision,m.sequence,m.timestamp_ns,m.sent_at_ns,std::is_same_v<T,c1::TrackerObservation>,m.capabilities};
 else return {};
},e);}
}
bool DiscoveryCache::receive(Bridge& bridge,std::string_view raw,std::string_view peer,std::int64_t now){
 try{c1::Envelope e;c1::Error error;if(now>=0&&raw.size()<=4096&&c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(raw.data()),raw.size(),e,error))observe(e,peer,now,bridge);}catch(...){++errors_;}
 return bridge.receive(raw,peer,now);
}
void DiscoveryCache::observe(const c1::Envelope& e,std::string_view peer,std::int64_t now,const Bridge& bridge){
 auto value=metadata(e);if(!value)return;const auto& m=*value;
 for(const auto* id:{&m.source,&m.device,&m.session,&m.clock,&m.space,&m.convention})if(id->empty()||id->size()>256){drop(now);return;}
 if(peer.size()>256||m.capabilities.size()>16){drop(now);return;}
 for(const auto& token:m.capabilities)if(token.size()>64){drop(now);return;}
 // Reuse the accepted Registry's lifetime facts without modifying it. Unknown
 // input cannot roll diagnostics back to a retired accepted source session.
 const auto active=bridge.registry.sources.find(m.source);
 if(active!=bridge.registry.sources.end()){
  const auto& s=active->second;
  if(s.retired.count(m.session)||(!s.session.empty()&&s.session!=m.session&&now-s.lastReceive<bridge.config().timeoutNs)||
     (s.session==m.session&&(s.clock!=m.clock||s.peer!=peer))){++ignored_;return;}
 }
 const Key key{m.source,m.device};
 if(bridge.config().bindings.count(key)){
  // Record only bounded canonical receipt metadata for a configured identity.
  // The accepted Registry's peer/clock/retirement and sequence facts guard it;
  // an unadmitted unknown session cannot delete or stop these liveness facts.
  const auto* device=bridge.registry.find(key);
  const bool duplicate=active!=bridge.registry.sources.end()&&active->second.session==m.session&&device&&m.sequence<=(m.pose?device->poseSequence:device->stateSequence);
  if(!duplicate){
   if(!receipts_.count(key)&&receipts_.size()>=maxDevices){for(auto i=receipts_.begin();i!=receipts_.end();)if(now>=i->second.last&&now-i->second.last>10*recentNs)i=receipts_.erase(i);else ++i;}
   if(receipts_.count(key)||receipts_.size()<maxDevices){auto& receipt=receipts_[key];
    if(receipt.session!=m.session){receipt=Receipt{};receipt.session=m.session;receipt.clock=m.clock;receipt.peer=std::string(peer);}
    auto& sequence=m.pose?receipt.poseSequence:receipt.stateSequence;
    if(receipt.clock==m.clock&&receipt.peer==peer&&m.sequence>sequence&&now>=receipt.last){sequence=m.sequence;receipt.last=now;}
   }else drop(now);
  }
 }
 if(!sources_.count(m.source)&&sources_.size()>=maxSources){drop(now);return;}
 auto& source=sources_[m.source];
 if(source.retired.count(m.session)||source.last>now){++ignored_;return;}
 if(!source.session.empty()&&source.session!=m.session){
  if(now-source.last<bridge.config().timeoutNs){++ignored_;return;}
  if(source.retired.size()>=maxRetired){drop(now);return;}
  source.retired.insert(source.session);
  for(auto i=entries_.begin();i!=entries_.end();)if(i->first.first==m.source)i=entries_.erase(i);else ++i;
 }else if(source.session==m.session&&(source.clock!=m.clock||source.peer!=peer)){++ignored_;return;}
 // Expired display entries may be reclaimed; source retired-session guards are
 // never evicted. Bounds affect diagnostics only, never the normal receive path.
 if(!entries_.count(key)&&entries_.size()>=maxDevices){
  for(auto i=entries_.begin();i!=entries_.end();)if(now>=i->second.last&&now-i->second.last>10*recentNs)i=entries_.erase(i);else ++i;
  if(entries_.size()>=maxDevices){drop(now);return;}
 }
 auto& entry=entries_[key];auto& sequence=m.pose?entry.poseSequence:entry.stateSequence;
 if(m.sequence<=sequence||(!entry.space.empty()&&(m.revision<entry.revision||(m.revision==entry.revision&&(m.space!=entry.space||m.convention!=entry.convention))))){++ignored_;return;}
 source.session=m.session;source.clock=m.clock;source.peer=std::string(peer);source.last=now;
 sequence=m.sequence;entry.session=m.session;entry.clock=m.clock;entry.space=m.space;entry.convention=m.convention;entry.revision=m.revision;entry.advertised=m.capabilities;entry.last=now;
 const auto age=m.sent-m.timestamp;entry.sample=age>=0&&age<=now?now-age:-1;
}
nlohmann::json DiscoveryCache::snapshot(const Bridge& bridge,std::int64_t now)const{
 using J=nlohmann::json;J rows=J::array(),receipts=J::array();
 for(const auto& [key,r]:receipts_)receipts.push_back({{"source",key.first},{"device",key.second},{"seen_age_ms",now>=r.last?double(now-r.last)/1000000.0:-1.0}});
 for(const auto& [key,e]:entries_){
  auto source=sources_.find(key.first);if(source==sources_.end()||source->second.session!=e.session)continue;
  const auto active=bridge.registry.sources.find(key.first);
  if(active!=bridge.registry.sources.end()&&(active->second.retired.count(e.session)||(!active->second.session.empty()&&active->second.session!=e.session&&now-active->second.lastReceive<bridge.config().timeoutNs)))continue;
  auto binding=bridge.config().bindings.find(key);bool configured=binding!=bridge.config().bindings.end();std::string reason="UNMAPPED";
  if(configured){const auto& b=binding->second;const auto profile=bridge.config().profiles.find(b.profile);reason=!bridge.config().contracts.count(key)?"CONFIGURED_CONTRACT_MISSING":profile==bridge.config().profiles.end()?"CONFIGURED_PROFILE_MISSING":b.inputSpace!=e.space?"INPUT_SPACE_MISMATCH":b.inputRevision!=e.revision?"INPUT_REVISION_MISMATCH":profile->second.convention!=e.convention?"CONVENTION_MISMATCH":"CONFIGURED";}
  const auto seen=now>=e.last?double(now-e.last)/1000000.0:-1.0;const auto sample=e.sample>=0&&now>=e.sample?double(now-e.sample)/1000000.0:-1.0;
  rows.push_back({{"source",key.first},{"device",key.second},{"session_id",e.session},{"clock_id",e.clock},{"coordinate_space",{{"id",e.space},{"convention",e.convention},{"revision",e.revision}}},
   {"advertised_capabilities",e.advertised},{"last_seen_ns",e.last},{"seen_age_ms",seen},{"sample_age_ms",sample},{"fresh",seen>=0&&seen<3000&&sample>=0&&sample<3000},
   {"configured",configured},{"reason",reason},{"tracker",configured?binding->second.tracker:""}});
 }
 return J{{"diagnostic_only",true},{"claims_are_untrusted",true},{"capacity",maxDevices},{"source_capacity",maxSources},{"recent_ms",3000},{"dropped",dropped_},{"ignored",ignored_},{"errors",errors_},
  {"truncated",lastDrop_>=0&&now>=lastDrop_&&now-lastDrop_<recentNs},{"recent_configured_receipts",receipts},{"observations",rows}};
}
}
