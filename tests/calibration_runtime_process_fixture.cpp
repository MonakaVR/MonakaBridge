// Separate test executable: same runtime provider/codec/owner/session/solver,
// with fake public SDK and in-memory datagrams. Never packaged as a helper.
#include "calibration_runtime_fake.hpp"
#include "calibration_b4_fixture.hpp"
#include <iostream>
namespace lc=mb::local_calibration;
int main(int argc,char** argv)try{
 if(argc==2&&std::string(argv[1])=="--local-api-version"){std::cout<<lc::productionCalibrationVersion().dump()<<'\n';return 0;}
 if(argc<2||argc>3)throw std::runtime_error("test-only fixture directory required");
 const bool local=argc==2&&std::string(argv[1])=="--local-api",seed=argc==2&&std::string(argv[1])=="--fixture-seed";
 auto environment=[](const wchar_t* name,bool required){wchar_t value[32768];DWORD n=GetEnvironmentVariableW(name,value,32768);if(!n){if(required)throw std::runtime_error("test-only fixture environment missing");return std::wstring{};}CHECK(n<32768);return std::wstring(value,n);};
 auto base=local||seed?std::filesystem::path(environment(L"MONAKA_CALIBRATION_TEST_ROOT",true)):std::filesystem::u8path(argv[1]);
 fixture::Run run(std::filesystem::absolute(base));
 runtime_fixture::State state(run.time);auto provider=runtime_fixture::provider(state,true);
 wchar_t self[32768];auto n=GetModuleFileNameW(nullptr,self,32768);CHECK(n&&n<32768);
 run.env.helperPath=std::filesystem::path(self).u8string();run.env.helperSha256=lc::sha256(mb::readConfigBytes(std::filesystem::path(self)));
 std::string mode=local||seed?std::filesystem::path(environment(L"MONAKA_CALIBRATION_TEST_MODE",false)).u8string():(argc==3?argv[2]:"");
 std::atomic<bool> blockReference{false};
 if(mode=="b4-static") {state.extrinsic={};state.poseHook=[&]{state.time+=8000000;};}
 else if(mode=="b4-approval"){state.poseHook=[&]{state.time+=8000000;};run.env.outputApprovalAuthority=std::make_shared<b4_fixture::Authority>();}
 else if(mode=="b4-unadopted")state.poseHook=[&]{state.time+=8000000;};
 else if(mode=="post-replace-exit")run.env.afterReplace=[](){ExitProcess(73);};
 else if(mode=="post-replace-unknown")run.env.afterReplace=[](){throw std::runtime_error("test-only lost commit reply");};
 else if(mode=="slow-commit")run.env.transactionHook=[](const char* stage){if(std::string(stage)=="before-replace")Sleep(200);};
 else if(mode=="init-failure")state.failInit=true;
 else if(mode=="no-input")state.generate=false;
 else if(mode=="busy-discover")state.poseHook=[&](){std::ofstream marker(base/"busy-discover-entered.fixture");marker<<"owned test SDK call entered";marker.close();Sleep(INFINITE);};
 else if(mode=="busy-record"){
  state.poseHook=[&](){if(blockReference.load()){std::ofstream marker(base/"busy-record-entered.fixture");marker<<"owned test provider call entered";marker.close();Sleep(INFINITE);}};
  run.env.ioHook=[&](const char* point){if(std::string(point)=="begin-barrier-acked")blockReference=true;};
 }
 else if(mode=="busy-solve")run.env.ioHook=[&](const char* point){if(std::string(point)=="solve-before"){std::ofstream marker(base/"busy-solve-entered.fixture");marker<<"owned test solver entered";marker.close();Sleep(INFINITE);}};
 else if(mode=="busy-commit")run.env.transactionHook=[&](const char* stage){if(std::string(stage)=="before-replace"){{std::ofstream marker(run.root/"commit-entered.fixture");marker<<"owned test fixture commit entered";}Sleep(INFINITE);}};
 else if(!mode.empty())throw std::runtime_error("unknown test-only fixture mode");
 if(!local)std::cout<<lc::Json{{"test_only_fixture_open",run.open()}}.dump()<<std::endl;
 if(seed)return 0;
 auto code=lc::runLocalApi(*provider,run.env);
 std::cerr<<lc::Json{{"test_only_runtime_fixture",true},{"init_calls",state.initCalls},{"shutdown_calls",state.shutdownCalls},{"bind_calls",state.bindCalls},{"transport_closes",state.transportCloses},{"real_sdk_execution","NOT_RUN"},{"real_socket_execution","NOT_RUN"}}.dump()<<'\n';return code;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
