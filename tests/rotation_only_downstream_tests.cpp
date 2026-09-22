#include "helpers.hpp"
#include <iostream>

// Common Observation semantics only. Synthetic poses are not a raw-status/HIL replay.
int main() try {
 auto c=config();c.bindings.clear();c.profiles.clear();c.bridgeId="monaka-bridge-local-1";
 mb::Profile profile;profile.convention="synthetic-native";profile.approved=true;
 profile.evidence="synthetic downstream regression, not hardware approval";
 profile.positionAxes={3,2,-1};profile.quaternionAxes={3,2,-1,4};c.profiles["synthetic"]=profile;
 mb::Binding binding;binding.source="vive-local-1";binding.device="23:34:e4:5a:fe:39";
 binding.tracker="altra-0";binding.profile="synthetic";binding.inputSpace="native";
 binding.inputRevision=1;binding.spaceApproved=true;binding.worldSpace="monaka-world-local";
 const auto root=std::sqrt(.5);
 binding.world.rotation={0,0,root,root};binding.world.translation={10,20,30};
 binding.mount.rotation={root,0,0,root};binding.mount.translation={.2,0,0};
 const mb::Key key{binding.source,binding.device};c.bindings[key]=binding;
 mb::Bridge bridge(c,SB);
 auto input=sample(binding.source);input.device_id=binding.device;
 input.coordinate_space.convention=profile.convention;
 input.orientation=mb::Quat{0,0,0,1};
 // Invalid retained numeric position/derivatives must not escape ROTATION_ONLY/NONE.
 input.linear_velocity=mb::c1::Derivative{{2,3,4},"space","measured"};
 input.linear_acceleration=mb::c1::Derivative{{5,6,7},"space","measured"};
 input.capabilities.push_back("linear_velocity");input.capabilities.push_back("linear_acceleration");
 std::int64_t now=1000000000,outputSequence=-1;
 std::optional<mb::Quat> previous;
 auto accept=[&](bool continuous){
  CHECK(bridge.receive(wire(input),"same-backend",now));
  const auto out=*bridge.logical().at(key).pose;
  CHECK(out.sequence==++outputSequence&&out.publisher_id==c.bridgeId);
  CHECK(out.source_id==binding.source&&out.tracker_id==binding.tracker&&out.session_id==SB);
  CHECK(out.input.source_id==input.source_id&&out.input.device_id==input.device_id);
  CHECK(out.input.session_id==input.session_id&&out.input.sequence==input.sequence);
  CHECK(out.input.orientation_evidence==input.orientation_evidence);
  CHECK(out.timestamp_ns==now-1000000&&out.mapping_revision==c.revision);
  CHECK(out.modality==input.modality);
  if(out.orientation){
   auto expected=mb::mul(mb::mul(binding.world.rotation,mb::permute(*input.orientation,profile.quaternionAxes)),binding.mount.rotation);
   double equivalence=0;for(int i=0;i<4;++i)equivalence+=expected[i]*(*out.orientation)[i];
   CHECK(near(std::abs(equivalence),1));
   if(continuous){CHECK(previous);double dot=0;for(int i=0;i<4;++i)dot+=(*previous)[i]*(*out.orientation)[i];CHECK(dot>.999);}
   previous=out.orientation;
  }
  // Inspect the actual fixed-codec serialized fanout, not just the logical cache.
  int monaka=0,direct=0;
  bridge.fanout.flush([&](auto channel,std::string_view bytes){
   mb::c1::Envelope e;mb::c1::Error error;
   CHECK(mb::c1::DecodeEnvelope(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),e,error));
   if(auto pose=std::get_if<mb::c1::MtpPose>(&e)){
    CHECK(wire(*pose)==wire(out));
    if(channel==mb::Channel::Monaka)++monaka;if(channel==mb::Channel::Steamvr)++direct;
   }
   return true;
  },now);
  CHECK(monaka==1&&direct==1);
  return out;
 };
 auto advance=[&]{++input.sequence;input.timestamp_ns+=10000000;input.sent_at_ns+=10000000;now+=10000000;};
 auto full=accept(false);CHECK(full.position&&full.orientation&&full.tracking_state=="tracked");
 CHECK(!full.angular_velocity);
 advance();input.modality="rotation_only";input.validity.position=false;input.tracking_state="degraded";
 input.orientation=mb::Quat{-std::sin(.01),0,0,-std::cos(.01)};
 auto rotation=accept(true);
 CHECK(!rotation.position&&rotation.orientation&&!rotation.validity.position&&rotation.validity.orientation);
 CHECK(rotation.confidence.position==0&&rotation.confidence.orientation>0&&rotation.tracking_state=="degraded");
 CHECK(!rotation.linear_velocity&&!rotation.linear_acceleration);
 CHECK(rotation.angular_velocity&&near(mb::norm(*rotation.angular_velocity),2)); // History survived FULL -> RO.
 const auto fixed=bridge.registry.find(key)->fixedTime;
 CHECK(!bridge.receive(wire(input),"same-backend",now+1));
 auto old=input;--old.sequence;CHECK(!bridge.receive(wire(old),"same-backend",now+2));
 auto metadata=state(input);metadata.modality="rotation_only";metadata.tracking_state="degraded";
 CHECK(bridge.receive(wire(metadata),"same-backend",now+3));
 CHECK(bridge.registry.find(key)->fixedTime==fixed&&bridge.logical().at(key).pose->sequence==rotation.sequence);
 // Fresh RO traffic keeps orientation alive beyond the original FULL timeout.
 for(int i=0;i<60;++i){advance();input.orientation=mb::Quat{std::sin(.01),0,0,std::cos(.01)};rotation=accept(true);CHECK(!rotation.position&&rotation.angular_velocity);}
 advance();input.modality="full";input.validity.position=true;input.tracking_state="tracked";
 full=accept(true);CHECK(full.position&&full.orientation&&full.angular_velocity);
 // No NONE is inserted between any of the admitted transition samples.
 advance();input.modality="none";input.validity={false,false};input.orientation_evidence="none";input.tracking_state="lost";
 auto none=accept(false);CHECK(!none.position&&!none.orientation&&!none.angular_velocity);
 CHECK(none.confidence.position==0&&none.confidence.orientation==0&&!bridge.logical().at(key).history.q);
 advance();input.modality="rotation_only";input.validity.orientation=true;input.orientation_evidence="device";input.tracking_state="degraded";
 rotation=accept(false);CHECK(!rotation.position&&rotation.orientation&&!rotation.angular_velocity);
 const auto last=bridge.registry.find(key)->fixedTime;
 bridge.tick(last+c.timeoutNs-1);CHECK(bridge.logical().at(key).pose);
 bridge.tick(last+c.timeoutNs);CHECK(!bridge.logical().at(key).pose&&!bridge.logical().at(key).history.q);
 // Session takeover after lease expiry resets history, preserves logical identity, rejects retired traffic.
 auto retired=input;now+=c.timeoutNs;input.session_id=input.clock_id=S2;input.sequence=0;
 rotation=accept(false);CHECK(!rotation.position&&rotation.orientation&&!rotation.angular_velocity);
 CHECK(!bridge.receive(wire(retired),"same-backend",now+1));
 std::cout<<"PASS Common Observation -> calibrated serialized MTP FULL/ROTATION_ONLY/FULL/NONE, identity, history, age and sessions\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
