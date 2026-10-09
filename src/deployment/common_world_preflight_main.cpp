#include "monaka_bridge/common_world_deployment_readiness.hpp"
#include <iostream>
using namespace monaka_bridge;
int main(int argc,char** argv) try {
 if(argc==2 && std::string(argv[1])=="--build-identity") { std::cout<<commonWorldBuildCommit()<<'\n'; return 0; }
 if(argc==1) { CommonWorldDeploymentReadiness r; std::cout<<"NotConfigured (activation OFF; observation only)\n"; return 0; }
 if(argc<8 || argc>9 || std::string(argv[1])!="--preflight") {
  std::cerr<<"--preflight ARTIFACT CONFIG SOURCE_JSONL LOG_DIRECTORY REPORT_DIRECTORY EXPECTED_COMMIT [EXPECTED_RECOVERED_OWNER_EPOCH]\n"
   "Existing writable directories required. JSON stdout; no activation, persistent commit, endpoint contact or report overwrite.\n"; return 2;
 }
 CommonWorldPreflightRequest q{argv[2],argv[3],argv[4],argv[5],argv[6],argv[7],argc==9?argv[8]:""};
 CommonWorldDeploymentReadiness observer; const auto r=observer.preflight(q); std::cout<<r.json<<'\n';
 return r.state==CommonWorldReadinessState::ReadyForControlledActivation?0:1;
} catch(const std::exception&) { std::cerr<<"preflight host failure; activation OFF\n"; return 1; }
