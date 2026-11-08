// Completed-result save through the real session/monitor/transaction, with a
// synthetic provider only. No SDK, socket, helper or physical device execution.
#include "calibration_fixture_provider.hpp"
#include "monaka_bridge/calibration_capture_monitor.hpp"
#include <iostream>

namespace lc=mb::local_calibration;
namespace fs=std::filesystem;
namespace {
int groups=0;
void pass(const char* message){++groups;std::cout<<"PASS "<<message<<std::endl;}
void require(const lc::Json& response,const char* status){if(response.at("status")!=status)throw std::runtime_error(response.dump());}
lc::Json prepare(fixture::Run& run,const lc::Json& result){auto command=run.command("prepare_commit");command["result_id"]=result.at("result").at("result_id");auto response=run.execute(command);require(response,"PREPARED");return response;}
void unchanged(const fixture::Run& run){CHECK(mb::readConfigBytes(run.path)==run.original&&run.session->commitCalls()==0&&run.provider.captures==1&&run.session->solveCalls()==1);}
void saveOnce(fixture::Run& run,const lc::Json& result){
 auto prepared=prepare(run,result);const auto candidate=mb::readConfigBytes(fs::u8path(prepared.at("candidate_path").get<std::string>()));
 CHECK(lc::sha256(candidate)==prepared.at("candidate_sha256"));auto commit=run.commit(prepared);require(run.execute(commit),"SAVED");
 CHECK(mb::readConfigBytes(run.path)==candidate&&run.provider.captures==1&&run.session->solveCalls()==1&&run.session->commitCalls()==1);
 auto replay=run.execute(commit);require(replay,"SAVED");CHECK(replay.at("replayed")==true&&run.session->commitCalls()==1);
}
template<class F>void until(F ready){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!ready()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
}

int main(int argc,char** argv)try{
 CHECK(argc==2);const auto base=fs::absolute(fs::u8path(argv[1])).lexically_normal();fs::create_directories(base);
 {
  fixture::Run run(base);auto result=run.result();const auto before=run.session->captureSnapshot();
  run.time+=100000000;CHECK(run.time-before.lastValidPose>50000000&&run.time-before.lastValidReference>50000000);
  saveOnce(run,result);CHECK(run.session->captureSnapshot().lastValidPose==before.lastValidPose);
  pass("final immutable result saves exact candidate once with same-identity source/reference 100ms old; no rerecord");
 }
 {
  fixture::Run run(base);auto result=run.result();const auto before=run.session->captureSnapshot();
  run.provider.edit([&]{
   run.time+=100000000;auto p=std::get<mb::c1::TrackerObservation>(run.provider.latest.frames.front().envelope);
   ++p.sequence;p.timestamp_ns=run.time-2000000;p.sent_at_ns=run.time;p.validity={false,false};
   p.modality="none";p.tracking_state="lost";p.position.reset();p.orientation.reset();p.orientation_evidence="none";
   run.provider.latest.frames={{p,"fixture-peer",run.time}};run.provider.latest.referenceReceiptNs=run.time;run.provider.latest.referenceValid=false;
  });
  saveOnce(run,result);const auto after=run.session->captureSnapshot();
  CHECK(!after.poseValid&&!after.referenceValid&&after.lastValidPose==before.lastValidPose&&after.lastValidReference==before.lastValidReference&&after.fault.empty());
  pass("brief instantaneous invalid source/reference within retained 1s liveness does not invalidate finalized measurement save");
 }
 {
  for(int mode=0;mode<3;++mode){
   fixture::Run run(base);bool armed=false;
   run.env.ioHook=[&](const char* point){if(mode==2&&armed&&std::string(point)=="precommit-barrier-acked"){
    run.provider.edit([&]{run.provider.latest.eventOverflow=true;});until([&]{return !run.session->captureSnapshot().fault.empty();});
   }};
   run.reset();auto result=run.result();auto prepared=prepare(run,result);run.time+=100000000;
   run.provider.edit([&]{if(mode==0)run.provider.latest.identity.session=S2;if(mode==1)++run.provider.latest.identity.referenceGeneration;});armed=true;
   require(run.execute(run.commit(prepared)),"REJECTED");auto snapshot=run.session->captureSnapshot();
   CHECK(!snapshot.fault.empty()&&snapshot.invalidationEpoch>0);unchanged(run);
   run.provider.edit([&]{run.provider.latest.identity.session=S1;run.provider.latest.identity.referenceGeneration=1;run.provider.latest.eventOverflow=false;});
   require(run.execute(run.commit(prepared)),"REJECTED");unchanged(run);
  }
  pass("source session/reference generation changes and post-ACK invalidation epoch remain sticky save refusals");
 }
 {
  for(int mode=0;mode<3;++mode){
   fixture::Run run(base);bool armed=false;const auto concurrent=run.original+" \n";
   if(mode==2){run.env.transactionHook=[&](const char* point){if(armed&&std::string(point)=="locked"){std::ofstream file(run.path,std::ios::binary|std::ios::trunc);file<<concurrent;CHECK(bool(file));}};run.reset();}
   auto result=run.result();auto prepared=prepare(run,result);run.time+=100000000;
   if(mode<2){auto path=mode==0?run.path:fs::u8path(run.env.helperPath);std::ofstream file(path,std::ios::binary|std::ios::app);file<<" ";CHECK(bool(file));}
   armed=true;auto refused=run.execute(run.commit(prepared));
   if(mode==2){require(refused,"UNKNOWN");CHECK(run.session->commitCalls()==1&&mb::readConfigBytes(run.path)==concurrent);}
   else{require(refused,"REJECTED");CHECK(run.session->commitCalls()==0);if(mode==1)CHECK(mb::readConfigBytes(run.path)==run.original);}
   CHECK(run.provider.captures==1&&run.session->solveCalls()==1);
  }
  pass("exact config/helper guards and atomic CAS conflict preserve external bytes; no freshness relaxation of authority");
 }
 {
  fixture::Run run(base);auto result=run.result();auto prepared=prepare(run,result);run.time+=100000000;
  const auto candidate=fs::u8path(prepared.at("candidate_path").get<std::string>());{std::ofstream file(candidate,std::ios::binary|std::ios::app);file<<" ";CHECK(bool(file));}
  require(run.execute(run.commit(prepared)),"REJECTED");unchanged(run);
  pass("finalized native candidate hash/artifact tampering remains rejected before commit");
 }
 {
  {fixture::Run run(base);auto result=run.result();run.time+=lc::second;auto command=run.command("prepare_commit");command["result_id"]=result.at("result").at("result_id");require(run.execute(command),"REJECTED");unchanged(run);CHECK(!run.session->captureSnapshot().fault.empty());}
  {fixture::Run run(base);bool armed=false;run.env.ioHook=[&](const char* point){if(armed&&std::string(point)=="precommit-barrier-acked")run.time+=30*lc::second;};run.reset();auto result=run.result();auto prepared=prepare(run,result);armed=true;auto refused=run.execute(run.commit(prepared));require(refused,"REJECTED");CHECK(refused.at("reason")=="REVIEW_TOKEN_EXPIRED_REPREPARE");unchanged(run);}
  pass("existing 1s liveness and 30s review token budgets still refuse finalized-result save");
 }
 std::cout<<"PASS finalized calibration result save "<<groups<<" groups; synthetic provider/native session only; SDK/socket/HIL/CUTOVER NOT_RUN\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
