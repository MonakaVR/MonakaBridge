#include "helpers.hpp"
#include "monaka_bridge/diagnostic.hpp"
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>
using J=nlohmann::json;
int main(int argc,char** argv){try{
 std::vector<J> events;
 mb::DiagnosticWriter writer([&](const J& e){events.push_back(e);},4096);mb::DiagnosticObserver observer(writer);
 auto c=config();mb::Bridge on(c,SB),off(c,SB);on.registry.diagnostic=&observer;
 writer.emit("component_started");writer.emit("component_ready","ingress",nullptr,nullptr,nullptr,{{"inventory_complete",false}});
 auto send=[&](auto p,std::int64_t time){auto bytes=wire(p);CHECK(on.receive(bytes,"mock",time)==off.receive(bytes,"mock",time));CHECK(on.registry.rejected==off.registry.rejected);std::vector<std::pair<mb::Channel,std::string>> observed,baseline;on.fanout.flush([&](auto channel,std::string_view wire){observed.emplace_back(channel,wire);return true;},time);off.fanout.flush([&](auto channel,std::string_view wire){baseline.emplace_back(channel,wire);return true;},time);CHECK(observed==baseline);std::this_thread::sleep_for(std::chrono::milliseconds(2));};
 auto p=sample();p.validity.position=false;p.position.reset();p.modality="rotation_only";p.tracking_state="degraded";send(p,10000000);send(p,10000000);p.sequence++;p.position=mb::Vec{1,2,3};p.validity.position=true;p.modality="full";p.tracking_state="tracked";send(p,11000000);
 on.tick(600000000);off.tick(600000000);std::this_thread::sleep_for(std::chrono::milliseconds(2));p.session_id=p.clock_id=S2;p.sequence=0;send(p,610000000);
 auto absent=state(p);absent.presence="absent";absent.tracking_state="disconnected";send(absent,620000000);writer.emit("fatal_error","component",nullptr,nullptr,nullptr,{{"reason","test"}});writer.close();
 CHECK(writer.criticalDropped()==0);
 auto count=[&](std::string type){return std::count_if(events.begin(),events.end(),[&](const J& e){return e["event_type"]==type;});};
 CHECK(count("first_pose")==2);CHECK(count("first_position_valid")==2);CHECK(count("session_reconnecting")==1);CHECK(count("tracker_unregistered")>=2);CHECK(count("diagnostic_warning")>=1);CHECK(count("fatal_error")==1);
 std::int64_t previous=-1;for(const auto& e:events){CHECK(e["schema_version"]==1);CHECK(e["monotonic_ns"].get<std::int64_t>()>=previous);previous=e["monotonic_ns"];if(!e["tracker_identity"].is_null()){if(e["event_type"]=="tracker_mapping"){CHECK(e["tracker_identity"]["domain"]=="protocol_tracker_id");CHECK(e["tracker_identity"]["value"]=="logical-pico");CHECK(e["identities"].size()==2);}else{CHECK(e["tracker_identity"]["domain"]=="backend_device_id");CHECK(e["tracker_identity"]["value"]=="device");}}CHECK(e["role"].is_null());}
 if(argc>1){std::ofstream file(argv[1]);for(const auto& e:events)file<<e.dump()<<'\n';}
 // A deliberately stalled sink verifies no producer waits on I/O and pose eviction keeps a fatal event.
 std::mutex mutex;std::condition_variable cv;bool entered=false,release=false;std::vector<J> pressure;
 mb::DiagnosticWriter blocked([&](const J& e){{std::unique_lock<std::mutex> lock(mutex);entered=true;cv.notify_one();cv.wait(lock,[&]{return release;});}pressure.push_back(e);},8);
 blocked.emit("component_started");{std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[&]{return entered;});}
 auto begin=std::chrono::steady_clock::now();for(int i=0;i<10000;++i)blocked.emit("pose_observed","mock",nullptr,nullptr,nullptr,J::object(),false);
 blocked.emit("fatal_error");CHECK(blocked.dropped()>0);CHECK(blocked.criticalDropped()==0);
 auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();{std::lock_guard<std::mutex> lock(mutex);release=true;}cv.notify_one();blocked.close();CHECK(std::count_if(pressure.begin(),pressure.end(),[](const J& e){return e["event_type"]=="fatal_error";})==1);
 std::cout<<"diagnostic tests PASS; stalled sink producer 10000 events us="<<elapsed<<" dropped="<<blocked.dropped()<<" critical_dropped="<<blocked.criticalDropped()<<'\n';
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
