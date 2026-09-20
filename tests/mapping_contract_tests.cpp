#include "helpers.hpp"
#include "monaka_bridge/mapping_selection.hpp"
#include <iostream>
template<class F>void rejected(F operation){bool fail=false;try{operation();}catch(const std::invalid_argument&){fail=true;}CHECK(fail);}
int main()try{
 auto c=config();c.bindings.at({"other","device"}).tracker="logical-pico";mb::validateConfig(c);
 rejected([&]{mb::detail::uniqueTracker(c,"logical-pico");});
 for(const auto source:{"pico","other"}){
  auto serial=mb::runtimeSerial(mb::outputSource(c.bridgeId,source),"logical-pico");
  CHECK(mb::detail::runtimeTracker(c,serial).source==source);
 }
 rejected([&]{mb::detail::runtimeTracker(c,mb::runtimeSerial(mb::outputSource("wrong-publisher","pico"),"logical-pico"));});
 auto collision=c;auto duplicate=collision.bindings.at({"pico","device"});duplicate.device="another-device";
 collision.bindings[{duplicate.source,duplicate.device}]=duplicate;rejected([&]{mb::validateConfig(collision);});
 mb::Bridge bridge(c,SB);CHECK(bridge.receive(wire(sample()),"one",10000000));CHECK(bridge.receive(wire(sample("other")),"two",10000000));
 mb::MtpFeed feed;auto output=[&](mb::Channel channel,std::string_view bytes){if(channel==mb::Channel::Monaka)CHECK(feed.receive(bytes,20000000));return true;};
 bridge.fanout.flush(output,11000000);CHECK(feed.devices.size()==2);
 auto otherPublisher=c;otherPublisher.bridgeId="another-publisher";otherPublisher.bindings.erase({"other","device"});mb::Bridge sibling(otherPublisher,S2);
 CHECK(sibling.receive(wire(sample()),"one",10000000));sibling.fanout.flush(output,11000000);CHECK(feed.devices.size()==3);
 // Same source/tracker -> different device still requires an explicit restart.
 auto rebound=c;++rebound.revision;auto b=rebound.bindings.at({"pico","device"});rebound.bindings.erase({"pico","device"});b.device="new-device";rebound.bindings[{b.source,b.device}]=b;
 rejected([&]{bridge.reconfigure(rebound,12000000);});CHECK(bridge.config().bindings.count({"pico","device"})==1);
 mb::Bridge restarted(rebound,S2);auto newDevice=sample();newDevice.device_id="new-device";CHECK(restarted.receive(wire(newDevice),"one",10000000));CHECK(restarted.logical().at({"pico","new-device"}).pose);
 CHECK(restarted.config().bindings.size()==2);CHECK(!restarted.config().bindings.count({"pico","device"}));
 // Source change intentionally changes persistent output identity, invalidates old output.
 auto changed=c;++changed.revision;b=changed.bindings.at({"pico","device"});changed.bindings.erase({"pico","device"});b.source="new-source";changed.bindings[{b.source,b.device}]=b;
 bridge.reconfigure(changed,12000000);CHECK(bridge.receive(wire(sample("new-source")),"three",13000000));
 bridge.fanout.flush(output,14000000);
 const auto& old=feed.devices.at({mb::outputSource(c.bridgeId,"pico"),"logical-pico"});CHECK(old.absent);
 CHECK(feed.devices.at({mb::outputSource(c.bridgeId,"new-source"),"logical-pico"}).pose);
 CHECK(bridge.config().bindings.size()==2);
 std::cout<<"PASS mapping identity: same tracker across sources/publishers, collision, exact runtime serial, rebind restart gate and source-change invalidation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
