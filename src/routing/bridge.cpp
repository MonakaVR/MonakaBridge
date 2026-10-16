#include "monaka_bridge/bridge.hpp"
#include <limits>
#include <algorithm>
namespace mb {
namespace {
bool contains(const std::vector<std::string>& values,const std::string& value){return std::find(values.begin(),values.end(),value)!=values.end();}
std::vector<std::string> outputCapabilities(const Device* device,const CapabilityProfile& contract,const Binding& binding){
 if(!device||!device->hasCapabilities)return {};
 const auto& source=device->capabilities;
 const bool orientation=contains(source,"orientation");
 const bool offset=norm(binding.mount.translation)>0;
 std::vector<std::string> result;
 for(const auto& capability:contract.outputDeclaration){
  bool supported=false;
  if(capability=="position"||capability=="orientation")supported=contains(source,capability);
  else if(capability=="linear_velocity")supported=contains(source,"position")&&contains(source,capability)&&(!offset||orientation);
  // Angular velocity may be converted from an admitted orientation stream
  // when no native angular-velocity channel is declared.
  else if(capability=="angular_velocity")supported=orientation;
  // The current mount transform omits angular/centripetal acceleration terms.
  else if(capability=="linear_acceleration")supported=contains(source,"position")&&contains(source,capability)&&!offset;
  else if(capability=="battery_fraction"||capability=="charging")supported=contains(source,capability);
  if(supported)result.push_back(capability);
 }
 return result;
}
std::string queueKey(const c1::Envelope& e){return std::visit([](const auto& v)->std::string{
 using T=std::decay_t<decltype(v)>;std::string id;
 if constexpr(std::is_same_v<T,c1::TrackerObservation>||std::is_same_v<T,c1::ObservationDeviceState>||
              std::is_same_v<T,c1::MtpPose>||std::is_same_v<T,c1::MtpTrackerState>){
 if constexpr(std::is_same_v<T,c1::TrackerObservation>||std::is_same_v<T,c1::ObservationDeviceState>)id=v.device_id;
 else id=v.tracker_id;
 return std::to_string(v.source_id.size())+":"+v.source_id+id+":"+std::to_string(c1::Envelope(v).index());
 } else return {}; // Authority messages never enter the existing latest-only MTP/mirror fanout.
 },e);}
template<class T>void header(T& out,const Config& c,const std::string& session,const Binding& b,std::int64_t seq,std::int64_t timestamp,std::int64_t now){out.version={2,0};out.modality="none";out.source_id=b.source;out.publisher_id=c.bridgeId;out.session_id=out.clock_id=session;out.tracker_id=b.tracker;out.sequence=seq;out.timestamp_ns=timestamp;out.sent_at_ns=now;out.timestamp_kind="receive";out.coordinate_space={b.worldSpace,"rh_y_up_neg_z_forward",b.worldRevision};out.mapping_revision=c.revision;}
}
void Fanout::put(Channel c,c1::Envelope e,std::int64_t now){
 auto key=queueKey(e);if(key.empty()){++errors;return;}auto& q=queues_[c];
 if(auto state=std::get_if<c1::MtpTrackerState>(&e);state && (state->presence=="absent"||state->tracking_state=="disconnected"||state->tracking_state=="lost")){
  c1::MtpPose p;p.source_id=state->source_id;p.tracker_id=state->tracker_id;auto stale=queueKey(p);q.latest.erase(stale);q.order.erase(std::remove(q.order.begin(),q.order.end(),stale),q.order.end());
 }
 if(!q.latest.count(key)){if(q.latest.size()>=capacity){++dropped;return;}q.order.push_back(key);}q.latest.insert_or_assign(key,Item{std::move(e),now});}
void Fanout::clear(Channel c){queues_[c]=Queue{};}
std::size_t Fanout::pending(Channel c)const{auto i=queues_.find(c);return i==queues_.end()?0:i->second.latest.size();}
void Fanout::flush(const Send& send,std::int64_t now,std::size_t budget){
 for(auto& [channel,q]:queues_){auto remaining=budget;while(remaining--&&!q.order.empty()){
  auto key=q.order.front();q.order.pop_front();auto item=std::move(q.latest.at(key));q.latest.erase(key);
  try{
   bool valid=now>=item.queuedAt;
   std::visit([&](auto& v){if(channel==Channel::Mirror){auto delay=now-item.queuedAt;if(delay>std::numeric_limits<std::int64_t>::max()-v.sent_at_ns)valid=false;else v.sent_at_ns+=delay;}else v.sent_at_ns=now;},item.value);
   std::string bytes;c1::Error error;if(!valid||!c1::EncodeEnvelope(item.value,bytes,error)||!send(channel,bytes))++errors;else ++sent;
  }catch(...){++errors;}
 }}
}
Bridge::Bridge(Config c,std::string session):config_(std::move(c)),session_(std::move(session)),normalization_(config_,session_){registry.sourceAuthorized=[this](const c1::Envelope& e){return normalization_.provisioned(e);};registry.normalize=[this](const c1::Envelope& e){return normalization_.input(e);};validateConfig(config_);registry.timeoutNs=config_.timeoutNs;for(auto& [key,b]:config_.bindings)trackerOwners_.emplace(Key{b.source,b.tracker},key);}
bool Bridge::approved(const Binding& b,const Device& d)const{auto p=config_.profiles.find(b.profile);return p!=config_.profiles.end()&&p->second.approved&&!p->second.evidence.empty()&&b.spaceApproved&&p->second.convention==d.convention&&b.inputSpace==d.space&&b.inputRevision==d.revision;}
c1::Envelope Bridge::publish(const c1::Envelope& input,std::int64_t now){const auto e=normalization_.output(input,true);if(config_.policy==Policy::Monaka||config_.policy==Policy::Both)fanout.put(Channel::Monaka,e,now);if(config_.policy==Policy::Steamvr||config_.policy==Policy::Both)fanout.put(Channel::Steamvr,e,now);return e;}
bool Bridge::receive(std::string_view bytes,std::string_view peer,std::int64_t now){c1::Envelope e;bool ok=registry.receive(bytes,peer,now,&e);if(ok)fanout.put(Channel::Mirror,std::move(e),now);tick(now);return ok;}
void Bridge::tick(std::int64_t now){
 for(const auto& [key,b]:config_.bindings){
  if(!config_.contracts.count(key))continue; // Fail closed; configuration tools never infer support.
  auto& l=logical_[key];auto d=registry.find(key);bool active=d&&approved(b,*d);bool fresh=active&&registry.fresh(key,now);
  const auto src=registry.sources.find(key.first);const std::string inputSession=src==registry.sources.end()?"":src->second.session;
  if(l.inputSession!=inputSession){l.history=History{};l.pose.reset();l.capabilities.clear();l.inputSequence=-1;l.inputSession=inputSession;l.stateSignature.clear();}
  const auto& contract=config_.contracts.at(key);
  if(d&&d->hasCapabilities)l.capabilities=outputCapabilities(d,contract,b);else l.capabilities.clear();
  if(fresh && d->pose && (l.inputSequence!=d->poseSequence || !l.pose)){
   const auto& in=*d->pose;
   if(l.poseSequence==std::numeric_limits<std::int64_t>::max())throw std::runtime_error("MTP sequence exhausted: restart Bridge");
   auto v=calibrate(in,b,config_.profiles.at(b.profile),l.history,d->fixedTime);
   c1::MtpPose out;header(out,config_,session_,b,l.poseSequence++,d->fixedTime,now);
   out.position=contains(l.capabilities,"position")?v.position:std::nullopt;
   out.orientation=contains(l.capabilities,"orientation")?v.orientation:std::nullopt;
   out.linear_velocity=contains(l.capabilities,"linear_velocity")?v.velocity:std::nullopt;
   out.angular_velocity=contains(l.capabilities,"angular_velocity")?v.omega:std::nullopt;
   out.linear_acceleration=contains(l.capabilities,"linear_acceleration")?v.acceleration:std::nullopt;
   out.validity={bool(out.position),bool(out.orientation)};out.confidence={out.validity.position?1.0:0.0,out.validity.orientation?(in.orientation_evidence=="device"?1.0:0.5):0.0};
   out.tracking_state=out.validity.position&&out.validity.orientation?"tracked":out.validity.position||out.validity.orientation?"degraded":in.tracking_state=="initializing"||in.tracking_state=="unknown"?in.tracking_state:"lost";
   out.capabilities=l.capabilities;
   out.modality=out.validity.position&&out.validity.orientation?"full":out.validity.orientation?"rotation_only":"none";
   if(out.modality=="none"){out.position.reset();out.validity.position=false;out.confidence.position=0;out.tracking_state="lost";}
   out.input={in.source_id,in.device_id,in.session_id,in.sequence,in.orientation_evidence};l.inputSequence=d->poseSequence;l.pose=std::get<c1::MtpPose>(publish(out,now));
  }
  if(!fresh){l.history=History{};l.pose.reset();}
  // Metadata is independent of pose sequence/freshness. Publish transitions, not pose heartbeats.
  const std::string capabilitySignature=[&]{std::string value;for(const auto& token:l.capabilities)value+=std::to_string(token.size())+":"+token;return value;}();
  const std::string poseState=l.pose?":"+l.pose->modality+":"+l.pose->tracking_state+":"+(l.pose->validity.position?"p1":"p0")+":"+(l.pose->validity.orientation?"q1":"q0"): ":no-pose";
  const std::string signature=(fresh?"fresh":"stale")+std::string(active?":approved:":":blocked:")+(d?std::to_string(d->stateSequence):"none")+":"+inputSession+":"+capabilitySignature+poseState;
  if(signature!=l.stateSignature){
   if(l.stateSequence==std::numeric_limits<std::int64_t>::max())throw std::runtime_error("MTP state sequence exhausted");
   c1::MtpTrackerState out;header(out,config_,session_,b,l.stateSequence++,now,now);
   out.presence=fresh?"present":d&&d->absent?"absent":"unknown";out.tracking_state=fresh&&l.pose?l.pose->tracking_state:"disconnected";
   out.modality=fresh&&l.pose?l.pose->modality:"none";
   out.capabilities=l.capabilities;
   if(active&&d&&d->state&&d->state->battery){auto battery=*d->state->battery;auto age=d->state->sent_at_ns-battery.timestamp_ns;if(age>=0&&age<=d->stateAt){battery.timestamp_ns=d->stateAt-age;if(!contains(out.capabilities,"battery_fraction"))battery.fraction.reset();if(!contains(out.capabilities,"charging"))battery.charging.reset();if(battery.fraction||battery.charging)out.battery=battery;}}
   publish(out,now);l.stateSignature=signature;
  }
  l.wasFresh=fresh;
 }
}
void Bridge::reconfigure(Config c,std::int64_t now){
 validateConfig(c);normalization_.validateReconfigure(c);if(c.bridgeId!=config_.bridgeId)throw std::invalid_argument("bridge ID change requires restart");
 if(c.revision<=config_.revision)throw std::invalid_argument("config update must increase mapping_revision");
 auto owners=trackerOwners_;std::set<Key> lifetimeKeys;for(auto& [key,l]:logical_)lifetimeKeys.insert(key);
 for(auto& [key,b]:c.bindings){auto old=owners.find(Key{b.source,b.tracker});if(old!=owners.end()&&old->second!=key)throw std::invalid_argument("tracker reassignment requires Bridge restart");owners.emplace(Key{b.source,b.tracker},key);lifetimeKeys.insert(key);}
 if(owners.size()>256||lifetimeKeys.size()>256)throw std::invalid_argument("lifetime identity bound reached; coordinate Bridge and SteamVR restart");
 trackerOwners_=std::move(owners);
 fanout.clear(Channel::Steamvr);fanout.clear(Channel::Monaka);
 // Invalidate old logical IDs before replacement; no stale transform is emitted afterward.
 for(auto& [key,l]:logical_){const auto old=config_.bindings.find(key);if(old!=config_.bindings.end()&&(!c.bindings.count(key)||c.bindings.at(key).tracker!=old->second.tracker)){c1::MtpTrackerState s;header(s,config_,session_,old->second,l.stateSequence++,now,now);s.presence="absent";s.tracking_state="disconnected";s.capabilities=l.capabilities;publish(s,now);}l.history=History{};l.pose.reset();l.stateSignature.clear();}
 normalization_.reconfigure(c);config_=std::move(c);registry.timeoutNs=config_.timeoutNs;
 tick(now);
}
}
