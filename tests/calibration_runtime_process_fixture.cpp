// Separate test executable: same runtime provider/codec/owner/session/solver,
// with fake public SDK and in-memory datagrams. Never packaged as a helper.
#include "calibration_runtime_fake.hpp"
#include <iostream>
namespace lc=mb::local_calibration;
int main(int argc,char** argv)try{
 if(argc==2&&std::string(argv[1])=="--local-api-version"){std::cout<<lc::productionCalibrationVersion().dump()<<'\n';return 0;}
 if(argc<2||argc>3)throw std::runtime_error("test-only fixture directory required");
 fixture::Run run(std::filesystem::absolute(std::filesystem::u8path(argv[1])));
 runtime_fixture::State state(run.time);auto provider=runtime_fixture::provider(state,true);
 wchar_t self[32768];auto n=GetModuleFileNameW(nullptr,self,32768);CHECK(n&&n<32768);
 run.env.helperPath=std::filesystem::path(self).u8string();run.env.helperSha256=lc::sha256(mb::readConfigBytes(std::filesystem::path(self)));
 std::string mode=argc==3?argv[2]:"";
 if(mode=="post-replace-exit")run.env.afterReplace=[](){ExitProcess(73);};
 else if(mode=="post-replace-unknown")run.env.afterReplace=[](){throw std::runtime_error("test-only lost commit reply");};
 else if(mode=="slow-commit")run.env.transactionHook=[](const char* stage){if(std::string(stage)=="before-replace")Sleep(200);};
 else if(mode=="init-failure")state.failInit=true;
 else if(mode=="no-input")state.generate=false;
 else if(!mode.empty())throw std::runtime_error("unknown test-only fixture mode");
 std::cout<<lc::Json{{"test_only_fixture_open",run.open()}}.dump()<<std::endl;
 auto code=lc::runLocalApi(*provider,run.env);
 std::cerr<<lc::Json{{"test_only_runtime_fixture",true},{"init_calls",state.initCalls},{"shutdown_calls",state.shutdownCalls},{"bind_calls",state.bindCalls},{"transport_closes",state.transportCloses},{"real_sdk_execution","NOT_RUN"},{"real_socket_execution","NOT_RUN"}}.dump()<<'\n';return code;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
