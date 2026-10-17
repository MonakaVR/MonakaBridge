#include "calibration_fixture_provider.hpp"
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <thread>
#include <algorithm>
namespace lc=mb::local_calibration;
int main(int argc,char** argv)try{
 if(argc<2||argc>3)throw std::runtime_error("test fixture directory required");
 fixture::Run run(std::filesystem::absolute(std::filesystem::u8path(argv[1])));
 std::string fault=argc==3?argv[2]:"";
 std::atomic<int> lines{0},writes{0},commands{0};std::atomic<std::int64_t> clock{run.time};
 auto mark=[&](const char* name){std::ofstream f(run.root/(std::string(name)+".fixture"));f<<name;};
 auto wait=[&](const char* name){auto end=lc::steadyNow()+10*lc::second;while(!std::filesystem::exists(run.root/(std::string(name)+".fixture"))&&lc::steadyNow()<end)Sleep(1);};
 if(fault.rfind("review-",0)==0){
  if(fault=="review-reader-ttl"||fault=="review-ttl-busy")run.env.clock=[&]{return clock.load();};
  run.env.ioHook=[&](const char* p){std::string point=p;
   if(point.rfind("pipe-",0)==0)mark(p);
   if(point=="reader-line-start"){
    int n=++lines;
    if((fault=="review-reader-failure"||fault=="review-reader-ttl")&&n==2){if(fault=="review-reader-ttl")clock=run.time+20*60*lc::second;mark("reader-paused");wait("release-reader");}
    if(fault=="review-reader-backpressure"&&n==10){mark("reader-paused");wait("release-reader");}
   }
   if(point=="worker-before-execute"){
    int n=++commands;
    if(fault=="review-reader-backpressure"&&n==2)wait("reader-paused");
    if(fault=="review-ttl-busy"&&n==2)clock=run.time+20*60*lc::second;
   }
   if(point=="writer-before-write"){
    int n=++writes;
    if(fault=="review-reader-failure"&&n==1){wait("reader-paused");wait("trigger-write");}
    if(fault=="review-reader-backpressure"&&n==2)wait("release-writer");
    if(fault=="review-writer-race"&&n==1){
     // Fill only this owned fixture's stdout, retaining the original wait mode.
     HANDLE pipe=GetStdHandle(STD_OUTPUT_HANDLE);DWORD original=0,mode=PIPE_NOWAIT,done=0;
     if(!GetNamedPipeHandleStateW(pipe,&original,nullptr,nullptr,nullptr,nullptr,0)||!SetNamedPipeHandleState(pipe,&mode,nullptr,nullptr))ExitProcess(81);
     char padding[256];std::fill(std::begin(padding),std::end(padding),' ');
     for(int i=0;i<4096;++i){if(!WriteFile(pipe,padding,sizeof(padding),&done,nullptr)||done==0)break;}
     mode=original&PIPE_NOWAIT;if(!SetNamedPipeHandleState(pipe,&mode,nullptr,nullptr))ExitProcess(82);
     mark("writer-paused");wait("release-writer");
    }
    if(fault=="review-writer-deadline"&&n==1){mark("writer-paused");wait("release-writer");}
   }
   if(point=="writer-failed")mark("writer-failed");
   if(point=="worker-stopped")mark("worker-stopped");
   if(point=="writer-stop-requested")mark("writer-stop-requested");
   if(point=="writer-finished")mark("writer-finished");
   if(point=="writer-deadline-expired")mark("writer-deadline-expired");
  };
 }
 else if(fault=="slow")run.provider.captureHook=[](int){Sleep(5);};
 else if(fault=="post-replace-unknown")run.env.afterReplace=[](){throw std::runtime_error("test-only post-replace lost outcome");};
 else if(fault=="post-replace-exit")run.env.afterReplace=[](){ExitProcess(73);};
 else if(fault=="slow-commit")run.env.transactionHook=[&](const char* stage){if(std::string(stage)=="before-replace"){{std::ofstream f(run.root/"commit-entered.fixture");f<<"owned fixture committing";}Sleep(200);}};
 else if(fault=="n0a-configure-fail")run.provider.failConfigure=true;
 else if(fault=="n0a-options"){}
 else if(!fault.empty())throw std::runtime_error("unknown fixture fault");
 std::cout<<fixture::lc::Json{{"test_only_fixture_open",run.open()}}.dump()<<std::endl;
 auto exit=fixture::lc::runLocalApi(run.provider,run.env);
 if(fault.rfind("n0a-",0)==0){auto o=run.provider.configuration();lc::Json proof={{"configuration_checks",run.provider.configurationChecks},{"configuration_present",bool(o)},{"captures",run.provider.captures},{"discoveries",run.provider.discoveries}};
  if(o){const auto& b=o->measurementTarget().target().binding;proof.update({{"source",b.source},{"device",b.device},{"tracker",b.tracker},{"readonly",o->readOnly()},{"declaration_present",bool(o->capabilityProfile())},{"mirror_port",o->mirrorPort()},{"reference_role",o->referenceRole()},{"config_sha256",o->configSha256()}});}
  std::cerr<<proof.dump()<<'\n';
 }return exit;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
