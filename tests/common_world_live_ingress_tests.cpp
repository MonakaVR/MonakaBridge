#include "monaka_bridge/common_world_live_ingress.hpp"
#include "../src/authority/common_world_facts.hpp"
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <thread>

using namespace monaka_bridge;
namespace w=authority_wire;
namespace d=common_world_detail;
namespace fs=std::filesystem;
int cases=0,checks=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__)); } while(false)
template<class F> void run(const std::string& n,F f) { f(); ++cases; std::cout<<"PASS "<<n<<std::endl; }
const std::string S1="11111111-1111-4111-8111-111111111111",S2="22222222-2222-4222-8222-222222222222";
struct Source {
 w::TrustedHmdSourceAuthority a;
 w::TrustedHmdSourcePose p;
 w::TrustedHmdSourceUnavailable u;
 Source() {
  std::ifstream in(std::string(ROOT_DIR)+"/third_party/monaka-protocol-v2/fixtures/v2.1/ordered-streams/source-normal.jsonl");
  auto read=[&] { std::string line; CHECK(bool(std::getline(in,line))); w::Envelope v; w::Error e;
   CHECK(w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(line.data()),line.size(),v,e)); return v; };
  a=std::get<w::TrustedHmdSourceAuthority>(read()); p=std::get<w::TrustedHmdSourcePose>(read()); u=std::get<w::TrustedHmdSourceUnavailable>(read());
 }
 void reconnect(unsigned n) {
  std::string session="11111111-1111-4111-8111-"+std::string(12-std::to_string(n).size(),'0')+std::to_string(n);
  a.session_id=p.session_id=u.session_id=session; a.sequence=0; p.sequence=1; ++p.source.observation_id;
 }
};
struct Pause {
 std::mutex mutex; std::condition_variable cv; bool armed=false,entered=false,released=false;
 RevisionFaultBoundary boundary=RevisionFaultBoundary::BeforePublish;
 void hook(RevisionFaultBoundary b) {
  std::unique_lock<std::mutex> g(mutex); if(!armed || b!=boundary) return;
  entered=true; cv.notify_all();
  if(!cv.wait_for(g,std::chrono::seconds(10),[&]{return released;})) throw std::runtime_error("watchdog: pause not released");
 }
 void wait() { std::unique_lock<std::mutex> g(mutex); CHECK(cv.wait_for(g,std::chrono::seconds(5),[&]{return entered;})); }
 void release() { std::lock_guard<std::mutex> g(mutex); released=true; cv.notify_all(); }
};
struct Rig {
 Source source;
 std::shared_ptr<CommonWorldRevisionStore> store;
 std::unique_ptr<CommonWorldLiveIngress> ingress;
 CommonWorldLiveIngress::Callback callback;
 Rig(const fs::path& path,RevisionFaultHook hook={}) {
  fs::create_directories(path);
  store=std::make_shared<CommonWorldRevisionStore>(makeCommonWorldFileStorage(path/"world.rev",hook),CommonWorldPersistentHighWater{"synthetic-bridge","common-A",{},0});
  ingress=std::make_unique<CommonWorldLiveIngress>(store,"live-epoch",source.a.publisher_id);
 }
 void start() { callback=ingress->start(1); }
 void approve() { CHECK(ingress->authorize(callback,source.a.publisher_id,source.a.session_id,source.a.clock_id,source.a,2).status=="authority"); }
 void live() { start(); approve(); auto c=ingress->commitPending(callback,3); CHECK(c.status=="committed" && c.eligible && c.receipt); }
};
int main(int argc,char** argv) try {
 CHECK(argc==2); fs::path root=fs::path(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 run("start does not acquire owner or publisher; first explicit durable transition",[&] {
  Rig r(root/"start"); CHECK(!r.ingress->snapshot().sourceActive); r.start(); auto s=r.ingress->snapshot();
  CHECK(s.sourceActive && !s.ownerApproved && !s.runtime.world && !s.runtime.publisherSessionActive);
  CHECK(r.callback.receive(r.source.p,2).status=="owner-not-approved");
  CHECK(r.ingress->commitPending(r.callback,2).status=="owner-not-approved"); CHECK(!r.store->open().latest);
  r.approve(); CHECK(r.ingress->snapshot().runtime.pending && !r.ingress->snapshot().runtime.world);
  auto c=r.ingress->commitPending(r.callback,3); CHECK(c.eligible && c.receipt && c.events.empty());
  CHECK(r.store->matchesCurrentCommit(*c.receipt)); CHECK(r.callback.receive(r.source.p,4).status=="pose");
  CHECK(!r.ingress->snapshot().runtime.publisherSessionActive);
 });
 run("valid ordered events retain current durable mapping and remain silent",[&] {
  Rig r(root/"ordered"); r.live(); CHECK(r.callback.receive(r.source.p,4).status=="pose");
  const auto first=r.store->open().latest;
  for(int i=2;i<=20;++i) { r.source.p.sequence=i; ++r.source.p.source.observation_id;
   auto result=r.callback.receive(r.source.p,4+i); CHECK(result.status=="pose" && result.events.empty()); }
  CHECK(sameCommonWorldRevisionCandidate(*first,*r.store->open().latest));
 });
 run("stop without in-flight work and repeated stop/dispose",[&] {
  Rig r(root/"stop"); r.live(); r.ingress->stop(r.callback,4); r.ingress->stop(r.callback,5);
  auto s=r.ingress->snapshot(); CHECK(!s.sourceActive && !s.ownerApproved && !s.runtime.world);
  CHECK(r.callback.receive(r.source.p,6).status=="stale-source");
  r.ingress->dispose(); r.ingress->dispose(); CHECK(r.callback.receive(r.source.p,7).status=="disposed");
 });
 run("queued immutable old callback and late stop cannot affect reconnect",[&] {
  Rig r(root/"queued"); r.live(); const auto old=r.callback; auto queued=[old,p=r.source.p] { return old.receive(p,10); };
  r.ingress->stop(old,4); r.source.reconnect(2); r.start(); CHECK(r.callback.generation()>old.generation()); r.approve();
  r.ingress->stop(old,5); CHECK(r.ingress->snapshot().sourceActive);
  CHECK(queued().status=="stale-source"); CHECK(r.ingress->commitPending(old,6).status=="stale-source");
  CHECK(r.ingress->commitPending(r.callback,6).eligible); CHECK(r.callback.receive(r.source.p,7).status=="pose");
 });
 run("rapid same-identity reconnect uses independent callback generation",[&] {
  Rig r(root/"rapid"); r.live(); std::vector<CommonWorldLiveIngress::Callback> retired;
  for(unsigned i=2;i<15;++i) { retired.push_back(r.callback); r.ingress->stop(r.callback,i); r.source.reconnect(i); r.start(); r.approve();
   for(const auto& old:retired) CHECK(old.receive(r.source.p,i).status=="stale-source");
   CHECK(r.ingress->commitPending(r.callback,i).eligible); }
  CHECK(r.store->open().highWater.lastWorldRevision==13);
 });
 run("source fault recovery fences queued work",[&] {
  Rig r(root/"fault"); r.live(); auto old=r.callback; r.ingress->sourceFault(old,4); r.ingress->sourceFault(old,5);
  CHECK(old.receive(r.source.p,6).status=="stale-source"); r.source.reconnect(2); r.start(); r.approve();
  CHECK(r.ingress->commitPending(r.callback,7).eligible);
 });
 run("destruction leaves late callback with weak disposed state",[&] {
  Rig r(root/"destruction"); r.live(); auto c=r.callback; r.ingress.reset(); CHECK(c.receive(r.source.p,4).status=="disposed"); CHECK(r.store->open().latest);
 });
 run("foreign callback cannot authorize stop or persist another ingress",[&] {
  Rig a(root/"foreign-a"),b(root/"foreign-b"); a.live(); b.live(); b.ingress->stop(a.callback,4);
  CHECK(b.ingress->snapshot().sourceActive);
  CHECK(b.ingress->authorize(a.callback,b.source.a.publisher_id,b.source.a.session_id,b.source.a.clock_id,b.source.a,5).status=="stale-source");
  CHECK(b.ingress->commitPending(a.callback,5).status=="stale-source");
 });
 run("revoke before admission keeps source running but permanently retires approval",[&] {
  Rig r(root/"revoke"); r.live(); r.ingress->revokeOwner(4); auto s=r.ingress->snapshot();
  CHECK(s.sourceActive && !s.ownerApproved && !s.runtime.world);
  CHECK(r.callback.receive(r.source.p,5).status=="owner-not-approved");
  CHECK(r.ingress->authorize(r.callback,r.source.a.publisher_id,r.source.a.session_id,r.source.a.clock_id,r.source.a,6).status=="approval-retired");
  CHECK(r.ingress->commitPending(r.callback,7).status=="owner-not-approved");
  r.source.reconnect(2); r.start(); r.approve(); CHECK(r.ingress->commitPending(r.callback,8).eligible);
 });
 // Actual file storage pauses, not probabilistic sleeps. Watchdogs detect a deadlock only.
 for(auto boundary:{RevisionFaultBoundary::BeforeWrite,RevisionFaultBoundary::BeforePublish,RevisionFaultBoundary::AfterPublish}) {
  for(int action=0;action<4;++action) run("in-flight persistence fence boundary="+std::to_string(int(boundary))+" action="+std::to_string(action),[&,boundary,action] {
   Pause pause; pause.boundary=boundary; Rig r(root/("race-"+std::to_string(int(boundary))+"-"+std::to_string(action)),[&](auto b){pause.hook(b);});
   r.start(); r.approve(); pause.armed=true;
   auto work=std::async(std::launch::async,[&] { return r.ingress->commitPending(r.callback,3); }); pause.wait(); auto old=r.callback;
   CHECK(r.callback.receive(r.source.p,4).status=="backpressure");
   CHECK(r.ingress->commitPending(r.callback,4).status=="backpressure");
   if(action==0) r.ingress->stop(old,5);
   if(action==1) r.ingress->revokeOwner(5);
   if(action==2) r.ingress->dispose();
   if(action==3) { r.ingress->stop(old,5); r.source.reconnect(2); r.start(); CHECK(r.ingress->authorize(r.callback,r.source.a.publisher_id,r.source.a.session_id,r.source.a.clock_id,r.source.a,6).status=="backpressure"); }
   CHECK(!r.ingress->snapshot().runtime.world); // reached BEFORE releasing blocked persistence
   pause.release(); auto done=work.get(); CHECK(!done.eligible && done.events.empty()); CHECK(done.receipt); CHECK(r.store->open().latest);
   if(action==3) { r.approve(); CHECK(old.receive(r.source.p,7).status=="stale-source");
    auto fresh=r.ingress->commitPending(r.callback,8); CHECK(fresh.eligible && fresh.receipt->committed.worldRevision==1); }
  });
 }
 run("concurrent stop/dispose is serialized and stale callback stays closed",[&] {
  Rig r(root/"stop-dispose"); r.live(); auto c=r.callback;
  auto stop=std::async(std::launch::async,[&]{r.ingress->stop(c,4);});
  auto dispose=std::async(std::launch::async,[&]{r.ingress->dispose();}); stop.get(); dispose.get();
  CHECK(c.receive(r.source.p,5).status=="disposed"); CHECK(r.ingress->snapshot().disposed);
 });
 run("callback and stop both deterministic admission interleavings",[&] {
  for(int order=0;order<2;++order) {
   Rig r(root/("callback-stop-"+std::to_string(order))); r.live(); const auto c=r.callback;
   std::promise<void> ready,release; auto gate=release.get_future();
   auto event=std::async(std::launch::async,[&,c] {
    if(order==0) { ready.set_value(); gate.wait(); return c.receive(r.source.p,4); }
    auto result=c.receive(r.source.p,4); ready.set_value(); gate.wait(); return result;
   });
   ready.get_future().wait(); r.ingress->stop(c,5); release.set_value();
   CHECK(event.get().status==(order==0?"stale-source":"pose"));
   CHECK(!r.ingress->snapshot().runtime.world); CHECK(c.receive(r.source.p,6).status=="stale-source");
  }
 });
 run("owner epoch supersession rejects current source and old receipt",[&] {
  Rig r(root/"epoch"); r.start(); r.approve(); r.store->acquireAuthorization("next-epoch");
  CHECK(r.ingress->commitPending(r.callback,3).status=="persistence-failed"); CHECK(!r.ingress->snapshot().runtime.world);
  CHECK(r.callback.receive(r.source.p,4).status=="authorization-unavailable"); CHECK(!r.ingress->snapshot().sourceActive);
  CommonWorldLiveIngress fresh(r.store,"third-epoch",r.source.a.publisher_id); auto c=fresh.start(5);
  CHECK(c.receive(r.source.p,6).status=="owner-not-approved"); CHECK(!fresh.snapshot().runtime.publisherSessionActive);
 });
 run("retire owner before receipt completion then hand over durable epoch",[&] {
  Pause pause; pause.boundary=RevisionFaultBoundary::AfterPublish; Rig r(root/"epoch-delayed",[&](auto b){pause.hook(b);});
  r.start(); r.approve(); pause.armed=true; auto work=std::async(std::launch::async,[&]{return r.ingress->commitPending(r.callback,3);}); pause.wait();
  // Store handover shares its commit mutex; explicitly retire source first, then hand over after flush.
  r.ingress->revokeOwner(4); pause.release(); auto result=work.get(); CHECK(result.receipt && !result.eligible);
  r.store->acquireAuthorization("handover-epoch"); CHECK(!r.store->matchesCurrentCommit(*result.receipt));
  CHECK(r.callback.receive(r.source.p,5).status=="owner-not-approved");
 });
 run("process reopen restores durable provenance only and fresh authorization",[&] {
  const auto dir=root/"reopen"; Rig r(dir); r.live(); const auto old=r.callback; auto oldWorld=*r.ingress->snapshot().runtime.world;
  r.ingress->dispose(); r.store->dispose();
  auto store=std::make_shared<CommonWorldRevisionStore>(makeCommonWorldFileStorage(dir/"world.rev"),CommonWorldPersistentHighWater{"synthetic-bridge","common-A",{},0});
  CommonWorldLiveIngress fresh(store,"reopen-epoch",r.source.a.publisher_id); CHECK(!fresh.snapshot().sourceActive && !fresh.snapshot().runtime.world && !fresh.snapshot().runtime.publisherSessionActive);
  CHECK(old.receive(r.source.p,6).status=="disposed"); auto c=fresh.start(7); CHECK(c.receive(r.source.p,8).status=="owner-not-approved");
  CHECK(fresh.authorize(c,r.source.a.publisher_id,r.source.a.session_id,r.source.a.clock_id,r.source.a,9).status=="authority"); CHECK(fresh.commitPending(c,10).eligible);
  CHECK(fresh.snapshot().runtime.world->world.world_epoch!=oldWorld.world.world_epoch); CHECK(store->open().highWater.lastWorldRevision==1);
 });
 for(auto boundary:{RevisionFaultBoundary::BeforeWrite,RevisionFaultBoundary::AfterPublish}) run("storage failure has no receipt and exact retry boundary="+std::to_string(int(boundary)),[&,boundary] {
  bool armed=false; Rig r(root/("retry-"+std::to_string(int(boundary))),[&](auto b){if(armed && b==boundary) throw std::runtime_error("injected failure");}); r.start(); r.approve(); armed=true;
  auto failed=r.ingress->commitPending(r.callback,3); CHECK(failed.status=="persistence-failed" && !failed.receipt && !failed.eligible); CHECK(!r.ingress->snapshot().runtime.world);
  CHECK(bool(r.store->open().latest)==(boundary==RevisionFaultBoundary::AfterPublish)); armed=false;
  auto retry=r.ingress->commitPending(r.callback,4); CHECK(retry.eligible && retry.receipt->committed.worldRevision==0); CHECK(r.store->matchesCurrentCommit(*retry.receipt));
  CHECK(r.ingress->commitPending(r.callback,5).status=="persistence-failed"); CHECK(r.store->open().highWater.lastWorldRevision==0);
 });
 run("cancelled persistence cannot yield receipt or eligibility; exact retry",[&] {
  Rig r(root/"cancel"); r.start(); r.approve(); std::atomic_bool cancelled{true};
  auto result=r.ingress->commitPending(r.callback,3,&cancelled); CHECK(result.status=="persistence-failed" && !result.receipt && !result.eligible);
  CHECK(!r.store->open().latest); cancelled=false; CHECK(r.ingress->commitPending(r.callback,4,&cancelled).eligible);
 });
 run("same-anchor authority refresh preserves exact post-publish retry",[&] {
  bool armed=false; Rig r(root/"retry-refresh",[&](auto b){if(armed && b==RevisionFaultBoundary::AfterPublish) throw std::runtime_error("uncertain receipt");});
  r.start(); r.approve(); armed=true; CHECK(!r.ingress->commitPending(r.callback,3).receipt); CHECK(r.store->open().latest);
  r.source.a.sequence=1; CHECK(r.callback.receive(r.source.a,4).status=="authority"); armed=false;
  auto result=r.ingress->commitPending(r.callback,5); CHECK(result.eligible && result.receipt->committed.worldRevision==0);
 });
 run("duplicates and older sequence do not consume authoritative order",[&] {
  Rig r(root/"duplicate"); r.live(); CHECK(r.callback.receive(r.source.p,4).status=="pose");
  CHECK(r.callback.receive(r.source.p,5).status=="duplicate"); auto old=r.source.p; old.sequence=0; CHECK(r.callback.receive(old,6).status=="old");
  r.source.p.sequence=2; ++r.source.p.source.observation_id; CHECK(r.callback.receive(r.source.p,7).status=="pose");
 });
 run("detectable sequence gap faults source with no invented reorder buffer",[&] {
  Rig r(root/"gap"); r.live(); auto gap=r.source.p; gap.sequence=3; CHECK(r.callback.receive(gap,4).status=="gap");
  CHECK(!r.ingress->snapshot().sourceActive && !r.ingress->snapshot().runtime.world); CHECK(r.callback.receive(r.source.p,5).status=="stale-source");
 });
 run("simultaneous duplicate callbacks are serialized once",[&] {
  Rig r(root/"concurrent"); r.live(); std::vector<std::future<CommonWorldOperationResult>> calls;
  for(int i=0;i<8;++i) calls.push_back(std::async(std::launch::async,[&]{return r.callback.receive(r.source.p,4);}));
  int poses=0,duplicates=0; for(auto& f:calls) { auto result=f.get(); poses+=result.status=="pose"; duplicates+=result.status=="duplicate"; }
  CHECK(poses==1 && duplicates==7); CHECK(r.store->open().highWater.lastMappingRevision==1);
 });
 run("foreign source identity cannot mutate mapping; no source identity synthesis",[&] {
  Rig r(root/"identity"); r.live(); auto before=*r.ingress->snapshot().runtime.mapping;
  auto wrong=r.source.p; wrong.source.source_space.source_id="foreign-source"; CHECK(r.callback.receive(wrong,4).status!="pose");
  CHECK(!r.ingress->snapshot().runtime.mapping || d::source(before.mapping.source_space)==d::source(r.ingress->snapshot().runtime.mapping->mapping.source_space));
 });
 run("binary64 pose/timestamp/validity exact copy through live ingress and dry-run",[&] {
  Rig r(root/"exact"); r.start(); r.approve(); CHECK(r.ingress->beginDryRunPublisherSession(S1,S2,2).events.empty());
  auto commit=r.ingress->commitPending(r.callback,3); CHECK(commit.eligible && commit.events.size()==2);
  r.source.p.position={-0.0,std::numeric_limits<double>::denorm_min(),1.2345678901234567};
  for(auto& n:r.source.p.orientation) n=-n;
  const auto frozen=r.source.p; auto result=r.callback.receive(frozen,4); CHECK(result.status=="pose" && result.events.size()==1);
  const auto pose=std::get<w::TrustedHmdCommonPose>(result.events[0]);
  CHECK(d::bits(pose.common_position)==d::bits(frozen.position)); CHECK(d::bits(pose.source_position)==d::bits(frozen.position));
  CHECK(d::bits(pose.common_orientation)==d::bits(frozen.orientation)); CHECK(d::bits(pose.source_orientation)==d::bits(frozen.orientation));
  CHECK(pose.source.source_locate_time_ns==frozen.source.source_locate_time_ns && pose.source.source_time_domain_id==frozen.source.source_time_domain_id);
  CHECK(pose.source.observation_id==frozen.source.observation_id && d::source(pose.mapping.source_space)==d::source(frozen.source.source_space));
  CHECK(pose.validity.position_valid==frozen.validity.position_valid && pose.validity.orientation_tracked==frozen.validity.orientation_tracked);
  CHECK(d::bits(frozen.position)==d::bits(r.source.p.position));
 });
 for(int variant=0;variant<3;++variant) run("wire contract rejects nonfinite binary64 variant="+std::to_string(variant),[&,variant] {
  Rig r(root/("nonfinite-"+std::to_string(variant))); r.live();
  r.source.p.position[0]=variant==0?std::numeric_limits<double>::infinity():variant==1?-std::numeric_limits<double>::infinity():std::numeric_limits<double>::quiet_NaN();
  CHECK(r.callback.receive(r.source.p,4).status=="malformed"); CHECK(!r.ingress->snapshot().runtime.world);
 });
 run("invalid bootstrap fails closed and publisher cannot activate owner",[&] {
  Rig r(root/"bootstrap"); r.start(); auto bad=r.source.a; bad.publisher_id="foreign";
  CHECK(r.ingress->authorize(r.callback,bad.publisher_id,bad.session_id,bad.clock_id,bad,2).status=="session-rejected");
  CHECK(!r.ingress->snapshot().sourceActive); CHECK(r.ingress->beginDryRunPublisherSession(S1,S2,3).events.empty()); CHECK(!r.ingress->snapshot().runtime.world);
 });
 run("bounded retained session history exhausts without reuse or eviction",[&] {
  Source source; fs::create_directories(root/"capacity"); auto store=std::make_shared<CommonWorldRevisionStore>(makeCommonWorldFileStorage(root/"capacity/world.rev"),CommonWorldPersistentHighWater{"synthetic-bridge","common-A",{},0});
  CommonWorldLiveIngress ingress(store,"capacity-epoch",source.a.publisher_id,1); auto first=ingress.start(1);
  CHECK(ingress.authorize(first,source.a.publisher_id,source.a.session_id,source.a.clock_id,source.a,2).status=="authority");
  ingress.stop(first,3); source.reconnect(2); auto second=ingress.start(4);
  CHECK(ingress.authorize(second,source.a.publisher_id,source.a.session_id,source.a.clock_id,source.a,5).status=="session-rejected");
  CHECK(first.receive(source.p,6).status=="stale-source" && !ingress.snapshot().runtime.world);
 });
 run("actual live ingress vectors for independent receiver replay",[&] {
  const auto fixtures=fs::path(ROOT_DIR)/"build/live-ingress-common-world-fixtures"; fs::create_directories(fixtures);
  for(int scenario=0;scenario<2;++scenario) {
   Rig r(root/("generated-"+std::to_string(scenario))); r.start(); r.approve();
   CHECK(r.ingress->beginDryRunPublisherSession(S1,S2,2).events.empty());
   auto all=r.ingress->commitPending(r.callback,3).events;
   auto append=[&](const auto& v){all.insert(all.end(),v.begin(),v.end());};
   append(r.callback.receive(r.source.p,4).events);
   if(scenario==0) {
    append(r.callback.receive(r.source.u,5).events); r.source.p.sequence=3; ++r.source.p.source.observation_id;
    append(r.callback.receive(r.source.p,6).events);
   } else {
    r.source.a.sequence=2; ++r.source.a.source_space.source_space_generation;
    append(r.callback.receive(r.source.a,5).events); auto c=r.ingress->commitPending(r.callback,6); CHECK(c.eligible); append(c.events);
    r.source.p.sequence=3; ++r.source.p.source.observation_id; r.source.p.source.source_space=r.source.a.source_space;
    append(r.callback.receive(r.source.p,7).events);
   }
   std::ofstream out(fixtures/(scenario==0?"live-normal.jsonl":"live-replacement.jsonl")); CHECK(out.good());
   for(const auto& event:all) { std::string bytes; w::Error e; CHECK(w::EncodeEnvelope(event,bytes,e)); out<<bytes<<'\n'; }
   CHECK(out.good());
  }
 });
 std::cout<<"PASS "<<cases<<" cases, "<<checks<<" assertions; source/runtime/store/publisher lifetimes independently fenced"<<std::endl; return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<std::endl; return 1; }
