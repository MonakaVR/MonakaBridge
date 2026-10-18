#include "helpers.hpp"
#include "monaka_bridge/world_calibration.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

using Json=nlohmann::json;
namespace {
std::filesystem::path output;unsigned serial=0,cases=0;
Json document(const mb::Config& c){auto file=output/(std::to_string(++serial)+".json");mb::saveConfig(file,c);std::ifstream input(file);Json value;input>>value;return value;}
bool group(const mb::Binding& b){return b.source=="pico"&&b.inputSpace=="native"&&b.inputRevision==1;}
void exactCandidate(const mb::Config& c,const mb::Config& next,const mb::Rigid& w){
 auto expected=document(c);expected["mapping_revision"]=c.revision+1;
 for(auto& row:expected["mappings"])if(row["source_id"]=="pico"&&row["input_space"]=="native"&&row["input_revision"]==1){row["world"]={{"rotation",w.rotation},{"translation",w.translation}};row["space_approved"]=false;}
 CHECK(document(next)==expected);unsigned count=0;for(const auto& [key,b]:next.bindings)if(group(b)){CHECK(!b.spaceApproved);++count;}CHECK(count==4);
}
template<class F>void reject(F f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}CHECK(failed);++cases;}
}
int main(int argc,char** argv)try{
 CHECK(argc==3);output=std::filesystem::path(argv[2])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directories(output);
 const auto c=mb::loadConfig(argv[1]);const auto original=document(c);
 const auto target=mb::selectWorldCalibrationTarget(c,"pico","device-1","logical-pico-device-1","native",1);
 const std::vector<mb::Rigid> changes={{{0,0,0,1},{2,3,4}},{{0,std::sin(.35),0,std::cos(.35)},{1,0,0}},mb::Rigid{},target.binding.world};
 for(const auto& world:changes){auto candidate=mb::worldCalibrationCandidate(c,target,world,true);exactCandidate(c,candidate,world);CHECK(document(c)==original);++cases;}
 auto measured=mb::worldCalibrationCandidate(c,target,changes[0],false);CHECK(document(measured)==original&&measured.sourceDocument==c.sourceDocument);++cases;
 auto invalid=changes[0];invalid.translation[0]=std::numeric_limits<double>::quiet_NaN();reject([&]{mb::worldCalibrationCandidate(c,target,invalid,true);});
 invalid=changes[0];invalid.rotation[0]=std::numeric_limits<double>::infinity();reject([&]{mb::worldCalibrationCandidate(c,target,invalid,true);});
 invalid=changes[0];invalid.rotation={0,0,0,0};reject([&]{mb::worldCalibrationCandidate(c,target,invalid,true);});
 auto overflow=c;overflow.revision=UINT32_MAX;auto overflowTarget=mb::selectWorldCalibrationTarget(overflow,"pico","device-1","logical-pico-device-1","native",1);reject([&]{mb::worldCalibrationCandidate(overflow,overflowTarget,changes[0],true);});CHECK(overflow.bindings.at({"pico","device-1"}).spaceApproved);
 auto stale=c;++stale.revision;reject([&]{mb::worldCalibrationCandidate(stale,target,changes[0],true);});
 auto changed=c;changed.profiles.at("synthetic").evidence="changed";reject([&]{mb::worldCalibrationCandidate(changed,target,changes[0],true);});CHECK(document(c)==original);
 // A saved candidate has no effect until this owned in-memory runtime reloads it.
 mb::Bridge runtime(c,SB);const std::int64_t now=1000000000;
 for(int i=1;i<=4;++i){auto p=sample();p.device_id="device-"+std::to_string(i);p.orientation=mb::Quat{0,0,0,1};CHECK(runtime.receive(wire(p),"fixture-peer",now+i));CHECK(runtime.logical().at({"pico",p.device_id}).pose.has_value());}
 const auto candidate=mb::worldCalibrationCandidate(c,target,changes[0],true);
 CHECK(runtime.config().bindings.at({"pico","device-1"}).spaceApproved);CHECK(runtime.logical().at({"pico","device-1"}).pose.has_value());
 runtime.reconfigure(candidate,now+10000);
 for(int i=1;i<=4;++i){mb::Key key{"pico","device-"+std::to_string(i)};CHECK(!runtime.config().bindings.at(key).spaceApproved);CHECK(!runtime.logical().at(key).pose.has_value());}
 ++cases;
 std::cout<<"PASS world approval native "<<cases<<" groups: four-member translation/rotation/clear/same-W, exact other groups/extensions, measure-only, invalid/stale/overflow, SAVED vs runtime reload; no sockets/SDK/HIL\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
