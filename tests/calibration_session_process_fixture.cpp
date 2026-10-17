#include "calibration_fixture_provider.hpp"
#define NOMINMAX
#include <windows.h>
#include <iostream>
int main(int argc,char** argv)try{
 if(argc<2||argc>3)throw std::runtime_error("test fixture directory required");
 fixture::Run run(std::filesystem::absolute(std::filesystem::u8path(argv[1])));
 std::string fault=argc==3?argv[2]:"";
 if(fault=="slow")run.provider.captureHook=[](int){Sleep(5);};
 else if(fault=="post-replace-unknown")run.env.afterReplace=[](){throw std::runtime_error("test-only post-replace lost outcome");};
 else if(fault=="post-replace-exit")run.env.afterReplace=[](){ExitProcess(73);};
 else if(fault=="slow-commit")run.env.transactionHook=[&](const char* stage){if(std::string(stage)=="before-replace"){{std::ofstream f(run.root/"commit-entered.fixture");f<<"owned fixture committing";}Sleep(200);}};
 else if(!fault.empty())throw std::runtime_error("unknown fixture fault");
 std::cout<<fixture::lc::Json{{"test_only_fixture_open",run.open()}}.dump()<<std::endl;
 return fixture::lc::runLocalApi(run.provider,run.env);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
