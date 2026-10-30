#include "monaka_bridge/bridge.hpp"
#include "monaka_bridge/network.hpp"
#include "monaka_bridge/health.hpp"
#include "monaka_bridge/discovery.hpp"
#include "monaka_bridge/runtime_replay.hpp"
#include "monaka_bridge/service_runtime.hpp"
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
#include "monaka_bridge/service_lifecycle.hpp"
#endif
namespace {
std::atomic_bool running{true};
void stop(int){running=false;}
}
int main(int argc,char** argv)try{
#ifdef _WIN32
 if(argc>=2&&std::string(argv[1])=="--native-owner")return mb::nativeOwner(argc,argv);
 if(argc>=2&&std::string(argv[1])=="--owner-command")return mb::nativeOwnerCommand(argc,argv);
 DWORD trayOwner=0;std::uint64_t trayOwnerCreated=0;
 while(argc>=2) {
  if(argc>=3&&std::string(argv[argc-2])=="--tray-owner") {
   pc_service_lifecycle::ParseOwner(argv[argc-1],trayOwner,trayOwnerCreated);argc-=2;
  }else break;
 }
#endif
 if(argc>=2&&std::string(argv[1])=="--replay")return mb::runtimeReplay(argc,argv);
 if(argc==2&&std::string(argv[1])=="--version"){std::cout<<nlohmann::json{{"bridge_source",P1_BRIDGE_SOURCE},{"source_input_hash",P1_BRIDGE_INPUT_HASH},{"mbip",P1_MBIP_COMMIT},{"mvip",P1_MVIP_DELIVERY},{"mvip_implementation",P1_MVIP_IMPLEMENTATION},{"mvip_kit_sha256",P1_MVIP_KIT_SHA256},{"default_policy","STRICT"}}.dump()<<'\n';return 0;}
 if(argc==2&&std::string(argv[1])=="--help"){std::cout<<"monaka_bridge_service CONFIG [INGRESS MONAKA DIRECT MIRROR] [--duration-ms N] [--tray-owner PID:FILETIME]\nWindows: an owner or a positive bounded duration is required.\nmonaka_bridge_service --native-owner CONFIG GUI CONFIG_HELPER INGRESS MONAKA DIRECT MIRROR [--open] [--read-only]\nmonaka_bridge_service --owner-command CONFIG Start|Stop|Restart|Open|Close|Exit|Probe\nmonaka_bridge_service --stop\nmonaka_bridge_service --version\nmonaka_bridge_service --replay CONFIG INPUT_JSONL OUTPUT_JSONL SESSION [--mtp]\n";return 0;}
#ifdef _WIN32
 if(argc==2&&std::string(argv[1])=="--stop")return pc_service_lifecycle::StopAndWait(L"Local\\MonakaBridge_ServiceLifecycle_29810",5000);
#endif
 if(argc<2)throw std::invalid_argument("configuration path required; use --help");
 std::filesystem::path path=argv[1];std::uint16_t ports[4]={29810,29811,29812,29813};int next=2;
 if(argc>=6&&std::string(argv[2])!="--duration-ms"){for(int i=0;i<4;++i){auto p=std::stoul(argv[i+2]);if(!p||p>65535)throw std::invalid_argument("invalid port");ports[i]=static_cast<std::uint16_t>(p);}next=6;}
 std::int64_t duration=0;if(argc>next){if(argc!=next+2||std::string(argv[next])!="--duration-ms")throw std::invalid_argument("invalid arguments");duration=std::stoll(argv[next+1])*1000000;}
#ifdef _WIN32
 if(!trayOwner&&duration<=0)throw std::invalid_argument("Start through the Bridge tray; an owner or a bounded --duration-ms is required");
#endif
 std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
#ifdef _WIN32
 std::unique_ptr<pc_service_lifecycle::Endpoint> lifecycle;
 pc_service_lifecycle::Handle stopEvent;
#endif
 mb::runService(path,ports,duration,[&]{
#ifdef _WIN32
  return !running||lifecycle->StopRequested()||WaitForSingleObject(stopEvent.value,0)==WAIT_OBJECT_0;
#else
  return !running;
#endif
 },[&]{
#ifdef _WIN32
  lifecycle=std::make_unique<pc_service_lifecycle::Endpoint>(L"Local\\MonakaBridge_ServiceLifecycle_"+std::to_wstring(ports[0]),trayOwner,trayOwnerCreated);
  const auto stopName=ports[0]==29810?std::wstring(L"Local\\MonakaBridge_Stop"):std::wstring(L"Local\\MonakaBridge_Stop_")+std::to_wstring(ports[0]);
  stopEvent.value=CreateEventW(nullptr,TRUE,FALSE,stopName.c_str());if(!stopEvent.value)throw std::runtime_error("stop event unavailable");ResetEvent(stopEvent.value);lifecycle->MarkReady();
#endif
 });
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
