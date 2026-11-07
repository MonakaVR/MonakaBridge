#include "helpers.hpp"
#include "monaka_bridge/qjump_bridge.hpp"
#include <iostream>
using namespace std::chrono_literals;
int connected(int argc,char** argv){
 CHECK(argc==6);std::ifstream input(argv[2]);CHECK(bool(input));std::vector<mb::c1::TrackerObservation> observations;std::string line;
 while(std::getline(input,line)){if(line.empty())continue;mb::c1::Envelope e;mb::c1::Error error;CHECK(mb::c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(line.data()),line.size(),e,error));if(auto* p=std::get_if<mb::c1::TrackerObservation>(&e))observations.push_back(*p);}
 CHECK(!observations.empty());mb::Config cfg;cfg.bridgeId="qjump-software-bridge";cfg.policy=mb::Policy::Monaka;
 for(const auto& p:observations){mb::Key key{p.source_id,p.device_id};if(cfg.bindings.count(key))continue;mb::Profile profile;profile.convention=p.coordinate_space.convention;profile.angularSpaceVerified=false;const auto name="fixture-"+std::to_string(cfg.profiles.size());cfg.profiles[name]=profile;mb::Binding b;b.source=p.source_id;b.device=p.device_id;b.tracker="software-logical-"+std::to_string(cfg.bindings.size());b.profile=name;b.inputSpace=p.coordinate_space.id;b.inputRevision=p.coordinate_space.revision;b.worldSpace="software-world";b.worldRevision=1;cfg.bindings[key]=b;mb::CapabilityProfile cap;cap.id="explicit-software-equivalent";cap.provenance="Offline canonical producer output supplied as input; explicit identity axes and identity world/mount";cap.inputDeclaration=cap.outputDeclaration=p.capabilities;cap.inputMode=cap.outputMode=monaka::bridge_input::Mode::STRICT;cfg.contracts[key]=cap;}
 qjump::Options o;o.component="bridge";o.output_dir=std::filesystem::absolute(argv[4]);o.coordination_dir=o.output_dir/"coord";o.pre_ns=100000000;o.post_ns=1000000000;o.sample_ns=50000000;o.build_identity={{"fixture","actual Bridge product route, explicit identity transforms"}};qjump::Collector collector(o);mb::Bridge bridge(cfg,SB);mb::QJumpBridge hooks(collector,bridge);
 const std::string incident="7a9ec380-55f1-4a85-9b9a-645ab15a8d01";nlohmann::json request={{"schema","qjump.request.v1"},{"incident_id",incident},{"origin_component","monaka"},{"origin_process_instance","offline-connected-fixture"},{"wall_clock_utc_ns",std::to_string(qjump::wall())},{"trigger_type","T4"}};
 std::ofstream output(argv[3],std::ios::binary|std::ios::trunc);CHECK(bool(output));int index=0;for(const auto& p:observations){const auto now=1000000000LL+index*2000000LL;CHECK(bridge.receive(wire(p),"offline-canonical-producer",now));bridge.fanout.flush([&](auto ch,std::string_view bytes){if(ch==mb::Channel::Monaka){mb::c1::Envelope e;mb::c1::Error error;CHECK(mb::c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),e,error));if(std::get_if<mb::c1::MtpPose>(&e))output<<bytes<<'\n';hooks.sent(ch,bytes,true,now);}return true;},now);if(index==20){std::ofstream req(o.coordination_dir/(incident+".json"));req<<request.dump();}++index;std::this_thread::sleep_for(2ms);}
 output.close();std::this_thread::sleep_for(1200ms);collector.stop();CHECK(collector.written()>0);std::cout<<"CONNECTED bridge bundle "<<collector.directory().string()<<" / actual MVIP "<<argv[3]<<"\n";return 0;
}
int main(int argc,char** argv)try{
 if(argc>1&&std::string(argv[1])=="--connected")return connected(argc,argv);
 CHECK(argc==2);qjump::Options o;o.component="bridge";o.output_dir=std::filesystem::absolute(argv[1]);o.coordination_dir=o.output_dir/"coord";o.pre_ns=1000000;o.post_ns=30000000;o.sample_ns=1000000;std::vector<nlohmann::json> exported;o.sink=[&](const auto& path,const auto& lines){for(auto& line:lines)exported.push_back(nlohmann::json::parse(line));return qjump::durable(path,lines);};qjump::Collector collector(o);
 auto cfg=config();mb::Bridge off(cfg,SB),on(cfg,SB);mb::QJumpBridge hooks(collector,on);
 // Same ordinary route with diagnostics OFF/ON; exact serialized wire equality
 // also verifies unrelated output counts, timestamps, validity and provenance.
 std::vector<std::string> a,b;
 for(int i=0;i<30;++i){auto p=sample("pico",i);p.orientation=i==15?mb::Quat{0,0.866025403784,0,0.5}:mb::Quat{0,0,0,1};p.timestamp_ns+=i*1000000;p.sent_at_ns=p.timestamp_ns+1000000;const auto now=10000000+i*1000000;CHECK(off.receive(wire(p),"peer",now)==on.receive(wire(p),"peer",now));off.fanout.flush([&](auto,std::string_view bytes){a.emplace_back(bytes);return true;},now);on.fanout.flush([&](auto ch,std::string_view bytes){b.emplace_back(bytes);hooks.sent(ch,bytes,true,now);return true;},now);std::this_thread::sleep_for(2ms);}
 CHECK(a==b);std::this_thread::sleep_for(120ms);collector.stop();CHECK(collector.written()>0);
 std::set<std::string> stages;bool wirePhase=false,transform=false,exact=false;
 for(const auto& j:exported)if(j.value("kind","")=="footer")CHECK(j.contains("incident_ids")&&!j["incident_ids"].empty());
 for(auto& j:exported)if(j.value("kind","")=="sample"){stages.insert(j.value("stage",""));if(j["stage"]=="bridge_published"){CHECK(j["links"]["input"]["session_id"]==S1);CHECK(j["identity"]["publisher_id"]==cfg.bridgeId);if(j.value("publication_phase","")=="actual_wire"){wirePhase=true;CHECK(j["wire_send_success"]==true);CHECK(j["timestamps"]["bridge_sent_at_ns"].is_string());}}if(j["context"].contains("transform"))transform=true;if(j["identity"].contains("sequence"))exact=j["identity"]["sequence"].is_string();}
 CHECK(stages.count("bridge_input")&&stages.count("bridge_axis")&&stages.count("bridge_calibrated")&&stages.count("bridge_published"));CHECK(wirePhase&&transform&&exact);
 // Throwing diagnostic hooks cannot alter admitted/published tracking.
 mb::Bridge bad(cfg,SB);bad.diagnosticReceive=[](auto&,auto){throw std::runtime_error("diagnostic");};bad.diagnosticPublication=[](auto&,auto&,auto&,auto&,auto&,auto&,auto){throw std::runtime_error("diagnostic");};CHECK(bad.receive(wire(sample()),"peer",10000000));CHECK(bad.logical().at({"pico","device"}).pose->orientation);
 // Unknown explicit profile is ordinarily admitted but has no transformed
 // output. Diagnostic absence of a transform is an observable state, no drop.
 qjump::Options other=o;other.output_dir=o.output_dir/"unknown-profile";other.sink=[](auto&,auto&){return true;};qjump::Collector unknownCollector(other);auto missing=cfg;missing.profiles.clear();mb::Bridge unknown(missing,SB);mb::QJumpBridge unknownHooks(unknownCollector,unknown);CHECK(unknown.receive(wire(sample()),"peer",10000000));CHECK(!unknown.logical().at({"pico","device"}).pose);unknownCollector.stop();CHECK(unknownCollector.dropped()==0);
 std::cout<<"PASS real Bridge hooks: native/axis/calibrated/queued/wire exact lineage; ON/OFF output parity; throwing hooks isolated\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
