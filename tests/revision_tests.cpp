#include "helpers.hpp"
#include "monaka_bridge/direct.hpp"
#include <iostream>
int main() try {
 auto p=sample(); mb::Registry r;
 CHECK(r.receive(wire(p),"port-A",10000000));
 auto restart=p;restart.session_id=restart.clock_id=S2;
 CHECK(!r.receive(wire(restart),"port-B",20000000));
 CHECK(r.find({"pico","device"})->pose->session_id==S1);
 CHECK(!r.receive(wire(p),"port-A",400000000)); // duplicates do not extend lease
 CHECK(r.receive(wire(restart),"port-B",510000000));
 CHECK(!r.receive(wire(p),"port-A",520000000));
 CHECK(r.find({"pico","device"})->pose->session_id==S2);
 mb::Registry ordered;
 CHECK(ordered.receive(wire(p),"a",10000000));
 auto absent=state(p); absent.presence="absent"; absent.tracking_state="disconnected";
 CHECK(ordered.receive(wire(absent),"a",11000000));
 auto old=p;old.sequence=1;CHECK(!ordered.receive(wire(old),"a",12000000));
 auto fresh=p;fresh.sequence=2;fresh.timestamp_ns+=200;fresh.sent_at_ns+=200;
 CHECK(ordered.receive(wire(fresh),"a",13000000)); // no present metadata needed
 absent.sequence=1;CHECK(ordered.receive(wire(absent),"a",14000000));
 CHECK(ordered.fresh({"pico","device"},14000000));
 auto sibling=p;sibling.device_id="sibling";CHECK(ordered.receive(wire(sibling),"a",15000000));
 auto fault=fresh;fault.sequence=3;fault.coordinate_space.id="wrong";
 CHECK(!ordered.receive(wire(fault),"a",16000000));
 CHECK(ordered.fresh({"pico","sibling"},16000000));CHECK(!ordered.fresh({"pico","device"},16000000));
 fresh.sequence=3;CHECK(ordered.receive(wire(fresh),"a",16000001));
 fault=fresh;fault.position=mb::Vec{999,0,0}; // conflicting same sequence is tracker-local
 CHECK(!ordered.receive(wire(fault),"a",17000000));
 CHECK(!ordered.find({"pico","device"}));CHECK(ordered.fresh({"pico","sibling"},17000000));
 auto c=config(); c.bindings.at({"other","device"}).tracker="logical-pico";
 mb::Bridge bridge(c,SB); CHECK(bridge.receive(wire(p),"pico-peer",10000000));
 CHECK(bridge.receive(wire(sample("other")),"other-peer",10000000));
 auto a=*bridge.logical().at({"pico","device"}).pose;
 auto b=*bridge.logical().at({"other","device"}).pose;
 CHECK(a.publisher_id==c.bridgeId&&a.source_id=="pico"&&a.input.source_id=="pico");
 CHECK(a.tracker_id==b.tracker_id&&a.source_id!=b.source_id);
 mb::MtpFeed feed;CHECK(feed.receive(wire(a),10000000));CHECK(feed.receive(wire(b),10000000));
 auto otherPublisher=a;otherPublisher.publisher_id="another-bridge";
 CHECK(feed.receive(wire(otherPublisher),10000000)); CHECK(feed.devices.size()==3);
 CHECK(!feed.receive(wire(a),11000000,"imposter"));CHECK(feed.devices.size()==3);
 auto rotation=p;rotation.sequence=1;rotation.modality="rotation_only";rotation.validity.position=false;
 rotation.tracking_state="degraded";rotation.timestamp_ns+=1000000;rotation.sent_at_ns+=1000000;
 CHECK(bridge.receive(wire(rotation),"pico-peer",11000000));
 a=*bridge.logical().at({"pico","device"}).pose;
 CHECK(a.modality=="rotation_only"&&!a.position&&a.orientation&&a.input.device_id==p.device_id);
 auto none=rotation;none.sequence=2;none.modality="none";none.validity.orientation=false;
 none.orientation_evidence="none";none.tracking_state="lost";
 CHECK(bridge.receive(wire(none),"pico-peer",12000000));
 a=*bridge.logical().at({"pico","device"}).pose;CHECK(a.modality=="none"&&!a.position&&!a.orientation);
 CHECK(bridge.logical().at({"other","device"}).pose->modality=="full");
 std::cout<<"PASS v2 identity / bounded lease / accepted-only age / absence reorder recovery / tracker isolation / modality\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
