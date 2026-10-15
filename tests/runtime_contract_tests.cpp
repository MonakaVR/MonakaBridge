#include "helpers.hpp"
#include <iostream>
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);}
int main()try{
 auto c=config();auto key=mb::Key{"pico","device"};
 auto& declaration=c.contracts.at(key);declaration.inputDeclaration={"position","orientation","linear_velocity"};declaration.outputDeclaration={"position","orientation","linear_velocity","angular_velocity"};
 declaration.inputMode=monaka::bridge_input::Mode::STRICT;
 auto p=sample();p.capabilities=declaration.inputDeclaration;
 mb::Bridge b(c,SB);CHECK(b.receive(wire(p),"peer",10000000));
 auto stable=b.logical().at(key).pose->capabilities;CHECK(stable==declaration.outputDeclaration);
 // CANDIDATE remains supported but invalid, then ADMITTED changes only validity/pose.
 ++p.sequence;p.validity={false,false};p.modality="none";p.position.reset();p.orientation.reset();p.orientation_evidence="none";p.tracking_state="lost";
 CHECK(b.receive(wire(p),"peer",20000000));auto unavailable=b.logical().at(key).pose;
 CHECK(unavailable&&!unavailable->validity.position&&!unavailable->validity.orientation&&unavailable->capabilities==stable);
 p=sample();p.sequence=2;p.capabilities=declaration.inputDeclaration;
 CHECK(b.receive(wire(p),"peer",30000000));CHECK(b.logical().at(key).pose->validity.position&&b.logical().at(key).pose->capabilities==stable);
 CHECK(!b.receive(wire(p),"peer",30000001)); // Original duplicate rules unchanged.
 auto foreign=p;foreign.source_id="foreign";CHECK(!b.receive(wire(foreign),"peer",30000002));
 foreign=p;foreign.device_id="foreign";CHECK(!b.receive(wire(foreign),"peer",30000003));
 foreign=p;foreign.session_id=foreign.clock_id=S2;foreign.sequence=0;
 CHECK(!b.receive(wire(foreign),"peer",30000004)); // Active lease does not authorize takeover.
 CHECK(b.receive(wire(foreign),"peer",600000000));CHECK(b.logical().at(key).pose->capabilities==stable);
 CHECK(!b.receive(wire(p),"peer",600000001)); // Retired session.
 auto unsupported=foreign;unsupported.sequence=1;unsupported.capabilities.push_back("linear_acceleration");
 CHECK(!b.receive(wire(unsupported),"peer",620000000));
 // Missing declarations never become inferred from configurable mappings.
 auto absent=c;absent.contracts.clear();mb::Bridge closed(absent,SB);CHECK(!closed.receive(wire(p),"peer",10000000));CHECK(closed.fanout.pending(mb::Channel::Monaka)==0);
 // Removing and restoring a mapping cannot evade lifetime declaration immutability.
 mb::RuntimeNormalization lifetime(c,SB);auto removed=c;removed.contracts.erase(key);lifetime.reconfigure(removed);
 auto changed=c;changed.contracts.at(key).outputDeclaration.push_back("linear_acceleration");rejects([&]{lifetime.reconfigure(changed);});
 lifetime.reconfigure(c);
 auto invalid=absent;invalid.bindings.at(key).world.rotation={0,0,0,0};rejects([&]{mb::validateConfig(invalid);});
 std::cout<<"PASS runtime contract: candidate/admitted, duplicate, two identities, active/new/retired session, out-of-contract, missing declaration, lifetime reconfigure, ordinary validation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
