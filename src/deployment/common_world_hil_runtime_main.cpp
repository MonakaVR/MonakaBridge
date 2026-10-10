#include "monaka_bridge/common_world_deployment_readiness.hpp"
#include "monaka_bridge/network.hpp"
#include <fstream>
#include <iostream>
using namespace monaka_bridge; namespace w=authority_wire;
namespace {
std::string bytes(const std::filesystem::path& p) {
 std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("config unavailable");
 std::string out((std::istreambuf_iterator<char>(f)),{}); if(f.bad() || out.size()>65536) throw std::runtime_error("config read failed"); return out;
}
w::Envelope decode(const std::string& line) {
 w::Envelope e; w::Error error;
 if(!w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(line.data()),line.size(),e,error)) throw std::runtime_error("authorized source codec failure"); return e;
}
}
// Packaged, file-stream controlled host. No listener, background reconnect or driver.
// Sentinel and config are polled before each receive/pump/activation entry. The stop
// takes effect at local revoke serialization; an already-entered datagram is unrecalled.
int main(int argc,char** argv) try {
 if(argc==5 && std::string(argv[1])=="--control" && std::string(argv[3])=="--emergency-stop-file")
  return runCommonWorldControlHost(argv[2],argv[4]);
 CommonWorldProductionActivation runtime; CommonWorldActivationConfig config;
 if(argc==1) { std::cout<<commonWorldRuntimeDiagnostics(runtime.snapshot(),config,commonWorldBuildCommit())<<'\n'; return 0; }
 if(argc==2 && std::string(argv[1])=="--build-identity") { std::cout<<commonWorldBuildCommit()<<'\n'; return 0; }
 if(argc==2 && std::string(argv[1])=="--help") {
  std::cout<<"--control CONFIG --emergency-stop-file PATH (local stdin JSONL commands + trusted input)\n"
   "CONFIG AUTHORIZED_SOURCE_JSONL [--enable-production --emergency-stop-file PATH]\n"
   "No arguments: OFF. Without explicit enable: prepare only. Readiness JSON is never accepted as authorization.\n"; return 0;
 }
 if(argc!=3 && argc!=6) throw std::runtime_error("use --help");
 const bool enable=argc==6;
 if(enable && (std::string(argv[3])!="--enable-production" || std::string(argv[4])!="--emergency-stop-file")) throw std::runtime_error("explicit activation and stop path required");
 const std::filesystem::path path=argv[1]; const auto frozen=bytes(path); config=loadCommonWorldActivationConfig(path);
 if(!config.valid || config.mode!=CommonWorldExecutionMode::Production) throw std::runtime_error("production configuration invalid");
 if(!config.enabled) { std::cout<<commonWorldRuntimeDiagnostics(runtime.snapshot(),config,commonWorldBuildCommit())<<'\n'; return 0; }
 std::filesystem::path stop;
 if(enable) {
  stop=std::filesystem::absolute(argv[5]);
  if(!std::filesystem::is_directory(stop.parent_path())) throw std::runtime_error("stop parent unavailable");
 }
 const auto guard=[&] {
  try {
   if(bytes(path)!=frozen || (enable && std::filesystem::exists(stop))) throw std::runtime_error("config changed or emergency sentinel present");
  } catch(...) { emergencyRevokeCommonWorld(runtime,mb::monotonicNs()); throw; }
 };
 guard(); std::ifstream input(argv[2],std::ios::binary); std::string line;
 if(!input || !std::getline(input,line)) throw std::runtime_error("authorized source bootstrap required");
 if(runtime.prepare(path,decode(line),mb::monotonicNs())!="ready-inactive") throw std::runtime_error("5AG preparation failed");
 guard();
 if(!enable) { std::cout<<commonWorldRuntimeDiagnostics(runtime.snapshot(),config,commonWorldBuildCommit())<<'\n'; return 0; }
 const auto expected=runtime.snapshot().sender->ingress.runtime.world->world;
 if(runtime.activate(expected,mb::monotonicNs())!="active") throw std::runtime_error("5AG activation denied");
 std::cout<<commonWorldRuntimeDiagnostics(runtime.snapshot(),config,commonWorldBuildCommit())<<std::endl;
 const auto drain=[&] {
  while(runtime.snapshot().sender->queued) { guard(); if(runtime.pump().status!="local-accepted") throw std::runtime_error("local submission failed"); }
 };
 drain();
 while(std::getline(input,line)) {
  guard(); runtime.receive(decode(line),mb::monotonicNs());
  if(runtime.status()!=CommonWorldActivationStatus::Active) throw std::runtime_error("source authority retired"); drain();
 }
 if(input.bad()) throw std::runtime_error("source read failed"); runtime.shutdown(mb::monotonicNs());
 std::cout<<commonWorldRuntimeDiagnostics(runtime.snapshot(),config,commonWorldBuildCommit())<<'\n'; return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<" (fail-closed shutdown)\n"; return 1; }
