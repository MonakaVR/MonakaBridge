#include "monaka_bridge/registry.hpp"
namespace mb {
namespace {
bool sameSample(c1::TrackerObservation a,c1::TrackerObservation b){a.sent_at_ns=a.timestamp_ns;b.sent_at_ns=b.timestamp_ns;std::string x,y;c1::Error e;return c1::EncodeEnvelope(a,x,e)&&c1::EncodeEnvelope(b,y,e)&&x==y;}
}
bool Registry::receive(std::string_view bytes,std::string_view peer,std::int64_t now,c1::Envelope* admitted){
 c1::Envelope e;c1::Error error;
 if(now<0||!c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),e,error)){++malformed;return false;}
 auto* p=std::get_if<c1::TrackerObservation>(&e);auto* s=std::get_if<c1::ObservationDeviceState>(&e);
 if(!p&&!s){++rejected;return false;}
 const auto& id=p?p->source_id:s->source_id;const auto& session=p?p->session_id:s->session_id;
 const auto& clock=p?p->clock_id:s->clock_id;const auto& device=p?p->device_id:s->device_id;
 if(!sources.count(id)&&sources.size()>=64){++rejected;return false;}
 auto& src=sources[id];
 const auto reject=[&]{++rejected;return false;};
 if(src.collision||src.retired.count(session))return reject();
 if(!src.session.empty()&&src.peer!=peer&&now-src.lastReceive<timeoutNs){src.collision=true;src.devices.clear();++collisions;return reject();}
 if(src.session!=session){
  if(src.retired.size()>=1024)return reject();
  if(!src.session.empty())src.retired.insert(src.session);
  src.session=session;src.clock=clock;src.peer=peer;src.devices.clear();
 }else if(src.clock!=clock||src.peer!=peer){src.collision=true;src.devices.clear();++collisions;return reject();}
 if(!src.devices.count(device)&&src.devices.size()>=256)return reject();
 auto& d=src.devices[device];
 const auto& space=p?p->coordinate_space:s->coordinate_space;
 const auto seq=p?p->sequence:s->sequence;
 if((p&&seq<=d.poseSequence)||(s&&seq<=d.stateSequence)){
  if(p&&seq==d.poseSequence&&d.pose&&!sameSample(*p,*d.pose)){src.collision=true;src.devices.clear();++collisions;}
  return reject();
 }
 if(d.hasSpace && (space.revision<d.revision || (space.revision==d.revision&&(space.id!=d.space||space.convention!=d.convention))))return reject();
 if(!d.hasSpace||space.revision!=d.revision){
  // Sequence high-water marks stay monotonic within the source session.
  d.pose.reset();d.state.reset();d.fixedTime=-1;d.absent=false;
  d.space=space.id;d.convention=space.convention;d.revision=space.revision;d.hasSpace=true;
 }
 if(p){
  d.poseSequence=seq;
  if(d.absent)return reject();
  d.pose=*p;const auto age=p->sent_at_ns-p->timestamp_ns;d.fixedTime=age<=now?now-age:-1;
 }else{
  d.stateSequence=seq;d.state=*s;d.stateAt=now;
  if(s->presence=="absent"){d.absent=true;d.pose.reset();d.fixedTime=-1;}
  else if(s->presence=="present")d.absent=false;
 }
 src.lastReceive=now;
 if(admitted)*admitted=std::move(e);return true;
}
const Device* Registry::find(const Key& k)const{auto s=sources.find(k.first);if(s==sources.end()||s->second.collision)return nullptr;auto d=s->second.devices.find(k.second);return d==s->second.devices.end()?nullptr:&d->second;}
bool Registry::fresh(const Key& k,std::int64_t now)const{auto d=find(k);return d&&d->pose&&!d->absent&&d->fixedTime>=0&&now>=d->fixedTime&&now-d->fixedTime<timeoutNs;}
}
