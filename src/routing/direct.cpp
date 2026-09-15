#include "monaka_bridge/direct.hpp"
namespace mb {
bool MtpFeed::receive(std::string_view bytes,std::int64_t now){
 c1::Envelope e;c1::Error error;if(now<0||!c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),e,error))return false;
 auto p=std::get_if<c1::MtpPose>(&e);auto s=std::get_if<c1::MtpTrackerState>(&e);if(!p&&!s)return false;
 const auto& source=p?p->source_id:s->source_id;const auto& session=p?p->session_id:s->session_id;const auto& clock=p?p->clock_id:s->clock_id;
 if(!sources_.count(source)&&sources_.size()>=64)return false;auto& src=sources_[source];
 if(src.retired.count(session))return false;
 if(src.session!=session){if(src.retired.size()>=1024)return false;if(!src.session.empty())src.retired.insert(src.session);src.session=session;src.clock=clock;for(auto& [key,v]:devices)if(key.first==source){v=DirectSample{};}}
 else if(src.clock!=clock)return false;
 Key key{source,p?p->tracker_id:s->tracker_id};if(!devices.count(key)&&devices.size()>=256)return false;auto& d=devices[key];
 auto seq=p?p->sequence:s->sequence;auto rev=p?p->mapping_revision:s->mapping_revision;
 if(rev<d.revision||(p?seq<=d.poseSequence:seq<=d.stateSequence))return false;
 if(rev>d.revision){d.pose.reset();d.fixedTime=-1;d.revision=rev;}
 if(p){d.poseSequence=seq;d.pose=*p;auto age=p->sent_at_ns-p->timestamp_ns;d.fixedTime=age<=now?now-age:-1;}
 else{d.stateSequence=seq;d.state=*s;d.absent=s->presence=="absent"||s->tracking_state=="disconnected";if(d.absent){d.pose.reset();d.fixedTime=-1;}}
 return true;
}
bool MtpFeed::fresh(const DirectSample& d,std::int64_t now)const{return d.pose&&!d.absent&&d.fixedTime>=0&&now>=d.fixedTime&&now-d.fixedTime<500000000;}
}
