#include "helpers.hpp"
#include "monaka_bridge/network.hpp"
#include "monaka_bridge/direct.hpp"
#include <fstream>
#include <iostream>
#include <limits>
int main()try{
 auto p=sample();auto c=config();mb::Registry r;
 CHECK(r.receive(wire(p),"a",10000000));CHECK(r.fresh({"pico","device"},10000001));CHECK(r.find({"pico","device"})->fixedTime==9000000);
 CHECK(!r.receive(wire(p),"a",20000000));auto other=sample("other");other.timestamp_ns=10;other.sent_at_ns=20;CHECK(r.receive(wire(other),"b",10000000));
 auto s=state(p);CHECK(r.receive(wire(s),"a",400000000));CHECK(!r.fresh({"pico","device"},600000000));
 p.session_id=p.clock_id=S2;p.sequence=0;CHECK(r.receive(wire(p),"a",910000000));CHECK(!r.receive(wire(sample()),"a",920000000));CHECK(r.find({"other","device"}));
 CHECK(!r.receive(wire(p),"imposter",920000000));CHECK(r.collisions==1&&r.find({"pico","device"}));
 mb::Registry duplicate;p=sample();CHECK(duplicate.receive(wire(p),"a",10000000));p.position=mb::Vec{4,5,6};CHECK(!duplicate.receive(wire(p),"a",10000001));CHECK(duplicate.collisions==1);
 mb::Registry age;p=sample();CHECK(age.receive(wire(p),"a",10));CHECK(!age.fresh({"pico","device"},20));
 CHECK(!age.receive("bad","a",30));CHECK(age.malformed==1);
 mb::History h;auto b=c.bindings.at({"pico","device"});b.world.translation={10,20,30};auto out=mb::calibrate(sample(),b,c.profiles.at("pico"),h,10000000);
 CHECK(near((*out.position)[0],11));CHECK(out.orientation==std::optional<mb::Quat>({-.5,.5,-.5,.5}));
 auto flip=sample();for(auto& x:*flip.orientation)x=-x;auto continued=mb::calibrate(flip,b,c.profiles.at("pico"),h,20000000);CHECK(continued.orientation==out.orientation);CHECK(continued.omega&&near(mb::norm(*continued.omega),0));
 CHECK(!mb::angularVelocity({0,0,0,1},{0,0,1,0},.001));CHECK(!mb::angularVelocity({0,0,0,1},{0,0,0,1},.0001));CHECK(!mb::angularVelocity({0,0,0,1},{0,0,0,1},.101));
 CHECK(mb::orientationZero({.5,.5,.5,.5},{.5,.5,.5,.5})==mb::Quat({0,0,0,1}));
 mb::Profile identity;identity.convention="fixture-native";identity.approved=true;identity.evidence="synthetic";identity.angularSpaceVerified=true;
 p=sample();p.orientation=mb::Quat{0,0,0,1};p.linear_velocity=mb::c1::Derivative{{1,0,0},"space","measured"};p.capabilities.push_back("linear_velocity");b.world.translation={0,0,0};b.mount.translation={1,0,0};h={};
 auto offset=mb::calibrate(p,b,identity,h,10000000);CHECK(offset.position&&near((*offset.position)[0],2));CHECK(!offset.velocity);
 p.angular_velocity=mb::c1::Derivative{{0,0,2},"space","measured"};p.capabilities.push_back("angular_velocity");h={};offset=mb::calibrate(p,b,identity,h,10000000);CHECK(offset.velocity&&near((*offset.velocity)[1],2));CHECK(!offset.acceleration);
 p.validity.orientation=false;p.orientation.reset();p.orientation_evidence="none";p.tracking_state="degraded";h={};CHECK(!mb::calibrate(p,b,identity,h,10000000).position);
 mb::Bridge bridge(c,SB);CHECK(bridge.receive(wire(sample()),"a",10000000));CHECK(bridge.receive(wire(sample("other")),"b",10000000));
 std::map<mb::Channel,std::vector<mb::c1::Envelope>> sent;
 auto send=[&](mb::Channel ch,std::string_view bytes){mb::c1::Envelope e;mb::c1::Error error;CHECK(mb::c1::DecodeEnvelope(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),e,error));sent[ch].push_back(e);return true;};
 bridge.fanout.flush(send,11000000);CHECK(sent[mb::Channel::Monaka].size()==sent[mb::Channel::Steamvr].size());CHECK(sent[mb::Channel::Mirror].size()==2);
 auto saved=bridge.logical().at({"pico","device"}).pose;CHECK(saved&&saved->input.sequence==0);CHECK(saved->timestamp_ns==9000000);
 auto updated=c;updated.revision=2;updated.bindings.at({"pico","device"}).world.translation={10,0,0};bridge.reconfigure(updated,12000000);CHECK(bridge.logical().at({"pico","device"}).pose->timestamp_ns==9000000);CHECK(bridge.logical().at({"pico","device"}).pose->sequence==saved->sequence+1);
 CHECK(near((*bridge.logical().at({"pico","device"}).pose->position)[0],11));
 CHECK(bridge.receive(wire(sample("other",1)),"b",510000000));auto restart=sample();restart.session_id=restart.clock_id=S2;bridge.receive(wire(restart),"a",520000000);CHECK(bridge.logical().at({"other","device"}).pose);CHECK(!bridge.logical().at({"pico","device"}).pose->angular_velocity);
 bridge.tick(1100000000);CHECK(!bridge.logical().at({"pico","device"}).pose);
 mb::Bridge unknown(c,SB);auto v=sample();v.coordinate_space.convention="vut-native-v1";CHECK(unknown.receive(wire(v),"a",10000000));CHECK(!unknown.logical().at({"pico","device"}).pose);
 c.policy=mb::Policy::Disabled;mb::Bridge disabled(c,SB);disabled.receive(wire(sample()),"a",10000000);CHECK(disabled.fanout.pending(mb::Channel::Mirror)==1);CHECK(disabled.fanout.pending(mb::Channel::Monaka)==0);
 mb::Fanout q;q.capacity=1;q.put(mb::Channel::Mirror,sample(),10);q.put(mb::Channel::Mirror,sample("other"),10);CHECK(q.dropped==1);q.flush([](auto,auto){return false;},20);CHECK(q.errors==1&&q.pending(mb::Channel::Mirror)==0);
 mb::Udp owner(0);bool bindRejected=false;try{mb::Udp duplicateBind(owner.port());}catch(...){bindRejected=true;}CHECK(bindRejected);
 auto path=std::filesystem::path(ROOT_DIR)/"build/test-config.json";mb::saveConfig(path,c);CHECK(mb::loadConfig(path).bindings.size()==2);
 auto conflict=c;conflict.bindings.at({"other","device"}).tracker="logical-pico";bool rejected=false;try{mb::validateConfig(conflict);}catch(...){rejected=true;}CHECK(!rejected);
 CHECK(mb::runtimeSerial("ab","c")!=mb::runtimeSerial("a","bc"));
 std::ifstream vive(std::string(ROOT_DIR)+"/tests/fixtures/vive-v2-synthetic-status-samples.jsonl");std::string line;int count=0;
 while(std::getline(vive,line)){mb::Registry vr;CHECK(vr.receive(line,"vive",100000000));++count;}CHECK(count==7);
 std::cout<<"PASS registry/calibration/mapping/routing/config/UDP and 7 VIVE v2 synthetic status fixtures\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
