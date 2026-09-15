#include "monaka_bridge/bridge.hpp"
#include "monaka_bridge/network.hpp"
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
namespace {std::atomic_bool running{true};void stop(int){running=false;}}
int main(int argc,char** argv)try{
 if(argc==2&&std::string(argv[1])=="--help"){std::cout<<"monaka_bridge_service CONFIG [INGRESS MONAKA DIRECT MIRROR] [--duration-ms N]\nmonaka_bridge_service --stop\n";return 0;}
#ifdef _WIN32
 if(argc==2&&std::string(argv[1])=="--stop"){HANDLE e=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\MonakaBridge_Stop");if(!e)return 1;SetEvent(e);CloseHandle(e);return 0;}
#endif
 if(argc<2)throw std::invalid_argument("configuration path required; use --help");
 std::filesystem::path path=argv[1];auto config=mb::loadConfig(path);std::uint16_t ports[4]={29810,29811,29812,29813};int next=2;
 if(argc>=6&&std::string(argv[2])!="--duration-ms"){for(int i=0;i<4;++i){auto p=std::stoul(argv[i+2]);if(!p||p>65535)throw std::invalid_argument("invalid port");ports[i]=static_cast<std::uint16_t>(p);}next=6;}
 std::int64_t duration=0;if(argc>next){if(argc!=next+2||std::string(argv[next])!="--duration-ms")throw std::invalid_argument("invalid arguments");duration=std::stoll(argv[next+1])*1000000;}
 mb::Udp input(ports[0]);mb::Udp output;const auto epoch=mb::monotonicNs();mb::Bridge bridge(config,mb::uuid());
 std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
#ifdef _WIN32
 HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,L"Local\\MonakaBridge_Stop");if(!stopEvent)throw std::runtime_error("stop event unavailable");ResetEvent(stopEvent);
#endif
 auto modified=std::filesystem::last_write_time(path);std::int64_t lastStatus=-1000000000;
 while(running){auto now=mb::monotonicNs()-epoch;if(duration>0&&now>=duration)break;
#ifdef _WIN32
 if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0)break;
#endif
 for(int i=0;i<128;++i){auto d=input.receive();if(!d)break;bridge.receive(d->bytes,d->peer,mb::monotonicNs()-epoch);}
 now=mb::monotonicNs()-epoch;bridge.tick(now);
 bridge.fanout.flush([&](mb::Channel c,std::string_view bytes){return output.send(ports[c==mb::Channel::Monaka?1:c==mb::Channel::Steamvr?2:3],bytes);},now);
 if(now-lastStatus>=500000000){
  lastStatus=now;
  try{auto change=std::filesystem::last_write_time(path);if(change!=modified){bridge.reconfigure(mb::loadConfig(path),now);modified=change;}}catch(const std::exception& e){std::cerr<<"config not applied: "<<e.what()<<'\n';}
  nlohmann::json status={{"policy",mb::policyName(bridge.config().policy)},{"mapping_revision",bridge.config().revision},{"malformed",bridge.registry.malformed},{"rejected",bridge.registry.rejected},{"collisions",bridge.registry.collisions},{"send_errors",bridge.fanout.errors},{"devices",nlohmann::json::array()}};
  for(auto& [source,s]:bridge.registry.sources)for(auto& [device,d]:s.devices){auto binding=bridge.config().bindings.find({source,device});status["devices"].push_back({{"source",source},{"device",device},{"space",d.space},{"convention",d.convention},{"revision",d.revision},{"fresh",bridge.registry.fresh({source,device},now)},{"tracker",binding==bridge.config().bindings.end()?"unmapped":binding->second.tracker},{"collision",s.collision}});}
  std::ofstream health(path.string()+".status.json");health<<status.dump(2);
 }
 std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
#ifdef _WIN32
 CloseHandle(stopEvent);
#endif
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
