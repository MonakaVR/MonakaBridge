#include "helpers.hpp"
#include "monaka_bridge/direct.hpp"
#include "monaka_bridge/health.hpp"
#include "monaka_bridge/world_calibration.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

int main(int argc,char** argv)try{
 CHECK(argc==2);const auto root=std::filesystem::path(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directories(root);
 auto c=config();c.profiles.at("pico").approved=false;c.profiles.at("pico").evidence.clear();
 c.profiles.at("pico").positionAxes={1,2,3};c.profiles.at("pico").quaternionAxes={1,2,3,4};
 for(auto& [key,b]:c.bindings){
  b.spaceApproved=false;
  auto& contract=c.contracts.at(key);contract.inputDeclaration={"position","orientation"};
  contract.outputDeclaration={"position","orientation","angular_velocity"};
  contract.inputMode=contract.outputMode=monaka::bridge_input::Mode::STRICT;
 }
 mb::validateConfig(c);
 auto input=sample();input.orientation=mb::Quat{0,0,0,1};
 mb::Bridge uncalibrated(c,SB);CHECK(uncalibrated.receive(wire(input),"peer",10000000));
 CHECK(uncalibrated.logical().at({"pico","device"}).pose->position==input.position);
 auto& binding=c.bindings.at({"pico","device"});binding.world.translation={10,20,30};binding.mount.translation={1,0,0};
 mb::saveConfig(root/"config.json",c);const auto saved=mb::readConfigBytes(root/"config.json");
 auto loaded=mb::loadConfig(root/"config.json");mb::Bridge bridge(loaded,SB);
 auto absent=nlohmann::json::parse(saved);for(auto& row:absent["mappings"]){row.erase("world");row.erase("mount");row.erase("space_approved");}
 for(auto& profile:absent["profiles"].items()){profile.value().erase("approved");profile.value().erase("evidence");profile.value().erase("angular_space_verified");}
 absent["unknown_extension"]={{"keep",nlohmann::json::array({1,2,3})}};
 const auto absentBytes=absent.dump();{std::ofstream file(root/"uncalibrated.json",std::ios::binary);file<<absentBytes;}
 auto absentLoaded=mb::loadConfig(root/"uncalibrated.json");auto absentTarget=mb::selectWorldCalibrationTarget(absentLoaded,"pico","device","logical-pico","native",1);
 auto unchanged=mb::worldCalibrationCandidate(absentLoaded,absentTarget,mb::Rigid{},true);mb::saveConfig(root/"uncalibrated.json",unchanged);
 CHECK(mb::readConfigBytes(root/"uncalibrated.json")==absentBytes&&unchanged.revision==absentLoaded.revision);
 bool conflict=false;auto extensionChanged=absent;extensionChanged["unknown_extension"]["keep"][0]=99;
 try{mb::commitConfigDocument(root/"uncalibrated.json",absentBytes,extensionChanged.dump());}catch(...){conflict=true;}CHECK(conflict&&mb::readConfigBytes(root/"uncalibrated.json")==absentBytes);
 conflict=false;try{mb::commitConfigDocument(root/"uncalibrated.json",absentBytes+" ",absentBytes);}catch(...){conflict=true;}CHECK(conflict);
 auto unmeasured=mb::parseConfigDocument(absent.dump());mb::Bridge withoutCalibration(unmeasured,SB);
 CHECK(withoutCalibration.receive(wire(input),"uncalibrated-peer",10000000));
 CHECK(withoutCalibration.logical().at({"pico","device"}).pose->position==input.position);
 mb::Bridge newMap(loaded,SB);CHECK(newMap.receive(wire(input),"peer",10000000));
 auto newFrame=input;newFrame.sequence=1;newFrame.coordinate_space.id="fresh-map-id";newFrame.coordinate_space.revision=0;
 CHECK(newMap.receive(wire(newFrame),"peer",20000000));CHECK(newMap.logical().at({"pico","device"}).pose->position==std::optional<mb::Vec>({12,22,33}));
 CHECK(!bridge.logical().count({"pico","device"}));CHECK(bridge.receive(wire(input),"peer",10000000));
 auto out=*bridge.logical().at({"pico","device"}).pose;CHECK(out.position==std::optional<mb::Vec>({12,22,33}));
 std::string monaka,direct;mb::MtpFeed feed;
 bridge.fanout.flush([&](mb::Channel channel,std::string_view bytes){
  if(channel==mb::Channel::Mirror)return true;
  mb::c1::Envelope e;mb::c1::Error error;CHECK(mb::c1::DecodeEnvelope(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),e,error));
  if(std::get_if<mb::c1::MtpPose>(&e)){if(channel==mb::Channel::Monaka)monaka=std::string(bytes);else direct=std::string(bytes);}
  if(channel==mb::Channel::Steamvr)CHECK(feed.receive(bytes,11000000));return true;
 },11000000);
 CHECK(!monaka.empty()&&monaka==direct);CHECK(feed.devices.size()==1);CHECK(feed.fresh(feed.devices.begin()->second,11000000));
 auto changed=input;changed.sequence=1;changed.coordinate_space={"new-map","fixture-native",22};
 changed.timestamp_ns+=10000000;changed.sent_at_ns+=10000000;changed.orientation=mb::Quat{0,0.1,0,std::sqrt(.99)};
 CHECK(bridge.receive(wire(changed),"peer",20000000));CHECK(bridge.logical().at({"pico","device"}).pose);
 CHECK(!bridge.logical().at({"pico","device"}).pose->angular_velocity);
 CHECK(bridge.config().bindings.at({"pico","device"}).world.translation==binding.world.translation);
 CHECK(bridge.receive(wire(sample("other")),"other-peer",20000000));
 auto wrong=changed;wrong.sequence=2;wrong.coordinate_space.convention="unknown-axis";wrong.coordinate_space.revision=23;
 CHECK(bridge.receive(wire(wrong),"peer",30000000));CHECK(!bridge.logical().at({"pico","device"}).pose);CHECK(bridge.logical().at({"other","device"}).pose);
 auto rotation=changed;rotation.sequence=3;rotation.coordinate_space.revision=24;rotation.modality="rotation_only";rotation.position.reset();rotation.validity.position=false;rotation.tracking_state="degraded";
 CHECK(bridge.receive(wire(rotation),"peer",40000000));CHECK(!bridge.logical().at({"pico","device"}).pose->position);CHECK(bridge.logical().at({"pico","device"}).pose->orientation);
 auto lost=rotation;lost.sequence=4;lost.modality="none";lost.orientation.reset();lost.validity.orientation=false;lost.orientation_evidence="none";lost.tracking_state="lost";
 CHECK(bridge.receive(wire(lost),"peer",50000000));CHECK(bridge.logical().at({"pico","device"}).pose->modality=="none");
 auto recovered=changed;recovered.sequence=5;recovered.coordinate_space.revision=24;CHECK(bridge.receive(wire(recovered),"peer",60000000));CHECK(bridge.logical().at({"pico","device"}).pose->position);
 bridge.tick(700000000);CHECK(!bridge.logical().at({"pico","device"}).pose);
 recovered.session_id=recovered.clock_id=S2;recovered.sequence=0;recovered.coordinate_space.revision=0;
 CHECK(bridge.receive(wire(recovered),"peer",710000000));CHECK(bridge.logical().at({"pico","device"}).pose);CHECK(!bridge.logical().at({"pico","device"}).pose->angular_velocity);
 CHECK(!bridge.receive(wire(input),"peer",720000000));CHECK(!bridge.receive(wire(recovered),"peer",720000000));
 mb::Bridge restarted(mb::loadConfig(root/"config.json"),S2);CHECK(restarted.receive(wire(recovered),"fresh-peer",10000000));CHECK(restarted.logical().at({"pico","device"}).pose->position==bridge.logical().at({"pico","device"}).pose->position);
 CHECK(mb::readConfigBytes(root/"config.json")==saved);
 auto target=mb::selectWorldCalibrationTarget(loaded,"pico","device","logical-pico","native",1);
 auto same=mb::worldCalibrationCandidate(loaded,target,binding.world,true);CHECK(same.revision==loaded.revision);
 auto equivalent=binding.world;for(auto& v:equivalent.rotation)v=-v;CHECK(mb::worldCalibrationCandidate(loaded,target,equivalent,true).revision==loaded.revision);
 mb::commitConfigDocument(root/"config.json",saved,saved);CHECK(mb::readConfigBytes(root/"config.json")==saved);
 auto updated=binding.world;updated.translation[0]=50;auto next=mb::worldCalibrationCandidate(loaded,target,updated,true);CHECK(next.revision==loaded.revision+1);
 CHECK(next.bindings.at({"other","device"}).world.translation==loaded.bindings.at({"other","device"}).world.translation);
 auto invalid=input;invalid.orientation=mb::Quat{0,0,0,0};bool rejected=false;try{wire(invalid);}catch(...){rejected=true;}CHECK(rejected);
 auto bad=c;bad.profiles.at("pico").positionAxes={1,1,3};rejected=false;try{mb::validateConfig(bad);}catch(...){rejected=true;}CHECK(rejected);
 std::cout<<"PASS P0 ordinary receive -> saved independent W/M -> dual fanout -> Direct feed; uncalibrated, space/revision/session reset, FULL/Q-only/NONE/recovery, same-W no-op, invalid math\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
