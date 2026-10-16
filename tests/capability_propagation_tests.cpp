#include "helpers.hpp"
#include <algorithm>
#include <iostream>

struct Captured {
 std::vector<mb::c1::MtpPose> poses;
 std::vector<mb::c1::MtpTrackerState> states;
};
Captured flushMonaka(mb::Bridge& bridge,std::int64_t now){
 Captured captured;
 bridge.fanout.flush([&](mb::Channel channel,std::string_view bytes){
  if(channel!=mb::Channel::Monaka)return true;
  mb::c1::Envelope envelope;mb::c1::Error error;
  CHECK(mb::c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),envelope,error));
  if(auto pose=std::get_if<mb::c1::MtpPose>(&envelope))captured.poses.push_back(*pose);
  if(auto state=std::get_if<mb::c1::MtpTrackerState>(&envelope))captured.states.push_back(*state);
  return true;
 },now);
 return captured;
}
mb::c1::MtpTrackerState lastPicoState(const Captured& captured){
 for(auto it=captured.states.rbegin();it!=captured.states.rend();++it)if(it->tracker_id=="logical-pico")return *it;
 throw std::runtime_error("missing logical-pico state");
}
bool has(const std::vector<std::string>& values,const std::string& value){return std::find(values.begin(),values.end(),value)!=values.end();}
mb::Config capabilityConfig(){
 auto c=config();
 for(auto& [key,contract]:c.contracts){
  contract.inputDeclaration={"position","orientation","linear_velocity","angular_velocity","linear_acceleration","battery_fraction","charging"};
  contract.outputDeclaration=contract.inputDeclaration;
  contract.inputMode=contract.outputMode=monaka::bridge_input::Mode::LEGACY_DYNAMIC;
 }
 return c;
}
int main()try{
 const mb::Key key{"pico","device"};
 // DeviceState may be the first message. Its static Q-only declaration must
 // be cached and used even though the legacy contract allows a wider set.
 {
  auto c=capabilityConfig();mb::Bridge bridge(c,SB);auto p=sample();p.capabilities={"orientation"};p.modality="rotation_only";p.position.reset();p.validity={false,true};p.tracking_state="degraded";
  auto s=state(p);s.capabilities=p.capabilities;
  CHECK(bridge.receive(wire(s),"peer",10000000));
  auto first=flushMonaka(bridge,10000001);CHECK(!first.states.empty());
  CHECK(!has(lastPicoState(first).capabilities,"position")&&has(lastPicoState(first).capabilities,"orientation"));
  CHECK(has(lastPicoState(first).capabilities,"angular_velocity")); // Bridge can derive this from Q samples.
  CHECK(lastPicoState(first).modality=="none");
  CHECK(bridge.receive(wire(p),"peer",11000000));auto second=flushMonaka(bridge,11000001);
  auto pose=bridge.logical().at(key).pose;CHECK(pose);
  CHECK(!pose->position&&pose->orientation&&!has(pose->capabilities,"position")&&has(pose->capabilities,"orientation"));
  CHECK(!pose->angular_velocity&&has(pose->capabilities,"angular_velocity"));
  CHECK(pose->modality=="rotation_only"&&!pose->validity.position&&pose->validity.orientation);
  CHECK(!has(lastPicoState(second).capabilities,"position")&&has(lastPicoState(second).capabilities,"orientation"));
  CHECK(lastPicoState(second).modality=="rotation_only");
 }
 // Static Pos+Q survives sample validity transitions, loss, stale timeout,
 // recovery, and a new session. Only modality/tracking metadata changes.
 {
  auto c=capabilityConfig();mb::Bridge bridge(c,SB);auto p=sample();
  CHECK(bridge.receive(wire(p),"peer",10000000));auto full=flushMonaka(bridge,10000001);
  CHECK(has(bridge.logical().at(key).pose->capabilities,"position")&&has(bridge.logical().at(key).pose->capabilities,"orientation"));
  CHECK(lastPicoState(full).modality=="full");
  p.sequence=1;p.modality="rotation_only";p.position.reset();p.validity={false,true};p.tracking_state="degraded";
  CHECK(bridge.receive(wire(p),"peer",12000000));auto rotated=flushMonaka(bridge,12000001);
  CHECK(has(bridge.logical().at(key).pose->capabilities,"position")&&has(bridge.logical().at(key).pose->capabilities,"orientation"));
  CHECK(!bridge.logical().at(key).pose->validity.position&&bridge.logical().at(key).pose->validity.orientation);
  CHECK(lastPicoState(rotated).modality=="rotation_only");
  p.sequence=2;p.modality="none";p.position.reset();p.orientation.reset();p.validity={false,false};p.orientation_evidence="none";p.tracking_state="lost";
  CHECK(bridge.receive(wire(p),"peer",14000000));auto lostPose=flushMonaka(bridge,14000001);
  CHECK(has(bridge.logical().at(key).pose->capabilities,"position")&&has(bridge.logical().at(key).pose->capabilities,"orientation"));
  CHECK(bridge.logical().at(key).pose->modality=="none"&&lastPicoState(lostPose).modality=="none");
  auto lost=state(p,1);lost.presence="present";lost.tracking_state="lost";
  CHECK(bridge.receive(wire(lost),"peer",15000000));auto lostState=flushMonaka(bridge,15000001);
  CHECK(!bridge.logical().at(key).pose&&has(lastPicoState(lostState).capabilities,"position")&&has(lastPicoState(lostState).capabilities,"orientation"));
  p=sample();p.sequence=3;p.timestamp_ns+=2000000;p.sent_at_ns+=2000000;
  CHECK(bridge.receive(wire(p),"peer",17000000));auto recovered=flushMonaka(bridge,17000001);
  CHECK(has(bridge.logical().at(key).pose->capabilities,"position")&&has(bridge.logical().at(key).pose->capabilities,"orientation"));
  CHECK(lastPicoState(recovered).modality=="full");
  bridge.tick(800000000);auto stale=flushMonaka(bridge,800000001);
  CHECK(!bridge.logical().at(key).pose&&has(lastPicoState(stale).capabilities,"position")&&has(lastPicoState(stale).capabilities,"orientation"));
  CHECK(lastPicoState(stale).modality=="none"&&lastPicoState(stale).tracking_state=="disconnected");
  auto reconnect=state(sample(),0);reconnect.session_id=reconnect.clock_id=S2;reconnect.capabilities={"orientation"};
  CHECK(bridge.receive(wire(reconnect),"peer",900000000));auto newSession=flushMonaka(bridge,900000001);
  CHECK(!has(lastPicoState(newSession).capabilities,"position")&&has(lastPicoState(newSession).capabilities,"orientation"));
  auto q=sample();q.session_id=q.clock_id=S2;q.sequence=0;q.capabilities={"orientation"};q.modality="rotation_only";q.position.reset();q.validity={false,true};q.tracking_state="degraded";
  CHECK(bridge.receive(wire(q),"peer",901000000));flushMonaka(bridge,901000001);
  CHECK(!has(bridge.logical().at(key).pose->capabilities,"position")&&has(bridge.logical().at(key).pose->capabilities,"orientation"));
 }
 // Pose and DeviceState declarations share one static contract. A changed
 // declaration within a session quarantines the identity instead of widening it.
 {
  auto c=capabilityConfig();mb::Bridge bridge(c,SB);auto q=sample();q.capabilities={"orientation"};q.modality="rotation_only";q.position.reset();q.validity={false,true};q.tracking_state="degraded";
  auto s=state(q);s.capabilities=q.capabilities;
  CHECK(bridge.receive(wire(s),"peer",10000000));flushMonaka(bridge,10000001);
  auto changed=sample();changed.sequence=0;
  CHECK(!bridge.receive(wire(changed),"peer",11000000));
  CHECK(!bridge.logical().at(key).pose&&bridge.logical().at(key).capabilities.empty());
 }
 // Unknown capabilities are rejected without widening the output declaration.
 {
  auto c=capabilityConfig();mb::Bridge bridge(c,SB);auto bytes=wire(sample());
  const auto caps=bytes.find("[\"position\",\"orientation\"]");CHECK(caps!=std::string::npos);
  bytes.replace(caps,26,"[\"orientation\",\"future_channel\"]");
  CHECK(!bridge.receive(bytes,"peer",10000000));
  CHECK(bridge.logical().at(key).capabilities.empty()&&!bridge.logical().at(key).pose);
 }
 std::cout<<"PASS static capability propagation: Q-only, Pos+Q validity/loss/recovery/stale/reconnect, DeviceState-first, unknown fail-closed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
