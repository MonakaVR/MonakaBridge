#include "monaka_bridge/common_world_production_activation.hpp"
#include "../src/authority/common_world_facts.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <thread>

using namespace monaka_bridge;
namespace fs=std::filesystem; namespace w=authority_wire; namespace d=common_world_detail;
using J=nlohmann::json;
int cases=0,checks=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__)); } while(false)
template<class F> void run(const std::string& name,F f) { f(); ++cases; std::cout<<"PASS "<<name<<std::endl; }
struct Record {
 std::mutex mutex; std::optional<std::string> bytes;
 bool corrupt=false,readFail=false,writeFail=false;
};
struct Storage final:CommonWorldRevisionStorage {
 std::shared_ptr<Record> record;
 explicit Storage(std::shared_ptr<Record> r):record(std::move(r)) {}
 std::optional<std::string> readCommitted() override {
  std::lock_guard<std::mutex> lock(record->mutex);
  if(record->readFail) throw std::runtime_error("injected recovery read failure");
  if(record->corrupt) return "corrupted durable state";
  return record->bytes;
 }
 void atomicWrite(const std::string& bytes,const std::atomic_bool*) override {
  std::lock_guard<std::mutex> lock(record->mutex);
  if(record->writeFail) throw std::runtime_error("injected durable commit failure"); record->bytes=bytes;
 }
};
struct Sink {
 std::size_t connects=0,closes=0,calls=0;
 bool connected=false,connectFail=false,throwConnect=false,throwSubmit=false;
 DatagramSubmission mode=DatagramSubmission::Accepted;
 std::vector<std::string> frames;
};
struct DeterministicTransport final:CommonWorldDatagramTransport {
 std::shared_ptr<Sink> sink;
 explicit DeterministicTransport(std::shared_ptr<Sink> s):sink(std::move(s)) {}
 bool connect() override { ++sink->connects; if(sink->throwConnect) throw std::runtime_error("injected connect failure"); return sink->connected=!sink->connectFail; }
 void close() noexcept override { ++sink->closes; sink->connected=false; }
 DatagramSubmissionResult submit(std::string_view bytes) override {
  CHECK(sink->connected); ++sink->calls;
  if(sink->throwSubmit) throw std::runtime_error("injected submit exception");
  if(sink->mode==DatagramSubmission::Accepted || sink->mode==DatagramSubmission::Pending) sink->frames.emplace_back(bytes);
  return {sink->mode,sink->mode==DatagramSubmission::Accepted?bytes.size():0};
 }
};
// This friend registration exists only in the test target. It cannot alter status,
// eligibility, owner/receipt flags or gates; its dependencies replace OS/storage seams.
namespace monaka_bridge {
class CommonWorldActivationTestRegistration {
public:
 static std::unique_ptr<CommonWorldProductionActivation> make(std::shared_ptr<Record> record,std::shared_ptr<Sink> sink,
  std::function<void(CommonWorldStartupStage)> hook={},std::function<std::string()> epoch={},bool noTransport=false,
  std::function<std::unique_ptr<CommonWorldRevisionStorage>(const fs::path&)> storage={}) {
  CommonWorldProductionActivation::Dependencies deps; deps.mode=CommonWorldExecutionMode::Test;
  deps.storage=storage?std::move(storage):[record](const fs::path&) { return std::make_unique<Storage>(record); };
  deps.transport=[sink,noTransport](const CommonWorldActivationConfig& config)->std::unique_ptr<CommonWorldDatagramTransport> {
   CHECK(config.mode==CommonWorldExecutionMode::Test && config.transport=="deterministic" && config.endpoint=="test://sink");
   if(noTransport) return {}; return std::make_unique<DeterministicTransport>(sink);
  };
  auto counter=std::make_shared<int>(0);
  deps.freshEpoch=epoch?std::move(epoch):[counter] { return "activation-test-epoch-"+std::to_string(++*counter); };
  deps.beforeStage=std::move(hook);
  return std::unique_ptr<CommonWorldProductionActivation>(new CommonWorldProductionActivation(std::move(deps)));
 }
};
}
w::Envelope decode(const std::string& line) { w::Envelope e; w::Error err;
 CHECK(w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(line.data()),line.size(),e,err)); return e; }
std::uint64_t bits(double v) { std::uint64_t b; std::memcpy(&b,&v,sizeof(b)); return b; }
struct Fixture {
 w::TrustedHmdSourceAuthority authority; w::TrustedHmdSourcePose pose;
 Fixture() {
  std::ifstream input(std::string(ROOT_DIR)+"/third_party/monaka-protocol-v2/fixtures/v2.1/ordered-streams/source-normal.jsonl"); std::string line;
  CHECK(bool(std::getline(input,line))); authority=std::get<w::TrustedHmdSourceAuthority>(decode(line));
  CHECK(bool(std::getline(input,line))); pose=std::get<w::TrustedHmdSourcePose>(decode(line));
 }
};
struct Rig {
 Fixture f; fs::path path; J config;
 std::shared_ptr<Record> record=std::make_shared<Record>(); std::shared_ptr<Sink> sink=std::make_shared<Sink>();
 std::unique_ptr<CommonWorldProductionActivation> runtime;
 explicit Rig(const fs::path& dir,std::function<void(CommonWorldStartupStage)> hook={},bool noTransport=false) {
  fs::create_directories(dir); path=dir/"activation.json";
  config={{"enabled",true},{"mode","test"},{"transport","deterministic"},{"codec","monaka-wire-2.1"},{"endpoint","test://sink"},
   {"owner_id","synthetic-bridge"},{"world_space_id","common-A"},{"source_publisher_id",f.authority.publisher_id},
   {"revision_path",fs::absolute(dir/"world.rev").string()},{"peer_policy","explicit-authority-association-no-implicit-world-replacement"}};
  runtime=CommonWorldActivationTestRegistration::make(record,sink,hook,{},noTransport); save();
 }
 void save() { std::ofstream out(path); out<<config.dump(); CHECK(bool(out)); }
 void ready() { const auto connects=sink->connects,calls=sink->calls;
  CHECK(runtime->prepare(path,f.authority,2)=="ready-inactive"); CHECK(sink->connects==connects && sink->calls==calls); }
 w::CommonWorldReference world() { return runtime->snapshot().sender->ingress.runtime.world->world; }
 void active() { ready(); CHECK(runtime->activate(world(),3)=="active"); }
 void drain() { while(runtime->snapshot().sender->queued) CHECK(runtime->pump().status=="local-accepted"); }
 void handover() { CommonWorldRevisionStore store(std::make_unique<Storage>(record),{"synthetic-bridge","common-A",{},0}); store.acquireAuthorization("external-fresh-epoch"); }
};
struct Barrier {
 std::mutex mutex; std::condition_variable cv; bool entered=false,released=false;
 void block() { std::unique_lock<std::mutex> lock(mutex); entered=true; cv.notify_all(); CHECK(cv.wait_for(lock,std::chrono::seconds(10),[&] { return released; })); }
 void wait() { std::unique_lock<std::mutex> lock(mutex); CHECK(cv.wait_for(lock,std::chrono::seconds(5),[&] { return entered; })); }
 void release() { std::lock_guard<std::mutex> lock(mutex); released=true; cv.notify_all(); }
};
int main(int argc,char** argv) try {
 CHECK(argc==2); const auto root=fs::path(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()); fs::create_directories(root);
 Fixture fixture;
 for(const auto& bytes:{std::string(),std::string("{}"),std::string("{\"enabled\":false}")}) run("default OFF "+bytes,[&] {
  Rig r(root/("off-"+std::to_string(cases))); std::ofstream(r.path)<<bytes;
  CHECK(r.runtime->prepare(r.path,r.f.authority,0)=="disabled"); CHECK(r.runtime->snapshot().status==CommonWorldActivationStatus::DisabledByDefault);
  CHECK(r.runtime->activate({},1)=="not-ready" && r.runtime->pump().status=="activation-inactive"); CHECK(!r.record->bytes && r.sink->connects==0 && r.sink->calls==0);
 });
 run("missing config and process construction perform zero outbound IO",[&] {
  CommonWorldProductionActivation production; CHECK(production.snapshot().status==CommonWorldActivationStatus::DisabledByDefault);
  CHECK(production.prepare({},fixture.authority,0)=="disabled" && production.activate({},1)=="not-ready");
  CHECK(!production.snapshot().sender && production.pump().status=="activation-inactive");
 });
 const std::vector<std::pair<std::string,J>> invalid={
  {"transport","fake"},{"codec","binary32"},{"endpoint","udp://192.0.2.1:29811"},{"owner_id",""},{"world_space_id",""},
  {"source_publisher_id","bad\nidentity"},{"queue_capacity",1},{"queue_capacity",4097},{"queue_capacity",2.5},
  {"retry_limit",0},{"retry_limit",33},{"retry_limit",-1},{"peer_policy","implicit-world-replacement"},{"mode","auto"},{"enabled","true"}};
 for(const auto& [key,value]:invalid) run("invalid config fail closed "+key+" "+value.dump(),[&] {
  Rig r(root/("invalid-"+std::to_string(cases))); r.config[key]=value; r.save();
  CHECK(r.runtime->prepare(r.path,r.f.authority,0)=="startup-failed"); CHECK(r.runtime->snapshot().status==CommonWorldActivationStatus::ConfigInvalid);
  CHECK(r.runtime->snapshot().stages.size()==2 && !r.record->bytes && r.sink->connects==0 && r.sink->calls==0);
 });
 for(const auto& key:{"endpoint","owner_id","world_space_id","source_publisher_id","revision_path","mode","codec","peer_policy"}) run(std::string("missing required config ")+key,[&] {
  Rig r(root/("missing-"+std::to_string(cases))); r.config.erase(key); r.save();
  CHECK(r.runtime->prepare(r.path,r.f.authority,0)=="startup-failed" && !r.runtime->snapshot().sender); CHECK(!r.record->bytes && !r.sink->connects);
 });
 for(const auto& bad:{"{", "[]", "{\"enabled\":true,\"enabled\":false}", "{\"enabled\":false,\"typo\":true}"}) run(std::string("malformed/duplicate config ")+bad,[&] {
  Rig r(root/("syntax-"+std::to_string(cases))); std::ofstream(r.path)<<bad;
  CHECK(r.runtime->prepare(r.path,r.f.authority,0)=="startup-failed" && !r.sink->connects && !r.record->bytes);
 });
 run("literal production endpoint validation has no socket fallback",[&] {
  Rig r(root/"production-validation"); r.config["mode"]="production"; r.config["transport"]="udp-ipv4"; r.config["endpoint"]="udp://192.0.2.10:30111";
  CHECK(parseCommonWorldActivationConfig(r.config.dump(),r.path).valid);
  for(const auto& endpoint:{"udp://127.0.0.1:30111","udp://localhost:30111","udp://0.0.0.0:30111","udp://224.0.0.1:30111",
   "udp://169.254.1.2:30111","udp://192.0.2.255:30111","udp://192.0.02.10:30111","udp://192.0.2.10:0","udp://192.0.2.10:65536",
   "udp://192.0.2.10","udp://1.2.3:45","udp://1.2.3.4.5:45","udp://256.2.3.4:45","tcp://192.0.2.10:45"}) {
   r.config["endpoint"]=endpoint; const auto c=parseCommonWorldActivationConfig(r.config.dump(),r.path); CHECK(!c.valid && !c.enabled);
  }
 });
 run("production registry rejects test mode; test registry rejects production endpoint before recovery",[&] {
  Rig r(root/"separation"); CommonWorldProductionActivation production;
  CHECK(production.prepare(r.path,r.f.authority,0)=="startup-failed"); CHECK(production.snapshot().stages.size()==2);
  r.config["mode"]="production"; r.config["transport"]="udp-ipv4"; r.config["endpoint"]="udp://192.0.2.10:30111"; r.save();
  CHECK(r.runtime->prepare(r.path,r.f.authority,0)=="startup-failed"); CHECK(!r.record->bytes && !r.sink->connects);
  r.config["transport"]="deterministic"; CHECK(!parseCommonWorldActivationConfig(r.config.dump(),r.path).valid);
 });
 run("real production registration prepares native durable state but never connects without explicit enable",[&] {
  Rig r(root/"production-inactive"); r.config["mode"]="production"; r.config["transport"]="udp-ipv4"; r.config["endpoint"]="udp://192.0.2.10:30111"; r.save();
  CommonWorldProductionActivation production; const auto prepared=production.prepare(r.path,r.f.authority,1);
  if(prepared!="ready-inactive") throw std::runtime_error("production preparation: "+production.snapshot().reason);
  const auto s=production.snapshot(); CHECK(s.recoveryCompleted && s.sender && !s.sender->connected && !s.sender->enabled && !s.sender->queued);
  auto foreign=s.sender->ingress.runtime.world->world; foreign.owner_id="foreign";
  CHECK(production.activate(foreign,2)=="world-identity-mismatch"); CHECK(production.pump().status=="activation-inactive");
  CHECK(!production.snapshot().sender->connected); production.shutdown();
 });
 for(int value=0;value<=8;++value) run("startup stage failure injection "+std::to_string(value),[&,value] {
  const auto point=static_cast<CommonWorldStartupStage>(value);
  Rig r(root/("stage-"+std::to_string(value)),[point](auto stage) { if(stage==point) throw std::runtime_error("injected startup stage failure"); });
  auto result=r.runtime->prepare(r.path,r.f.authority,1);
  if(value<7) { CHECK(result=="startup-failed"); CHECK(r.runtime->snapshot().stages.size()==static_cast<std::size_t>(value+1)); }
  else { CHECK(result=="ready-inactive"); CHECK(r.runtime->activate(r.world(),2)=="activation-failed"); CHECK(r.runtime->snapshot().stages.size()==static_cast<std::size_t>(value+1)); }
  CHECK(!r.sink->connects && !r.sink->calls); CHECK(r.runtime->pump().status=="activation-inactive");
 });
 run("config load failure starts no validation or recovery",[&] {
  Rig r(root/"load-failure"); CHECK(r.runtime->prepare(root/"missing.json",r.f.authority,0)=="startup-failed");
  CHECK(r.runtime->snapshot().stages.size()==1 && !r.record->bytes && !r.sink->connects);
 });
 for(int failure=0;failure<3;++failure) run("native store/corrupt recovery/commit fail closed "+std::to_string(failure),[&,failure] {
  Rig r(root/("store-fail-"+std::to_string(failure))); if(failure==0)r.record->corrupt=true; if(failure==1)r.record->readFail=true; if(failure==2)r.record->writeFail=true;
  CHECK(r.runtime->prepare(r.path,r.f.authority,0)=="startup-failed"); CHECK(r.runtime->snapshot().status==CommonWorldActivationStatus::Faulted);
  CHECK(r.runtime->snapshot().stages.size()==static_cast<std::size_t>(failure==2?4:3)); CHECK(!r.sink->connects && !r.sink->calls);
 });
 run("missing transport and source identity cannot satisfy readiness",[&] {
  Rig r(root/"no-transport",{},true); CHECK(r.runtime->prepare(r.path,r.f.authority,0)=="startup-failed"); CHECK(r.runtime->snapshot().stages.size()==6 && !r.sink->connects);
  Rig wrong(root/"wrong-source"); wrong.f.authority.publisher_id="unapproved-publisher";
  CHECK(wrong.runtime->prepare(wrong.path,wrong.f.authority,0)=="startup-failed"); CHECK(wrong.runtime->snapshot().stages.size()==5 && !wrong.sink->connects);
 });
 run("activation requires owner and completed recovery, independent of flag",[&] {
  Barrier b; Rig r(root/"recovery-pending",[&](auto stage) { if(stage==CommonWorldStartupStage::Recovery)b.block(); });
  auto startup=std::async(std::launch::async,[&] { return r.runtime->prepare(r.path,r.f.authority,1); }); b.wait();
  CHECK(r.runtime->snapshot().status==CommonWorldActivationStatus::RecoveryPending && !r.runtime->snapshot().recoveryCompleted);
  CHECK(r.runtime->activate({},2)=="startup-in-progress" && r.sink->connects==0);
  r.runtime->deactivate(); b.release(); CHECK(startup.get()=="startup-retired"); CHECK(!r.runtime->snapshot().sender && !r.sink->connects);
 });
 run("precise startup order and explicit enable are necessary",[&] {
  Rig r(root/"order"); r.ready();
  CHECK(r.runtime->snapshot().status==CommonWorldActivationStatus::ReadyButInactive && r.runtime->snapshot().recoveryCompleted);
  CHECK(r.runtime->snapshot().stages.size()==7); for(int i=0;i<7;++i) CHECK(r.runtime->snapshot().stages[i]==static_cast<CommonWorldStartupStage>(i));
  CHECK(r.runtime->pump().status=="activation-inactive"); CHECK(r.runtime->activate(r.world(),3)=="active");
  CHECK(r.runtime->snapshot().stages.size()==9 && r.sink->connects==1 && r.sink->calls==0); r.drain(); CHECK(r.sink->frames.size()==2);
 });
 run("activate/deactivate/shutdown are idempotent and config cannot reconfigure running runtime",[&] {
  Rig r(root/"idempotent"); r.active(); const auto world=r.world(); const auto generation=r.runtime->snapshot().generation;
  CHECK(r.runtime->activate(world,4)=="already-active" && r.sink->connects==1 && r.runtime->snapshot().generation==generation);
  r.config["world_space_id"]="replacement"; r.save(); CHECK(r.runtime->prepare(r.path,r.f.authority,5)=="already-prepared");
  r.runtime->deactivate(); const auto retired=r.runtime->snapshot().generation; r.runtime->deactivate(); CHECK(r.runtime->snapshot().generation==retired);
  CHECK(r.runtime->snapshot().sender->queued==0 && !r.runtime->snapshot().sender->connected && r.runtime->pump().status=="activation-inactive");
  CHECK(r.runtime->activate(world,6)=="active" && r.sink->connects==2); r.runtime->shutdown(7); const auto shutdown=r.runtime->snapshot().generation;
  r.runtime->shutdown(8); r.runtime->deactivate(); CHECK(r.runtime->snapshot().generation==shutdown);
  CHECK(r.runtime->activate(world,9)=="shutdown" && r.runtime->prepare(r.path,r.f.authority,9)=="shutdown");
  CHECK(r.runtime->snapshot().sender->disposed && !r.runtime->snapshot().sender->ingress.ownerApproved && !r.runtime->snapshot().sender->ingress.sourceActive);
 });
 for(int mismatch=0;mismatch<5;++mismatch) run("exact active/durable world mismatch "+std::to_string(mismatch),[&,mismatch] {
  Rig r(root/("world-mismatch-"+std::to_string(mismatch))); r.ready(); auto expected=r.world();
  if(mismatch==0) expected.owner_id="foreign"; if(mismatch==1) expected.world_epoch="retired"; if(mismatch==2) expected.coordinate_space.id="other-world";
  if(mismatch==3) ++expected.coordinate_space.revision; if(mismatch==4) expected.coordinate_space.convention="wrong";
  CHECK(r.runtime->activate(expected,3)=="world-identity-mismatch" && !r.sink->connects && !r.sink->calls);
  CHECK(r.runtime->activate(r.world(),4)=="active"); CHECK(r.runtime->activate(expected,5)=="world-identity-mismatch" && r.sink->connects==1);
 });
 run("external stale epoch before activation denies endpoint construction side effect",[&] {
  Rig r(root/"epoch-before"); r.ready(); r.handover(); CHECK(r.runtime->activate(r.world(),3)=="stale-epoch-or-receipt"); CHECK(!r.sink->connects && !r.sink->calls);
 });
 run("owner/epoch revoke during activation before endpoint entry",[&] {
  Rig* raw=nullptr; Rig r(root/"epoch-during",[&](auto stage) { if(stage==CommonWorldStartupStage::TransportActivation) raw->handover(); }); raw=&r;
  r.ready(); CHECK(r.runtime->activate(r.world(),3)=="activation-failed"); CHECK(!r.sink->connects && !r.sink->calls && !r.runtime->snapshot().sender->enabled);
 });
 for(int action=0;action<3;++action) run("startup cancellation by deactivate/revoke/shutdown "+std::to_string(action),[&,action] {
  Barrier b; Rig r(root/("startup-race-"+std::to_string(action)),[&](auto stage) { if(stage==CommonWorldStartupStage::Gate)b.block(); });
  auto startup=std::async(std::launch::async,[&] { return r.runtime->prepare(r.path,r.f.authority,1); }); b.wait();
  if(action==0)r.runtime->deactivate(); if(action==1)r.runtime->revokeOwner(2); if(action==2)r.runtime->shutdown(2);
  b.release(); CHECK(startup.get()=="startup-retired"); CHECK(!r.runtime->snapshot().sender && !r.sink->connects && !r.sink->calls);
 });
 for(int action=0;action<3;++action) run("serialized activation race with deactivate/revoke/shutdown "+std::to_string(action),[&,action] {
  Barrier b; Rig r(root/("activation-race-"+std::to_string(action)),[&](auto stage) { if(stage==CommonWorldStartupStage::ExplicitEnable)b.block(); }); r.ready(); const auto expected=r.world();
  auto activation=std::async(std::launch::async,[&] { return r.runtime->activate(expected,3); }); b.wait();
  CHECK(r.runtime->status()==CommonWorldActivationStatus::Activating);
  std::promise<void> requested; auto retired=std::async(std::launch::async,[&] { requested.set_value(); if(action==0)r.runtime->deactivate(); if(action==1)r.runtime->revokeOwner(4); if(action==2)r.runtime->shutdown(4); });
  requested.get_future().get(); b.release(); CHECK(activation.get()=="active"); retired.get();
  CHECK(r.runtime->snapshot().status!=CommonWorldActivationStatus::Active && !r.sink->connected && !r.sink->calls);
  CHECK(!r.runtime->snapshot().sender->enabled && r.runtime->snapshot().sender->queued==0);
 });
 run("owner revoke immediately before send produces no outbound IO",[&] {
  Rig r(root/"before-send"); r.active(); const auto world=r.world(); r.runtime->revokeOwner(4);
  CHECK(r.runtime->pump().status=="activation-inactive" && !r.sink->calls); CHECK(r.runtime->activate(world,5)=="not-ready");
 });
 run("external epoch handover at final send fence retires publisher",[&] {
  Rig r(root/"send-epoch"); r.active(); r.handover(); CHECK(r.runtime->pump().status=="authorization-unavailable");
  CHECK(!r.sink->calls && r.runtime->snapshot().status==CommonWorldActivationStatus::Faulted && !r.sink->connected);
 });
 for(int action=0;action<3;++action) run("late transport completion after deactivate/revoke/shutdown "+std::to_string(action),[&,action] {
  Rig r(root/("completion-"+std::to_string(action))); r.active(); r.sink->mode=DatagramSubmission::Pending; auto pending=r.runtime->pump();
  CHECK(pending.status=="pending-local-completion" && pending.completion && r.sink->frames.size()==1);
  const auto count=r.sink->frames[0].size(); if(action==0)r.runtime->deactivate(); if(action==1)r.runtime->revokeOwner(4); if(action==2)r.runtime->shutdown(4);
  CHECK(r.runtime->complete(pending.completion,{DatagramSubmission::Accepted,count})=="stale-completion");
  CHECK(!r.runtime->snapshot().sender->lastLocalAcceptedSequence && r.runtime->snapshot().sender->queued==0);
 });
 run("late reconnect completion cannot pop fresh session head or reactivate",[&] {
  Rig r(root/"reconnect-completion"); r.active(); const auto world=r.world(); r.sink->mode=DatagramSubmission::Pending; auto old=r.runtime->pump();
  r.runtime->deactivate(); CHECK(r.runtime->activate(world,4)=="active"); const auto queued=r.runtime->snapshot().sender->queued;
  CHECK(r.runtime->complete(old.completion,{DatagramSubmission::Accepted,r.sink->frames[0].size()})=="stale-completion");
  CHECK(r.runtime->snapshot().sender->queued==queued && !r.runtime->snapshot().sender->lastLocalAcceptedSequence);
  r.sink->mode=DatagramSubmission::Accepted; r.drain(); CHECK(r.sink->frames.size()==3);
  CHECK(std::get<w::CommonWorldAuthorityPublication>(decode(r.sink->frames[0])).session_id!=std::get<w::CommonWorldAuthorityPublication>(decode(r.sink->frames[1])).session_id);
 });
 for(int failure=0;failure<3;++failure) run("transport connect/submit faults preserve durable state and owner "+std::to_string(failure),[&,failure] {
  Rig r(root/("transport-fault-"+std::to_string(failure))); r.ready(); const auto durable=*r.record->bytes,world=r.world().world_epoch;
  if(failure==0)r.sink->connectFail=true; if(failure==1)r.sink->throwConnect=true;
  if(failure<2)CHECK(r.runtime->activate(r.world(),3)=="activation-failed");
  else { CHECK(r.runtime->activate(r.world(),3)=="active"); r.sink->throwSubmit=true; CHECK(r.runtime->pump().status=="submission-exception-ambiguous"); }
  CHECK(*r.record->bytes==durable && r.world().world_epoch==world && r.runtime->snapshot().sender->ingress.ownerApproved);
  CHECK(!r.runtime->snapshot().sender->enabled && !r.sink->connected && r.runtime->snapshot().status==CommonWorldActivationStatus::Faulted);
 });
 run("durable corruption immediately before activation is fail closed",[&] {
  Rig r(root/"corrupt-late"); r.ready(); r.record->corrupt=true; CHECK(r.runtime->activate(r.world(),3)=="activation-failed"); CHECK(!r.sink->connects && !r.sink->calls);
 });
 run("source generation/world change cannot be implicitly committed or enabled",[&] {
  Rig r(root/"source-transition"); r.active(); auto next=r.f.authority; next.sequence=1; ++next.source_space.source_space_generation;
  const auto bytes=*r.record->bytes; const auto received=r.runtime->receive(next,4);
  CHECK(received.source.status=="authority"); CHECK(r.runtime->snapshot().status==CommonWorldActivationStatus::OwnerUnavailable);
  CHECK(r.runtime->snapshot().sender->queued==0 && !r.sink->calls && *r.record->bytes==bytes); CHECK(r.runtime->activate({},5)=="not-ready");
 });
 run("deactivated source callback cannot enqueue or send through a late pose",[&] {
  Rig r(root/"late-source"); r.active(); r.runtime->deactivate();
  CHECK(r.runtime->receive(r.f.pose,4).publication=="publisher-disabled");
  CHECK(r.runtime->snapshot().sender->queued==0 && !r.sink->calls && !r.sink->connected);
  r.runtime->shutdown(); CHECK(r.runtime->receive(r.f.pose,5).source.status=="shutdown");
 });
 run("stale source publisher identity cannot affect prepared world or publication",[&] {
  Rig r(root/"stale-source-identity"); r.active(); auto foreign=r.f.pose; foreign.publisher_id="unapproved-source";
  const auto world=r.world(); auto result=r.runtime->receive(foreign,4);
  CHECK(result.source.status!="pose" && !r.sink->calls);
  const auto snapshot=r.runtime->snapshot();
  CHECK(!snapshot.sender->ingress.runtime.world || d::world(snapshot.sender->ingress.runtime.world->world)==d::world(world));
 });
 run("restart keeps durable installation/space/high-water; old world and completion cannot activate",[&] {
  Rig r(root/"restart"); r.active(); const auto old=r.world(); r.sink->mode=DatagramSubmission::Pending; auto pending=r.runtime->pump(); r.runtime->shutdown(); r.runtime.reset();
  r.runtime=CommonWorldActivationTestRegistration::make(r.record,r.sink,{},[] { return "restart-fresh-epoch"; }); r.sink->mode=DatagramSubmission::Accepted; r.ready();
  const auto fresh=r.world(); CHECK(fresh.owner_id==old.owner_id && fresh.coordinate_space.id==old.coordinate_space.id && fresh.coordinate_space.revision>old.coordinate_space.revision && fresh.world_epoch!=old.world_epoch);
  CHECK(r.runtime->activate(old,3)=="world-identity-mismatch"); CHECK(r.runtime->activate(fresh,4)=="active");
  CHECK(r.runtime->complete(pending.completion,{DatagramSubmission::Accepted,1})=="stale-completion");
  CHECK(r.runtime->snapshot().sender->queued==2 && !r.runtime->snapshot().sender->lastLocalAcceptedSequence);
 });
 run("persistent identity/world mismatch rejects restart before owner/sender",[&] {
  Rig r(root/"restart-mismatch"); r.ready(); r.runtime.reset(); r.config["owner_id"]="foreign-installation"; r.save();
  r.runtime=CommonWorldActivationTestRegistration::make(r.record,r.sink); CHECK(r.runtime->prepare(r.path,r.f.authority,2)=="startup-failed");
  CHECK(r.runtime->snapshot().stages.size()==3 && !r.sink->connects);
 });
 run("native NTFS recovery/restart maintains successor revisions with no implicit connection",[&] {
  Rig r(root/"native-restart"); const auto native=[](const fs::path& p) { return makeCommonWorldFileStorage(p); };
  r.runtime=CommonWorldActivationTestRegistration::make(r.record,r.sink,{},[] { return "native-epoch-one"; },false,native); r.ready(); const auto old=r.world(); r.runtime->shutdown(); r.runtime.reset();
  r.runtime=CommonWorldActivationTestRegistration::make(r.record,r.sink,{},[] { return "native-epoch-two"; },false,native); r.ready();
  CHECK(r.world().owner_id==old.owner_id && r.world().coordinate_space.revision>old.coordinate_space.revision && !r.sink->connects);
 });
 run("actual codec preserves binary64, identity, durable revision and ordered bytes after activation",[&] {
  Rig r(root/"wire"); r.active(); r.drain(); const auto expected=r.world();
  r.f.pose.position={-0.0,std::numeric_limits<double>::denorm_min(),1.2345678901234567}; r.f.pose.orientation={-0.0,0.0,-0.0,-1.0};
  CHECK(r.runtime->receive(r.f.pose,4).publication=="queued"); r.drain(); CHECK(r.sink->frames.size()==3);
  for(std::size_t i=0;i<r.sink->frames.size();++i) { auto e=decode(r.sink->frames[i]);
   CHECK(std::visit([](const auto& v) { return v.sequence; },e)==static_cast<std::int64_t>(i));
   std::string bytes; w::Error error; CHECK(w::EncodeEnvelope(e,bytes,error) && bytes==r.sink->frames[i]);
  }
  const auto world=std::get<w::CommonWorldAuthorityPublication>(decode(r.sink->frames[0])); const auto mapping=std::get<w::CommonWorldMappingPublication>(decode(r.sink->frames[1]));
  const auto pose=std::get<w::TrustedHmdCommonPose>(decode(r.sink->frames[2]));
  CHECK(d::world(world.world)==d::world(expected) && d::world(mapping.mapping.world)==d::world(expected));
  CHECK(d::mapping(pose.mapping)==d::mapping(mapping.mapping) && d::source(pose.source.source_space)==d::source(r.f.pose.source.source_space));
  CHECK(pose.source.observation_id==r.f.pose.source.observation_id && pose.source.source_locate_time_ns==r.f.pose.source.source_locate_time_ns);
  for(int i=0;i<3;++i) CHECK(bits(pose.source_position[i])==bits(r.f.pose.position[i]) && bits(pose.common_position[i])==bits(r.f.pose.position[i]));
  for(int i=0;i<4;++i) CHECK(bits(pose.source_orientation[i])==bits(r.f.pose.orientation[i]) && bits(pose.common_orientation[i])==bits(r.f.pose.orientation[i]));
  CommonWorldRevisionStore audit(std::make_unique<Storage>(r.record),{"synthetic-bridge","common-A",{},0}); const auto durable=audit.open();
  CHECK(durable.latest && durable.latest->worldRevision==expected.coordinate_space.revision && durable.latest->mappingRevision==mapping.mapping.mapping_revision);
  CHECK(durable.latest->transition.ownerId==expected.owner_id && durable.latest->transition.worldSpaceId==expected.coordinate_space.id);
  CHECK(d::source(durable.latest->transition.anchor.source_space)==d::source(mapping.mapping.source_space));
  fs::create_directories(fs::path(ROOT_DIR)/"build/activation-fixtures"); std::ofstream stream(fs::path(ROOT_DIR)/"build/activation-fixtures/activation-normal.jsonl");
  for(const auto& bytes:r.sink->frames)stream<<bytes<<'\n'; CHECK(bool(stream));
 });
 std::cout<<"PASS "<<cases<<" cases / "<<checks<<" assertions; production endpoint connections=0; deterministic test registration only"<<std::endl; return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<std::endl; return 1; }
