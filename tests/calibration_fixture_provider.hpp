#pragma once
#include "helpers.hpp"
#include "monaka_bridge/calibration_session.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include <fstream>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace fixture {
namespace lc=mb::local_calibration;
inline mb::Quat axis(int i,double a){mb::Quat q{0,0,0,std::cos(a/2)};q[i]=std::sin(a/2);return q;}
// Test producer pacing, independent of the synthetic 20ms sample timestamps.
// A high-resolution timer avoids Windows coarse Sleep(1) stretching 501 inputs
// past fixture gates. Fast/blocked-consumer overflow has its own explicit test.
struct CapturePacer {
 HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
 CapturePacer(){CHECK(timer!=nullptr);}
 ~CapturePacer(){CloseHandle(timer);}
 void wait(){LARGE_INTEGER due;due.QuadPart=-10000;CHECK(SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE));CHECK(WaitForSingleObject(timer,1000)==WAIT_OBJECT_0);}
};
struct Provider:lc::CaptureProvider {
 std::atomic<std::int64_t>& time;mutable std::recursive_mutex fixtureMutex;lc::ProviderBatch latest;std::function<void(int)> captureHook;std::function<void(lc::ProviderBatch&)> guardHook;std::function<void()> discoverHook;
 int captures=0,discoveries=0;bool support=true,staticMotion=false;
 mutable int configurationChecks=0;bool failConfigure=false;
 std::function<void(const lc::ProviderOpenOptions&)> configurationHook;
 explicit Provider(std::atomic<std::int64_t>& t):time(t){
  latest.identity={"pico","device",S1,S1,"native","fixture-native","fixture-peer",1,0,{},"fixture-reference","left","fixture-standing",1};
  auto p=sample();p.orientation=mb::Quat{0,0,0,1};p.timestamp_ns=time-2000000;p.sent_at_ns=time;
  latest.frames.push_back({p,"fixture-peer",time});latest.referenceReceiptNs=time;latest.referenceValid=true;
 }
 bool supported()const override{return support;}
 template<class F>void edit(F f){std::lock_guard<std::recursive_mutex> lock(fixtureMutex);f();}
 lc::ProviderBatch discover(const std::atomic<bool>&)override{std::lock_guard<std::recursive_mutex> lock(fixtureMutex);++discoveries;if(discoverHook)discoverHook();return latest;}
 void capture(const std::function<void(const lc::ProviderBatch&)>& sink,const std::atomic<bool>& cancel)override{
  ++captures;CapturePacer pacer;const mb::Rigid W{mb::normalized({.2,-.3,.1,.9}),{1,2,-.5}},E{mb::normalized({.3,.1,-.2,.9}),{.1,.05,-.2}};
  for(int i=0;i<=500&&!cancel;++i){
   lc::ProviderBatch batch;{std::lock_guard<std::recursive_mutex> lock(fixtureMutex);
   time+=20000000;double t=i*.02;
   mb::Rigid source{mb::mul(mb::mul(axis(0,.8*std::sin(1.3*t)),axis(1,.9*std::sin(.9*t))),axis(2,.7*std::sin(1.7*t))),{.45*std::sin(t),.35*std::sin(1.4*t),.30*std::sin(.7*t)}};
   if(staticMotion)source={};
   auto p=sample("pico",i+1);p.coordinate_space={latest.identity.space,latest.identity.convention,latest.identity.revision};p.timestamp_ns=time-2000000;p.sent_at_ns=time;p.position=source.translation;p.orientation=source.rotation;
   latest.frames={{p,"fixture-peer",time}};latest.references={{time-2000000,mb::composeRigid(mb::composeRigid(W,source),E),true}};latest.referenceReceiptNs=time;
   if(captureHook)captureHook(i);batch=latest;}sink(batch);pacer.wait();
  }std::lock_guard<std::recursive_mutex> lock(fixtureMutex);latest.references.clear();
 }
 lc::ProviderBatch guard()override{std::lock_guard<std::recursive_mutex> lock(fixtureMutex);auto b=latest;b.references.clear();if(guardHook)guardHook(b);return b;}
 void fresh(){std::lock_guard<std::recursive_mutex> lock(fixtureMutex);time+=1000000;auto p=std::get<mb::c1::TrackerObservation>(latest.frames.front().envelope);++p.sequence;p.timestamp_ns=time-2000000;p.sent_at_ns=time;latest.frames={{p,"fixture-peer",time}};latest.referenceReceiptNs=time;latest.referenceValid=true;}
protected:
 void validateConfiguration(const lc::ProviderOpenOptions& options)const override{
  ++configurationChecks;if(configurationHook)configurationHook(options);
  if(failConfigure)throw std::runtime_error("FIXTURE_CONFIGURE_REJECTED");
 }
};
struct Run {
 std::filesystem::path root,path;std::atomic<std::int64_t> time{100*lc::second};Provider provider{time};lc::SessionEnvironment env;
 std::unique_ptr<lc::CalibrationSession> session;std::string original;int request=0;
 explicit Run(std::filesystem::path base){
  root=base/lc::opaqueId().substr(0,16);std::filesystem::create_directories(root);path=root/"config.json";
  auto c=config();c.profiles.at("pico").quaternionAxes={1,2,3,4};c.bindings.at({"pico","device"}).spaceApproved=false;
  auto b=c.bindings.at({"pico","device"});b.device="sibling";b.tracker="sibling";c.bindings[{b.source,b.device}]=b;
  for(auto& [key,contract]:c.contracts)contract.inputDeclaration=contract.outputDeclaration={"position","orientation"};c.contracts[{"pico","sibling"}]=c.contracts.at({"pico","device"});
  mb::saveConfig(path,c);original=mb::readConfigBytes(path);auto j=lc::Json::parse(original);j["unknown_extension"]={{"kept",{1,2,3}}};original=j.dump();{std::ofstream f(path,std::ios::binary|std::ios::trunc);f<<original;}
  auto helper=root/"helper.fixture";{std::ofstream f(helper);f<<"fixture helper only";}
  env.draftRoot=root/"drafts";env.helperPath=helper.u8string();env.helperSha256=lc::sha256(mb::readConfigBytes(helper));env.clock=[this]{return time.load();};
  reset();
 }
 void reset(){session=std::make_unique<lc::CalibrationSession>(provider,env);}
 lc::Json command(const std::string& op){lc::Json j={{"api_version",1},{"request_id",std::to_string(++request)},{"op",op}};if(op!="open"){j["session_id"]=session->sessionId();j["run_id"]=session->runId();}return j;}
 lc::Json open(){auto j=command("open");j.update({{"config_path",path.u8string()},{"source","pico"},{"device","device"},{"tracker","logical-pico"},{"input_space","native"},{"input_revision",1},{"readonly",false},{"bridge_id","bridge-installation"},{"installation_path",root.u8string()},{"helper_path",env.helperPath},{"helper_sha256",env.helperSha256},{"association","operator_declared_process_owner_unverified"},{"mirror_address","127.0.0.1"},{"mirror_port",49123},{"reference_role","left"}});return j;}
 lc::Json execute(const lc::Json& j){return session->execute(lc::parseRequest(j.dump()));}
 lc::Json confirmed(){CHECK(execute(open()).at("status")=="OPEN");CHECK(execute(command("discover")).at("status")=="DISCOVERED");auto reply=execute(command("confirm_target"));CHECK(reply.at("status")=="CONFIRMED");return reply;}
 lc::Json result(){auto c=confirmed();auto j=command("begin");j["confirmation_token"]=c.at("confirmation_token");auto r=execute(j);if(r.at("status")!="RESULT_READY")throw std::runtime_error(r.dump());return r;}
 lc::Json prepared(){auto r=result();auto j=command("prepare_commit");j["result_id"]=r.at("result").at("result_id");auto p=execute(j);if(p.at("status")!="PREPARED")throw std::runtime_error(p.dump());return p;}
 lc::Json commit(const lc::Json& p){auto j=command("commit");for(auto f:{"result_id","review_token","candidate_sha256"})j[f]=p.at(f);return j;}
};
} // namespace fixture
