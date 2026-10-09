#include "monaka_bridge/common_world_production_activation.hpp"
#include "monaka_bridge/network.hpp"
#include <fstream>
#include <iostream>

using namespace monaka_bridge;
namespace w=authority_wire;
namespace {
w::Envelope decode(const std::string& line) {
 w::Envelope value; w::Error error;
 if(!w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(line.data()),line.size(),value,error)) throw std::runtime_error("invalid authorized source stream envelope");
 return value;
}
}
// Dedicated opt-in host for the typed source-stream boundary. No installer, service,
// driver or legacy Bridge routing changes. The source file is an explicitly selected
// trusted adapter stream; reading it never discovers devices or authenticates a peer.
int main(int argc,char** argv) try {
 if(argc==1) { CommonWorldProductionActivation runtime; std::cout<<commonWorldActivationStatusName(runtime.snapshot().status)<<'\n'; return 0; }
 if(argc==2 && std::string(argv[1])=="--help") {
  std::cout<<"monaka_common_world_runtime CONFIG AUTHORIZED_SOURCE_JSONL [--enable-production]\n"
   "No config: DisabledByDefault. Config alone: prepare only, no transport activation.\n"; return 0;
 }
 if(argc<3 || argc>4 || (argc==4 && std::string(argv[3])!="--enable-production")) throw std::invalid_argument("use --help");
 const std::filesystem::path path=argv[1]; const auto config=loadCommonWorldActivationConfig(path);
 if(!config.valid) throw std::runtime_error(config.error);
 if(!config.enabled) { std::cout<<"DisabledByDefault\n"; return 0; }
 if(config.mode!=CommonWorldExecutionMode::Production) throw std::runtime_error("production host rejects test mode");
 std::ifstream input(argv[2],std::ios::binary); std::string line;
 if(!input || !std::getline(input,line)) throw std::runtime_error("explicit authorized source bootstrap required");
 CommonWorldProductionActivation runtime;
 const auto prepared=runtime.prepare(path,decode(line),mb::monotonicNs());
 if(prepared!="ready-inactive") throw std::runtime_error(prepared+": "+runtime.snapshot().reason);
 std::cout<<"ReadyButInactive\n";
 if(argc==3) return 0;
 const auto expected=runtime.snapshot().sender->ingress.runtime.world->world;
 if(runtime.activate(expected,mb::monotonicNs())!="active") throw std::runtime_error(runtime.snapshot().reason);
 const auto drain=[&] {
  while(runtime.snapshot().sender->queued) {
   const auto result=runtime.pump();
   if(result.status!="local-accepted") throw std::runtime_error("local submission failed: "+result.status);
  }
 };
 drain();
 while(std::getline(input,line)) {
  const auto received=runtime.receive(decode(line),mb::monotonicNs());
  if(runtime.snapshot().status!=CommonWorldActivationStatus::Active) throw std::runtime_error("source authority retired: "+received.source.status);
  drain();
 }
 if(input.bad()) throw std::runtime_error("source stream read failed");
 runtime.shutdown(mb::monotonicNs()); std::cout<<"Shutdown (ordered best-effort local submission)\n"; return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
