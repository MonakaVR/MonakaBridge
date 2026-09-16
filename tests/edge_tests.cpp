#include "helpers.hpp"
#include "monaka_bridge/direct.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
int main()try{
 auto c=config();mb::Bridge b(c,SB);b.receive(wire(sample()),"p",10000000);
 auto send=[](auto,auto){return true;};b.fanout.flush(send,11000000);
 // Metadata cannot extend age; absence cancels a pose already in the fanout queue.
 auto absent=state(sample());absent.presence="absent";absent.tracking_state="disconnected";
 b.receive(wire(absent),"p",12000000);CHECK(!b.logical().at({"pico","device"}).pose);
 auto next=sample("pico",1);CHECK(!b.receive(wire(next),"p",13000000));
 auto present=state(sample(),1);b.receive(wire(present),"p",14000000);
 next.sequence=2;next.timestamp_ns+=1000000;next.sent_at_ns+=1000000;b.receive(wire(next),"p",15000000);CHECK(b.logical().at({"pico","device"}).pose);
 mb::MtpFeed feed;auto first=*b.logical().at({"pico","device"}).pose;first.sent_at_ns=16000000;
 CHECK(feed.receive(wire(first),10000000));auto key=mb::Key{mb::outputSource(c.bridgeId,"pico"),"logical-pico"};CHECK(feed.devices.at(key).fixedTime==8000000);
 CHECK(!feed.receive(wire(first),20000000));CHECK(feed.devices.at(key).fixedTime==8000000);
 auto restarted=first;restarted.session_id=restarted.clock_id=S2;restarted.sequence=0;
 CHECK(feed.receive(wire(restarted),520000000));CHECK(!feed.receive(wire(first),530000000));
 auto cfg=c;cfg.revision=2;cfg.bindings.at({"other","device"}).tracker="logical-pico";cfg.bindings.at({"pico","device"}).tracker="new";
 bool failed=false;try{b.reconfigure(cfg,16000000);}catch(...){failed=true;}CHECK(!failed);CHECK(b.config().revision==2);
 // Rigid rotation and explicit mount are applied exactly once in Hamilton order.
 auto binding=c.bindings.begin()->second;binding.world.rotation={0,0,std::sqrt(.5),std::sqrt(.5)};binding.world.translation={10,20,30};binding.mount.translation={1,0,0};binding.mount.rotation={std::sqrt(.5),0,0,std::sqrt(.5)};
 auto p=sample();p.orientation=mb::Quat{0,0,0,1};mb::History h;mb::Profile profile;profile.approved=true;
 auto out=mb::calibrate(p,binding,profile,h,10000000);CHECK(near((*out.position)[0],8)&&near((*out.position)[1],22));
 auto expected=mb::mul(binding.world.rotation,binding.mount.rotation);for(int i=0;i<4;++i)CHECK(near((*out.orientation)[i],expected[i]));
 auto omega=mb::angularVelocity({0,0,0,1},{0,0,std::sin(.01),std::cos(.01)},.01);CHECK(omega&&near((*omega)[2],2));
 // Mapping updates reset derivative history and preserve original sample age.
 cfg=c;cfg.revision=3;b.reconfigure(cfg,17000000);CHECK(!b.logical().at({"pico","device"}).pose->angular_velocity);
 auto dir=std::filesystem::path(ROOT_DIR)/"build"/("migration-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(dir);
 {std::ofstream f(dir/"route");f<<"monaka-external";}{std::ofstream f(dir/"alignment.json");f<<R"({"version":1,"xMeters":1,"yMeters":2,"zMeters":3})";}
 mb::migrateLegacy(dir/"route",dir/"alignment.json",dir/"new.json",c);auto migrated=mb::loadConfig(dir/"new.json");CHECK(migrated.policy==mb::Policy::Monaka&&migrated.revision==2);CHECK(!migrated.bindings.begin()->second.spaceApproved);CHECK(std::filesystem::exists(dir/"route.pre-monaka-bridge.bak"));CHECK(std::filesystem::exists(dir/"alignment.json.pre-monaka-bridge.bak"));
 // Overflowing revisions and timeout are rejected, never wrapped or multiplied first.
 std::ifstream raw(dir/"new.json");std::string data((std::istreambuf_iterator<char>(raw)),{});auto at=data.find("\"mapping_revision\": 2");CHECK(at!=std::string::npos);data.replace(at,21,"\"mapping_revision\": 4294967296");{std::ofstream f(dir/"overflow.json");f<<data;}
 failed=false;try{mb::loadConfig(dir/"overflow.json");}catch(...){failed=true;}CHECK(failed);
 // A blocked output never blocks independent channels, and queue size is finite.
 mb::Fanout q;for(int i=0;i<1000;++i){auto v=sample("source-"+std::to_string(i));q.put(mb::Channel::Mirror,v,10);}CHECK(q.pending(mb::Channel::Mirror)==512&&q.dropped==488);
 q.put(mb::Channel::Monaka,first,10);int delivered=0;q.flush([&](auto ch,auto){if(ch==mb::Channel::Monaka)++delivered;return ch==mb::Channel::Monaka;},20000000,1);CHECK(delivered==1&&q.pending(mb::Channel::Mirror)==511);
 std::cout<<"PASS absence/retired Direct sessions/reassignment/rigid transform/derivative reset/migration backups/overflow/bounded fanout\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
