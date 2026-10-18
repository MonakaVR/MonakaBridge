#include "monaka_bridge/bridge.hpp"
#include "monaka_bridge/network.hpp"
#include "monaka_bridge/health.hpp"
#include "monaka_bridge/discovery.hpp"
#include "monaka_bridge/runtime_replay.hpp"
#include "p1_runtime_identity.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
std::atomic_bool running{true};
void stop(int){running=false;}
}
int main(int argc,char** argv)try{
 if(argc>=2&&std::string(argv[1])=="--replay")return mb::runtimeReplay(argc,argv);
 if(argc==2&&std::string(argv[1])=="--version"){std::cout<<nlohmann::json{{"bridge_source",P1_BRIDGE_SOURCE},{"source_input_hash",P1_BRIDGE_INPUT_HASH},{"mbip",P1_MBIP_COMMIT},{"mvip",P1_MVIP_DELIVERY},{"mvip_implementation",P1_MVIP_IMPLEMENTATION},{"mvip_kit_sha256",P1_MVIP_KIT_SHA256},{"default_policy","STRICT"}}.dump()<<'\n';return 0;}
 if(argc==2&&std::string(argv[1])=="--help"){std::cout<<"monaka_bridge_service CONFIG [INGRESS MONAKA DIRECT MIRROR] [--duration-ms N]\nmonaka_bridge_service --stop\nmonaka_bridge_service --version\nmonaka_bridge_service --replay CONFIG INPUT_JSONL OUTPUT_JSONL SESSION [--mtp]\n";return 0;}
#ifdef _WIN32
 if(argc==2&&std::string(argv[1])=="--stop"){HANDLE e=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\MonakaBridge_Stop");if(!e)return 1;SetEvent(e);CloseHandle(e);return 0;}
#endif
 if(argc<2)throw std::invalid_argument("configuration path required; use --help");
 std::filesystem::path path=argv[1];auto config=mb::loadConfig(path);std::uint16_t ports[4]={29810,29811,29812,29813};int next=2;
 if(argc>=6&&std::string(argv[2])!="--duration-ms"){for(int i=0;i<4;++i){auto p=std::stoul(argv[i+2]);if(!p||p>65535)throw std::invalid_argument("invalid port");ports[i]=static_cast<std::uint16_t>(p);}next=6;}
 std::int64_t duration=0;if(argc>next){if(argc!=next+2||std::string(argv[next])!="--duration-ms")throw std::invalid_argument("invalid arguments");duration=std::stoll(argv[next+1])*1000000;}
 mb::Udp input(ports[0]);mb::Udp output;const auto epoch=mb::monotonicNs();mb::Bridge bridge(config,mb::uuid());mb::DiscoveryCache discovery;
 std::unique_ptr<mb::HealthWriter> healthWriter;
 try{auto health=path;health+=L".status.json";healthWriter=std::make_unique<mb::HealthWriter>(health);}catch(...){/* Diagnostic failure cannot disable tracking. */}
 std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
#ifdef _WIN32
 const auto stopName=ports[0]==29810?std::wstring(L"Local\\MonakaBridge_Stop"):std::wstring(L"Local\\MonakaBridge_Stop_")+std::to_wstring(ports[0]);
 HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,stopName.c_str());if(!stopEvent)throw std::runtime_error("stop event unavailable");ResetEvent(stopEvent);
#endif
 auto modified=std::filesystem::last_write_time(path);std::int64_t lastStatus=-1000000000;
 while(running){auto now=mb::monotonicNs()-epoch;if(duration>0&&now>=duration)break;
#ifdef _WIN32
 if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0)break;
#endif
 for(int i=0;i<128;++i){auto d=input.receive();if(!d)break;discovery.receive(bridge,d->bytes,d->peer,mb::monotonicNs()-epoch);}
 now=mb::monotonicNs()-epoch;bridge.tick(now);
 bridge.fanout.flush([&](mb::Channel c,std::string_view bytes){return output.send(ports[c==mb::Channel::Monaka?1:c==mb::Channel::Steamvr?2:3],bytes);},now);
 if(now-lastStatus>=500000000){
  lastStatus=now;
  try{auto change=std::filesystem::last_write_time(path);if(change!=modified){bridge.reconfigure(mb::loadConfig(path),now);modified=change;}}catch(const std::exception& e){std::cerr<<"config not applied: "<<e.what()<<'\n';}
  if(healthWriter)try{
   const auto unixMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
   auto snapshot=mb::healthSnapshot(bridge,now,unixMs);
   snapshot["discovery"]=discovery.snapshot(bridge,now);
   snapshot["health_write_failures"]=healthWriter->writeFailures();
   snapshot["health_replace_failures"]=healthWriter->replaceFailures();
   snapshot["health_dropped"]=healthWriter->dropped();
   healthWriter->publish(std::move(snapshot));
  }catch(...){/* Snapshot allocation/format failure is also best effort. */}
 }
 std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
#ifdef _WIN32
 CloseHandle(stopEvent);
#endif
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
