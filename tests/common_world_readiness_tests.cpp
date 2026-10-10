#include "monaka_bridge/common_world_deployment_readiness.hpp"
#include "common_world_build_identity.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <future>
#include <iostream>
using namespace monaka_bridge; namespace fs=std::filesystem; namespace w=authority_wire; using J=nlohmann::json;
std::atomic<int> checks{0}; int cases=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__)); } while(false)
template<class F> void run(const std::string& n,F f) { f(); ++cases; std::cout<<"PASS "<<n<<std::endl; }
void write(const fs::path& p,const std::string& b) { fs::create_directories(p.parent_path()); std::ofstream f(p,std::ios::binary); f<<b; CHECK(bool(f)); }
std::string read(const fs::path& p) { std::ifstream f(p,std::ios::binary); return {(std::istreambuf_iterator<char>(f)),{}}; }
struct Record { std::optional<std::string> bytes; int writes=0; bool readFail=false; };
class Storage final:public CommonWorldRevisionStorage {
 std::shared_ptr<Record> r_;
public:
 explicit Storage(std::shared_ptr<Record> r):r_(std::move(r)) {}
 std::optional<std::string> readCommitted() override { if(r_->readFail) throw std::runtime_error("recovery failed"); return r_->bytes; }
 void atomicWrite(const std::string& b,const std::atomic_bool*) override { ++r_->writes; r_->bytes=b; }
};
struct Sink { int connects=0,sends=0; bool pending=false; };
class TestTransport final:public CommonWorldDatagramTransport {
 std::shared_ptr<Sink> sink_;
public:
 explicit TestTransport(std::shared_ptr<Sink> s):sink_(std::move(s)) {}
 bool connect() override { ++sink_->connects; return true; }
 void close() noexcept override {}
 DatagramSubmissionResult submit(std::string_view b) override { ++sink_->sends; return {sink_->pending?DatagramSubmission::Pending:DatagramSubmission::Accepted,sink_->pending?0:b.size()}; }
};
namespace monaka_bridge {
class CommonWorldReadinessTestRegistration {
public:
 static std::unique_ptr<CommonWorldDeploymentReadiness> make(std::shared_ptr<Record> r,std::function<void(const std::string&)> hook={}) {
  CommonWorldDeploymentReadiness::Dependencies d; d.storage=[r](const fs::path&) { return std::make_unique<Storage>(r); }; d.before=std::move(hook);
  return std::unique_ptr<CommonWorldDeploymentReadiness>(new CommonWorldDeploymentReadiness(std::move(d)));
 }
};
class CommonWorldActivationTestRegistration {
public:
 static std::unique_ptr<CommonWorldProductionActivation> make(std::shared_ptr<Record> r,std::shared_ptr<Sink> s) {
  CommonWorldProductionActivation::Dependencies d; d.mode=CommonWorldExecutionMode::Test;
  d.storage=[r](const fs::path&) { return std::make_unique<Storage>(r); };
  d.transport=[s](const CommonWorldActivationConfig& c) { CHECK(c.mode==CommonWorldExecutionMode::Test && c.endpoint=="test://sink"); return std::make_unique<TestTransport>(s); };
  d.freshEpoch=[] { return "emergency-test-epoch"; };
  return std::unique_ptr<CommonWorldProductionActivation>(new CommonWorldProductionActivation(std::move(d)));
 }
};
}
struct Rig {
 fs::path dir; CommonWorldPreflightRequest q; J config,manifest;
 std::shared_ptr<Record> record=std::make_shared<Record>(); std::unique_ptr<CommonWorldDeploymentReadiness> observer;
 explicit Rig(const fs::path& path,std::function<void(const std::string&)> hook={}):dir(path) {
  fs::create_directories(dir); q={dir/"artifact",dir/"config/production.json",dir/"source.jsonl",dir/"logs",dir/"reports",commonWorldBuildCommit(),""};
  for(auto p:{q.logs,q.reports,dir/"state"}) fs::create_directories(p);
  const auto fixture=read(fs::path(ROOT_DIR)/"third_party/monaka-protocol-v2/fixtures/v2.1/ordered-streams/source-normal.jsonl");
  write(q.source,fixture); const auto first=J::parse(fixture.substr(0,fixture.find('\n')));
  config={{"enabled",false},{"mode","production"},{"transport","udp-ipv4"},{"codec","monaka-wire-2.2"},{"endpoint","udp://192.0.2.10:30111"},
   {"owner_id","synthetic-bridge"},{"world_space_id","common-A"},{"source_publisher_id",first.at("publisher_id")},
   {"revision_path",fs::absolute(dir/"state/world.rev").string()},{"peer_policy","explicit-authority-association-no-implicit-world-replacement"}};
  save();
  write(q.artifact/"bin/monaka_common_world_runtime.exe","test-only inert bytes"); write(q.artifact/"bin/monaka_common_world_preflight.exe","test-only inert observer bytes");
  write(q.artifact/"templates/production.activation.json",read(fs::path(ROOT_DIR)/"deployment/production.activation.json"));
  write(q.artifact/"dependency-inventory.json",J{{"schema","monaka-common-world-dependencies-1"},{"codec","monaka-wire-2.2"},{"system_dlls",{"kernel32.dll","bcrypt.dll","ws2_32.dll","ole32.dll"}}}.dump());
  manifest={{"schema","monaka-common-world-artifact-1"},{"activation_default",false},{"architecture","x64"},{"runtime","windows-local-ntfs"},
   {"commit",commonWorldBuildCommit()},{"build_input_sha256",COMMON_WORLD_BUILD_INPUT_SHA256},{"build_configuration","Release"},{"files",J::array()}};
  remanifest(); observer=CommonWorldReadinessTestRegistration::make(record,std::move(hook));
 }
 void save() { write(q.config,config.dump()); }
 void remanifest() {
  manifest["files"]=J::array();
  for(auto name:{"bin/monaka_common_world_runtime.exe","bin/monaka_common_world_preflight.exe","templates/production.activation.json","dependency-inventory.json"}) {
   const auto p=q.artifact/name; manifest["files"].push_back({{"path",name},{"size",fs::file_size(p)},{"sha256",commonWorldFileSha256(p)}});
  }
  saveManifest();
 }
 void saveManifest() { write(q.artifact/"artifact-manifest.json",manifest.dump()); write(q.artifact/"artifact-manifest.sha256",commonWorldFileSha256(q.artifact/"artifact-manifest.json")+"\n"); }
 CommonWorldReadinessResult flight() {
  const auto writes=record->writes; const auto before=record->bytes; const auto r=observer->preflight(q);
  CHECK(record->writes==writes && record->bytes==before); const auto j=J::parse(r.json);
  CHECK(j.at("authorization")==false && j.at("activation_enabled")==false); return r;
 }
 void ready() { CHECK(flight().state==CommonWorldReadinessState::ReadyForControlledActivation); }
 w::Envelope bootstrap() { const auto b=read(q.source),first=b.substr(0,b.find('\n')); w::Envelope e; w::Error err; CHECK(w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(first.data()),first.size(),e,err)); return e; }
};
struct Barrier {
 std::mutex m; std::condition_variable cv; bool entered=false,released=false;
 void block() { std::unique_lock<std::mutex> l(m); entered=true; cv.notify_all(); CHECK(cv.wait_for(l,std::chrono::seconds(10),[&] { return released; })); }
 void wait() { std::unique_lock<std::mutex> l(m); CHECK(cv.wait_for(l,std::chrono::seconds(5),[&] { return entered; })); }
 void release() { std::lock_guard<std::mutex> l(m); released=true; cv.notify_all(); }
};
int main(int argc,char** argv) try {
 CHECK(argc==2); const auto root=fs::absolute(fs::path(argv[1]))/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 auto path=[&] { return root/std::to_string(cases); };
 run("default OFF template and missing config",[&] {
  CHECK(J::parse(read(fs::path(ROOT_DIR)/"deployment/production.activation.json"))["enabled"]==false);
  Rig r(path()); fs::remove(r.q.config); CHECK(r.flight().state==CommonWorldReadinessState::NotConfigured);
 });
 run("complete OFF configuration ready; no real persistent writes or network API",[&] { Rig r(path()); r.ready(); const auto j=J::parse(r.observer->snapshot().json); CHECK(j["production_transport_connected"]==false && j["persistent_writes"]==0); CHECK(!j["construction_only"]["enabled"].get<bool>()); });
 for(auto b:{"{","[]","{\"enabled\":false,\"enabled\":false}","{\"enabled\":false,\"unknown_secret\":\"ignored?\"}"}) run("strict missing/malformed/duplicate/unknown configuration",[&] { Rig r(path()); write(r.q.config,b); CHECK(r.flight().state==CommonWorldReadinessState::InvalidConfiguration); });
 run("malformed input diagnostics never echo possible secret",[&] { Rig r(path()); write(r.q.config,"{\"enabled\":false,\"secret\":SECRET_5AHA"); const auto result=r.flight(); CHECK(result.state==CommonWorldReadinessState::InvalidConfiguration && result.json.find("SECRET_5AHA")==std::string::npos); });
 for(auto key:{"enabled","mode","owner_id","world_space_id","source_publisher_id","revision_path","codec","peer_policy"}) run(std::string("missing required ")+key,[&] { Rig r(path()); r.config.erase(key); r.save(); CHECK(r.flight().state!=CommonWorldReadinessState::ReadyForControlledActivation); });
 run("activation ON rejected before recovery",[&] { Rig r(path()); r.config["enabled"]=true; r.save(); CHECK(r.flight().state==CommonWorldReadinessState::InvalidConfiguration); CHECK(!r.record->bytes); });
 for(auto target:{"test://sink","udp://127.0.0.1:30111","udp://localhost:30111","udp://192.0.02.10:30111"}) run(std::string("reject test/loopback/ambiguous production endpoint ")+target,[&] { Rig r(path()); r.config["endpoint"]=target; r.save(); CHECK(r.flight().state==CommonWorldReadinessState::TransportConfigurationInvalid); });
 run("test mode cannot select production endpoint or production registry",[&] { Rig r(path()); r.config["mode"]="test"; r.save(); CHECK(r.flight().state!=CommonWorldReadinessState::ReadyForControlledActivation); r.config["transport"]="deterministic"; r.config["endpoint"]="test://sink"; r.save(); CHECK(r.flight().state==CommonWorldReadinessState::TransportConfigurationInvalid); });
 run("missing dependency is explicit",[&] { Rig r(path()); fs::remove(r.q.artifact/"bin/monaka_common_world_runtime.exe"); CHECK(r.flight().state==CommonWorldReadinessState::DependencyMissing); });
 run("missing OS dependency fails closed",[&] { Rig r(path()); auto d=J::parse(read(r.q.artifact/"dependency-inventory.json")); d["system_dlls"].push_back("monaka_missing_dependency_5aha.dll"); write(r.q.artifact/"dependency-inventory.json",d.dump()); r.remanifest(); CHECK(r.flight().state==CommonWorldReadinessState::DependencyMissing); });
 for(auto name:{"state","logs","reports"}) run(std::string("unavailable writable directory ")+name,[&] { Rig r(path()); const auto p=r.dir/name; fs::remove(p); write(p,"file blocks directory"); CHECK(r.flight().state==CommonWorldReadinessState::RuntimeNotReady); });
 run("persistent recovery read failure",[&] { Rig r(path()); r.record->readFail=true; CHECK(r.flight().state==CommonWorldReadinessState::PersistentStateNotReady); });
 run("corrupt committed record",[&] { Rig r(path()); r.record->bytes="corrupt"; CHECK(r.flight().state==CommonWorldReadinessState::PersistentStateNotReady); });
 for(auto key:{"owner_id","world_space_id"}) run(std::string("persistent identity/world mismatch ")+key,[&] { Rig r(path()); CommonWorldRevisionStore store(std::make_unique<Storage>(r.record),{"synthetic-bridge","common-A",{},0,r.config["source_publisher_id"].get<std::string>()}); store.acquireAuthorization("prior-epoch"); r.config[key]="mismatch"; r.save(); CHECK(r.flight().state==CommonWorldReadinessState::PersistentStateNotReady); });
 run("stale owner epoch expectation",[&] { Rig r(path()); r.q.expectedOwnerEpoch="stale-epoch"; CHECK(r.flight().state==CommonWorldReadinessState::PersistentStateNotReady); });
 run("source unavailable and source identity mismatch",[&] { Rig r(path()); write(r.q.source,"{}"); CHECK(r.flight().state==CommonWorldReadinessState::RuntimeNotReady); r.config["source_publisher_id"]="foreign"; r.save(); write(r.q.source,read(fs::path(ROOT_DIR)/"third_party/monaka-protocol-v2/fixtures/v2.1/ordered-streams/source-normal.jsonl")); CHECK(r.flight().state==CommonWorldReadinessState::RuntimeNotReady); });
 run("codec selection unavailable",[&] { Rig r(path()); r.config["codec"]="unknown"; r.save(); CHECK(r.flight().state==CommonWorldReadinessState::InvalidConfiguration); });
 for(auto key:{"commit","build_input_sha256","architecture","build_configuration"}) run(std::string("artifact version/architecture mismatch ")+key,[&] { Rig r(path()); r.manifest[key]="mismatch"; r.saveManifest(); CHECK(r.flight().state==CommonWorldReadinessState::ArtifactInvalid); });
 run("corrupt/missing manifest",[&] { Rig r(path()); write(r.q.artifact/"artifact-manifest.json","{"); CHECK(r.flight().state==CommonWorldReadinessState::ArtifactInvalid); fs::remove(r.q.artifact/"artifact-manifest.json"); CHECK(r.flight().state==CommonWorldReadinessState::ArtifactInvalid); });
 run("hash mismatch and unexpected artifact file",[&] { Rig r(path()); write(r.q.artifact/"bin/monaka_common_world_runtime.exe","tampered"); CHECK(r.flight().state==CommonWorldReadinessState::ArtifactInvalid); r.remanifest(); write(r.q.artifact/"unreviewed.rev","state"); CHECK(r.flight().state==CommonWorldReadinessState::ArtifactInvalid); });
 run("manifest traversal/duplicate rejected",[&] { Rig r(path()); r.manifest["files"][0]["path"]="../source.jsonl"; r.saveManifest(); CHECK(r.flight().state==CommonWorldReadinessState::ArtifactInvalid); r.remanifest(); r.manifest["files"].push_back(r.manifest["files"][0]); r.saveManifest(); CHECK(r.flight().state==CommonWorldReadinessState::ArtifactInvalid); });
 run("mutable state cannot be inside artifact",[&] { Rig r(path()); r.config["revision_path"]=(r.q.artifact/"world.rev").string(); r.save(); CHECK(r.flight().state==CommonWorldReadinessState::RuntimeNotReady); });
 run("repeated preflight idempotent state and persistent bytes",[&] { Rig r(path()); r.ready(); const auto gen=r.observer->snapshot().generation; r.ready(); CHECK(r.observer->snapshot().generation>gen); CHECK(!r.record->bytes); });
 for(auto point:{"owner-publisher-sender-construction","source-codec","persistent-recovery"}) run(std::string("construction prerequisite failure ")+point,[&] { Rig r(path(),[point](const std::string& p) { if(p==point) throw std::runtime_error("injected prerequisite failure"); }); CHECK(r.flight().state!=CommonWorldReadinessState::ReadyForControlledActivation); });
 run("parallel/reentrant preflight deterministic busy",[&] {
  Barrier b; Rig r(path(),[&](const std::string& p) { if(p=="persistent-recovery") b.block(); });
  auto f=std::async(std::launch::async,[&] { return r.observer->preflight(r.q); }); b.wait(); const auto busy=r.observer->preflight(r.q); CHECK(busy.reason=="preflight-in-progress"); b.release(); CHECK(f.get().state==CommonWorldReadinessState::ReadyForControlledActivation);
 });
 run("same-thread preflight reentry cannot activate or deadlock",[&] {
  Rig* ptr=nullptr; Rig r(path(),[&](const std::string& p) { if(p=="persistent-recovery") CHECK(ptr->observer->preflight(ptr->q).reason=="preflight-in-progress"); }); ptr=&r; r.ready();
 });
 for(auto action:{"shutdown","emergency","invalidate"}) run(std::string("in-flight preflight retirement ")+action,[&] {
  Barrier b; Rig r(path(),[&](const std::string& p) { if(p=="persistent-recovery") b.block(); });
  auto f=std::async(std::launch::async,[&] { return r.observer->preflight(r.q); }); b.wait();
  if(std::string(action)=="shutdown") r.observer->shutdown(); else if(std::string(action)=="emergency") r.observer->emergencyRevoke(); else r.observer->invalidate("owner/world/config changed");
  b.release(); CHECK(f.get().state==CommonWorldReadinessState::RuntimeNotReady); CHECK(!r.record->writes);
 });
 run("configuration changed during preflight",[&] {
  Barrier b; Rig r(path(),[&](const std::string& p) { if(p=="final-input-revalidation") b.block(); });
  auto f=std::async(std::launch::async,[&] { return r.observer->preflight(r.q); }); b.wait(); r.config["enabled"]=true; r.save(); b.release(); CHECK(f.get().state==CommonWorldReadinessState::RuntimeNotReady);
 });
 run("ready snapshots cannot authorize; invalidation requires fresh observation",[&] { Rig r(path()); const auto stale=r.flight(); r.observer->invalidate("epoch changed"); CHECK(stale.state==CommonWorldReadinessState::ReadyForControlledActivation); CHECK(r.observer->snapshot().state==CommonWorldReadinessState::RuntimeNotReady); r.observer->emergencyRevoke(); r.observer->emergencyRevoke(); CHECK(r.observer->preflight(r.q).reason=="observer-retired"); });
 run("native NTFS preflight recovery preserves exact committed bytes",[&] {
  Rig r(path()); const auto state=fs::path(r.config["revision_path"].get<std::string>());
  { CommonWorldRevisionStore store(makeCommonWorldFileStorage(state),{"synthetic-bridge","common-A",{},0,r.config["source_publisher_id"].get<std::string>()}); store.acquireAuthorization("native-prior-epoch"); }
  const auto before=read(state); CommonWorldDeploymentReadiness production; CHECK(production.preflight(r.q).state==CommonWorldReadinessState::ReadyForControlledActivation); CHECK(read(state)==before);
  auto owned=makeCommonWorldFileStorage(state); CHECK(production.preflight(r.q).state==CommonWorldReadinessState::PersistentStateNotReady); CHECK(read(state)==before);
 });
 for(bool pending:{false,true}) run(pending?"emergency retires pending completion":"emergency retires queued publication and reconnect",[&] {
  Rig r(path()); r.config["enabled"]=true; r.config["mode"]="test"; r.config["transport"]="deterministic"; r.config["endpoint"]="test://sink"; r.save();
  auto sink=std::make_shared<Sink>(); sink->pending=pending; auto runtime=CommonWorldActivationTestRegistration::make(r.record,sink);
  CHECK(runtime->prepare(r.q.config,r.bootstrap(),1)=="ready-inactive"); const auto world=runtime->snapshot().sender->ingress.runtime.world->world;
  CHECK(runtime->activate(world,2)=="active"); CommonWorldProductionActivation::Completion completion;
  if(pending) { auto result=runtime->pump(); CHECK(result.status=="pending-local-completion"); completion=result.completion; }
  const auto beforeJson=J::parse(commonWorldRuntimeDiagnostics(runtime->snapshot(),parseCommonWorldActivationConfig(r.config.dump(),r.q.config),commonWorldBuildCommit()));
  CHECK(beforeJson["sender"]["owner_epoch_observed_from_world_token"]=="emergency-test-epoch");
  const auto durable=r.record->bytes; emergencyRevokeCommonWorld(*runtime,3); emergencyRevokeCommonWorld(*runtime,4);
  const auto s=runtime->snapshot(); CHECK(s.shutdown && s.status==CommonWorldActivationStatus::Shutdown && s.sender && !s.sender->enabled && !s.sender->queued && !s.sender->connected);
  const auto sends=sink->sends,connects=sink->connects; CHECK(runtime->activate(world,5)=="shutdown"); CHECK(runtime->prepare(r.q.config,r.bootstrap(),5)=="shutdown"); CHECK(runtime->pump().status=="activation-inactive");
  if(completion) CHECK(runtime->complete(completion,{DatagramSubmission::Accepted,1})=="stale-completion");
  CHECK(sink->sends==sends && sink->connects==connects && r.record->bytes==durable);
  const auto json=J::parse(commonWorldRuntimeDiagnostics(s,parseCommonWorldActivationConfig(r.config.dump(),r.q.config),commonWorldBuildCommit())); CHECK(json["gate_state"]=="Shutdown");
 });
 std::cout<<"PASS "<<cases<<" readiness cases / "<<checks.load()<<" assertions\n"; return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
