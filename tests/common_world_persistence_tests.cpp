#include "monaka_bridge/persistent_common_world_runtime.hpp"
#include "../src/authority/common_world_facts.hpp"
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

using namespace monaka_bridge;
namespace w=authority_wire;
namespace d=common_world_detail;
namespace fs=std::filesystem;
int cases=0,checks=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__)); } while(false)
template<class F> void fails(F f) { bool failed=false; try { f(); } catch(const std::exception&) { failed=true; } CHECK(failed); }
template<class F> void run(const std::string& name,F f) { f(); ++cases; std::cout<<"PASS "<<name<<std::endl; }
const std::string S1="11111111-1111-4111-8111-111111111111",S2="22222222-2222-4222-8222-222222222222",
 S3="33333333-3333-4333-8333-333333333333",S4="44444444-4444-4444-8444-444444444444";
w::Envelope decode(const std::string& s) { w::Envelope v; w::Error error; CHECK(w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(s.data()),s.size(),v,error)); return v; }
struct Source {
 w::TrustedHmdSourceAuthority a;
 w::TrustedHmdSourcePose p;
 Source() {
  std::ifstream in(std::string(ROOT_DIR)+"/third_party/monaka-protocol-v2/fixtures/v2.1/ordered-streams/source-normal.jsonl");
  std::string line; CHECK(bool(std::getline(in,line))); a=std::get<w::TrustedHmdSourceAuthority>(decode(line));
  CHECK(bool(std::getline(in,line))); p=std::get<w::TrustedHmdSourcePose>(decode(line));
 }
 void begin(PersistentCommonWorldRuntime& r,std::int64_t at=100) { CHECK(r.beginSourceSession(a.publisher_id,a.session_id,a.clock_id,a,at).status=="authority"); }
};
CommonWorldPersistentHighWater initial() { return {"synthetic-bridge","common-A",{},0}; }
std::shared_ptr<CommonWorldRevisionStore> store(const fs::path& file,RevisionFaultHook hook={}) {
 return std::make_shared<CommonWorldRevisionStore>(makeCommonWorldFileStorage(file,std::move(hook)),initial());
}
CommonWorldRevisionCandidate next(const CommonWorldRevisionCandidate& previous) {
 auto c=previous; ++c.transition.requestId; ++c.worldRevision; ++c.mappingRevision;
 c.transition.minimumWorldRevision=c.worldRevision; c.transition.minimumMappingRevision=c.mappingRevision; return c;
}
void write(const fs::path& path,const std::string& bytes) { std::ofstream out(path,std::ios::binary|std::ios::trunc); out.write(bytes.data(),bytes.size()); CHECK(out.good()); }
std::string read(const fs::path& path) { std::ifstream in(path,std::ios::binary); CHECK(in.good()); return {std::istreambuf_iterator<char>(in),{}}; }
std::string resign(std::string bytes) {
 bytes.resize(bytes.size()-32); BCRYPT_ALG_HANDLE a{}; BCRYPT_HASH_HANDLE h{}; std::string digest(32,'\0');
 CHECK(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
 CHECK(BCryptCreateHash(a,&h,nullptr,0,nullptr,0,0)>=0);
 CHECK(BCryptHashData(h,reinterpret_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0);
 CHECK(BCryptFinishHash(h,reinterpret_cast<PUCHAR>(digest.data()),32,0)>=0); BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(a,0); return bytes+digest;
}
struct Pause {
 std::mutex mutex; std::condition_variable cv; bool armed=false,entered=false,released=false;
 RevisionFaultBoundary boundary=RevisionFaultBoundary::DuringFlush;
 void hook(RevisionFaultBoundary b) {
  std::unique_lock<std::mutex> g(mutex); if(!armed || b!=boundary) return;
  entered=true; cv.notify_all(); if(!cv.wait_for(g,std::chrono::seconds(10),[&]{return released;})) throw std::runtime_error("pause timeout");
 }
 void wait() { std::unique_lock<std::mutex> g(mutex); CHECK(cv.wait_for(g,std::chrono::seconds(5),[&]{return entered;})); }
 void release() { std::lock_guard<std::mutex> g(mutex); released=true; cv.notify_all(); }
};
int main(int argc,char** argv) try {
 if(argc==4 && std::string(argv[1])=="--crash") {
  Source source; const auto boundary=static_cast<RevisionFaultBoundary>(std::stoi(argv[3])); bool armed=false;
  auto s=store(fs::path(argv[2]),[&](auto b) { if(armed && b==boundary) ExitProcess(77); });
  PersistentCommonWorldRuntime r(s,"crash-child",source.a.publisher_id); source.begin(r);
  auto t=r.prepareTransition(); armed=true; r.persist(t); return 78;
 }
 CHECK(argc==2); fs::path root=fs::path(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()); fs::create_directories(root);
 auto path=[&](const std::string& name) { const auto p=root/name; fs::create_directories(p); return p/"world.rev"; };
 run("missing store and construction are not durable authority",[&] {
  auto s=store(path("missing")); CHECK(!s->open().latest); CHECK(!s->open().highWater.lastWorldRevision);
  Source source; PersistentCommonWorldRuntime r(s,"missing-epoch",source.a.publisher_id);
  CHECK(!r.snapshot().world && !r.snapshot().mapping && !r.snapshot().publisherSessionActive);
  source.begin(r); auto t=r.prepareTransition(); CHECK(!s->open().latest); CHECK(!r.snapshot().world);
  CHECK(r.deliver(t,{t.candidate},101).empty()); CHECK(!r.snapshot().world);
 });
 run("first successive exact duplicate stale and same-key conflict",[&] {
  auto s=store(path("ordering")); Source source; PersistentCommonWorldRuntime r(s,"order-epoch",source.a.publisher_id); source.begin(r);
  auto t=r.prepareTransition(); auto receipt=r.persist(t); CHECK(s->matchesCurrentCommit(receipt));
  CHECK(sameCommonWorldRevisionCandidate(r.persist(t).committed,t.candidate));
  auto changed=t.candidate; changed.transition.anchor.source_time_domain_id="other-domain"; fails([&]{s->commit(changed);});
  changed=t.candidate; changed.transition.reason="source_space_changed"; fails([&]{s->commit(changed);});
  auto c=next(t.candidate); auto second=s->commit(c); CHECK(s->matchesCurrentCommit(second)); CHECK(!s->matchesCurrentCommit(receipt));
  fails([&]{r.persist(t);}); CHECK(s->open().highWater.lastWorldRevision==1); CHECK(s->open().highWater.lastMappingRevision==2);
 });
 run("exact identity anchor minima request epoch and receipt payload gates",[&] {
  auto s=store(path("exact")); Source source; PersistentCommonWorldRuntime r(s,"exact-epoch",source.a.publisher_id); source.begin(r);
  auto t=r.prepareTransition(); auto good=r.persist(t);
  for(int i=0;i<12;++i) {
   auto bad=good;
   switch(i) {
    case 0:bad.committed.transition.ownerId="foreign";break;
    case 1:bad.committed.transition.worldSpaceId="foreign";break;
    case 2:bad.committed.transition.requestId++;break;
    case 3:bad.committed.transition.anchor.source_space.source_id="foreign";break;
    case 4:bad.committed.transition.anchor.source_space.source_authority_session_epoch="foreign";break;
    case 5:bad.committed.transition.anchor.source_space.source_space_id="foreign";break;
    case 6:bad.committed.transition.anchor.source_space.source_space_generation++;break;
    case 7:bad.committed.transition.anchor.source_time_domain_id="foreign";break;
    case 8:bad.committed.transition.minimumWorldRevision++;break;
    case 9:bad.committed.mappingRevision++;break;
    case 10:bad.committed.authorizationEpoch="foreign";break;
    case 11:bad.committed.transition.reason="source_space_changed";break;
   }
   CHECK(!s->matchesCurrentCommit(bad)); CHECK(r.deliver(t,bad,101).empty()); CHECK(!r.snapshot().world);
  }
  auto wrong=t.candidate; wrong.transition.ownerId="foreign"; fails([&]{s->commit(wrong);});
  wrong=t.candidate; wrong.transition.worldSpaceId="foreign"; fails([&]{s->commit(wrong);});
  wrong=t.candidate; wrong.authorizationEpoch="foreign"; fails([&]{s->commit(wrong);});
  CHECK(r.deliver(t,good,102).empty()); CHECK(r.snapshot().world && !r.snapshot().publisherSessionActive);
  CHECK(r.deliver(t,good,103).empty());
  auto snapshot=r.snapshot(); CHECK(snapshot.mapping); CHECK(d::transform(snapshot.mapping->transform)==d::transform(d::identity()));
 });
 for(int index=0;index<5;++index) run("fault boundary "+std::to_string(index)+" has no receipt; recover old/new",[&,index] {
  auto file=path("fault-"+std::to_string(index)); bool armed=false;
  auto s=store(file,[&](auto b) { if(armed && static_cast<int>(b)==index) throw std::runtime_error("injected boundary "+std::to_string(index)); });
  Source source; PersistentCommonWorldRuntime r(s,"fault-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition(); auto first=r.persist(t);
  auto c=next(t.candidate); armed=true; bool returned=false;
  fails([&] { s->commit(c); returned=true; }); CHECK(!returned); CHECK(!fs::exists(file.wstring()+L".tmp"));
  s->dispose(); auto restored=store(file); const auto state=restored->open(); CHECK(state.latest);
  CHECK(sameCommonWorldRevisionCandidate(*state.latest,index==4?c:t.candidate));
  CHECK(restored->matchesCurrentCommit({index==4?c:t.candidate}));
  std::cout<<"RECOVERY boundary="<<index<<" expected="<<(index==4?"new":"old")<<" actual="<<(state.latest->worldRevision==c.worldRevision?"new":"old")<<" no-returned-receipt"<<std::endl;
 });
 for(int index=0;index<5;++index) run("actual process termination boundary "+std::to_string(index),[&,index] {
  auto file=path("crash-"+std::to_string(index)); wchar_t executable[32768]{}; CHECK(GetModuleFileNameW(nullptr,executable,32768)>0);
  std::wstring cmd=L"\""+std::wstring(executable)+L"\" --crash \""+file.wstring()+L"\" "+std::to_wstring(index);
  STARTUPINFOW si{}; si.cb=sizeof(si); PROCESS_INFORMATION pi{};
  CHECK(CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=0);
  CHECK(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0); DWORD exit=0; CHECK(GetExitCodeProcess(pi.hProcess,&exit)!=0); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CHECK(exit==77);
  auto s=store(file); auto state=s->open(); CHECK(bool(state.latest)==(index==4));
  CHECK(state.currentAuthorizationEpoch=="crash-child");
  if(index>0 && index<4) CHECK(fs::exists(file.wstring()+L".tmp"));
  Source source; PersistentCommonWorldRuntime restart(s,"crash-restart",source.a.publisher_id); CHECK(!restart.snapshot().world && !restart.snapshot().publisherSessionActive);
  if(state.latest) CHECK(!s->matchesCurrentCommit({*state.latest}));
  std::cout<<"PROCESS-CRASH boundary="<<index<<" exit="<<exit<<" recovery="<<(state.latest?"new committed":"authorization only")<<std::endl;
 });
 run("invalid header version truncation corruption trailing state fail closed",[&] {
  auto file=path("corrupt"); auto s=store(file); Source source; PersistentCommonWorldRuntime r(s,"corrupt-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition(); r.persist(t); s->dispose();
  const auto original=read(file);
  for(int index=0;index<6;++index) {
   auto bytes=original;
   switch(index) { case 0:bytes[0]='X'; bytes=resign(bytes);break; case 1:bytes[8]=2;bytes=resign(bytes);break;
    case 2:bytes.resize(20);break;case 3:bytes[20]^=1;break;case 4:bytes.pop_back();break;
    case 5:bytes.insert(bytes.size()-32,1,'X');bytes=resign(bytes);break; }
   write(file,bytes); fails([&]{store(file);});
  }
  write(file,original); auto restored=store(file); CHECK(restored->open().latest);
 });
 run("persistent identity and caller floor mismatches reject open",[&] {
  auto file=path("open-identity"); auto s=store(file); s->acquireAuthorization("identity-epoch"); s->dispose();
  auto h=initial(); h.ownerId="other-owner"; fails([&]{CommonWorldRevisionStore other(makeCommonWorldFileStorage(file),h);});
  h=initial(); h.worldSpaceId="other-space"; fails([&]{CommonWorldRevisionStore other(makeCommonWorldFileStorage(file),h);});
  h=initial(); h.lastWorldRevision=10; fails([&]{CommonWorldRevisionStore other(makeCommonWorldFileStorage(file),h);});
 });
 run("uncommitted temp ignored exclusive process lock and disappeared record",[&] {
  auto file=path("temp"); write(file.wstring()+L".tmp","uncommitted garbage"); auto s=store(file); CHECK(!s->open().latest);
  fails([&]{store(file);}); s->acquireAuthorization("temp-epoch"); CHECK(!fs::exists(file.wstring()+L".tmp"));
  CHECK(fs::remove(file)); fails([&]{s->open();}); s->dispose(); fails([&]{s->open();});
 });
 run("cancel before write before publish and post publish policy",[&] {
  auto file=path("cancel"); std::atomic_bool cancel{false}; bool armed=false; auto boundary=RevisionFaultBoundary::BeforePublish;
  auto s=store(file,[&](auto b) { if(armed && b==boundary) cancel=true; }); Source source; PersistentCommonWorldRuntime r(s,"cancel-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition();
  cancel=true; fails([&]{r.persist(t,&cancel);}); CHECK(!s->open().latest);
  cancel=false; armed=true; fails([&]{r.persist(t,&cancel);}); CHECK(!s->open().latest); CHECK(!fs::exists(file.wstring()+L".tmp"));
  cancel=false; boundary=RevisionFaultBoundary::AfterPublish; auto receipt=r.persist(t,&cancel); CHECK(cancel); CHECK(s->matchesCurrentCommit(receipt));
 });
 run("concurrent exact requests serialize to one committed record",[&] {
  auto s=store(path("concurrent")); Source source; PersistentCommonWorldRuntime r(s,"concurrent-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition();
  std::vector<std::future<CommonWorldDurableReceipt>> jobs;
  for(int i=0;i<8;++i) jobs.push_back(std::async(std::launch::async,[&]{return r.persist(t);}));
  for(auto& job:jobs) CHECK(sameCommonWorldRevisionCandidate(job.get().committed,t.candidate));
  CHECK(s->open().highWater.lastWorldRevision==0); CHECK(s->open().highWater.lastMappingRevision==1);
 });
 run("concurrent conflicting requests permit exactly one complete winner",[&] {
  auto s=store(path("concurrent-conflict")); Source source; PersistentCommonWorldRuntime r(s,"conflict-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition();
  auto changed=t.candidate; changed.transition.anchor.source_space.source_space_id="other-space";
  auto attempt=[&](auto c) { try { s->commit(c); return true; } catch(...) { return false; } };
  auto a=std::async(std::launch::async,[&]{return attempt(t.candidate);}); auto b=std::async(std::launch::async,[&]{return attempt(changed);});
  const bool wonA=a.get(),wonB=b.get(); CHECK(wonA!=wonB); const auto state=s->open();
  CHECK(sameCommonWorldRevisionCandidate(*state.latest,wonA?t.candidate:changed));
 });
 run("open serializes behind commit while source busy gate immediately revokes",[&] {
  Pause pause; auto s=store(path("concurrent-open"),[&](auto b){pause.hook(b);}); Source source; PersistentCommonWorldRuntime r(s,"open-race-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition();
  pause.armed=true; auto job=std::async(std::launch::async,[&]{return r.persist(t);}); pause.wait();
  auto opening=std::async(std::launch::async,[&]{return s->open();}); CHECK(opening.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
  auto rejected=r.receiveSource(source.p,101); CHECK(rejected.status=="authorization-unavailable"); CHECK(!r.snapshot().world && !r.snapshot().pending);
  pause.release(); auto receipt=job.get(); const auto restored=opening.get(); CHECK(sameCommonWorldRevisionCandidate(*restored.latest,t.candidate));
  CHECK(r.deliver(t,receipt,102).empty());
 });
 run("corrupt committed state at authorization gate invalidates cached world",[&] {
  auto file=path("active-corruption"); auto s=store(file); Source source; PersistentCommonWorldRuntime r(s,"active-corrupt-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition(); auto receipt=r.persist(t); r.deliver(t,receipt,101); CHECK(r.snapshot().world);
  auto original=read(file); auto corrupt=original; corrupt[0]^=1; write(file,corrupt); fails([&]{r.deliver(t,receipt,102);});
  CHECK(!r.snapshot().world && !r.snapshot().mapping && !r.snapshot().pending); write(file,original);
  CHECK(r.deliver(t,receipt,103).empty()); CHECK(!r.snapshot().world);
 });
 for(auto boundary:{RevisionFaultBoundary::DuringFlush,RevisionFaultBoundary::AfterPublish}) run("immediate revoke at boundary "+std::to_string(static_cast<int>(boundary)),[&,boundary] {
  Pause pause; pause.boundary=boundary; auto s=store(path("revoke-"+std::to_string(static_cast<int>(boundary))),[&](auto b){pause.hook(b);});
  Source source; PersistentCommonWorldRuntime r(s,"revoke-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition(); pause.armed=true;
  auto job=std::async(std::launch::async,[&]{return r.persist(t);}); pause.wait();
  // Delivery must not wait on the store mutex while holding the revoke mutex.
  CHECK(r.deliver(t,{t.candidate},101).empty()); r.revokeWorld(102); CHECK(!r.snapshot().world && !r.snapshot().pending);
  pause.release(); auto receipt=job.get(); CHECK(s->matchesCurrentCommit(receipt)); CHECK(r.deliver(t,receipt,103).empty()); CHECK(!r.snapshot().world);
 });
 run("out of order completion source replacement and ABA do not authorize",[&] {
  auto s=store(path("out-of-order")); Source source; PersistentCommonWorldRuntime r(s,"order-runtime",source.a.publisher_id); source.begin(r); auto old=r.prepareTransition(); auto late=r.persist(old);
  auto a=source.a; a.sequence=1; a.source_space.source_space_generation++; CHECK(r.receiveSource(a,101).status=="authority");
  auto current=r.prepareTransition(); auto receipt=r.persist(current); CHECK(r.deliver(old,late,102).empty()); CHECK(!r.snapshot().world);
  CHECK(r.deliver(current,receipt,103).empty()); CHECK(r.snapshot().world); CHECK(r.snapshot().world->world.coordinate_space.revision==1);
  auto oldEpoch=r.snapshot().world->world.world_epoch; r.revokeWorld(104); a.sequence=2; a.source_space=source.a.source_space;
  CHECK(r.receiveSource(a,105).status=="untrusted"); // 5AB forbids rolling source generation back
  auto aba=r.prepareTransition(); CHECK(aba.candidate.transition.requestId!=old.candidate.transition.requestId);
  CHECK(aba.candidate.transition.anchor.source_space.source_space_generation==source.a.source_space.source_space_generation+1);
  CHECK(r.deliver(old,late,106).empty()); auto ar=r.persist(aba); r.deliver(aba,ar,107); CHECK(r.snapshot().world->world.world_epoch!=oldEpoch);
 });
 run("restart opens highwater only fresh authorization and epochs reject replay",[&] {
  auto file=path("restart"); Source source; auto s=store(file); PersistentCommonWorldRuntime old(s,"process-one",source.a.publisher_id); source.begin(old); auto t=old.prepareTransition(); auto receipt=old.persist(t); old.deliver(t,receipt,101);
  const auto previous=*old.snapshot().mapping; s->dispose(); auto reopened=store(file); CHECK(reopened->open().highWater.lastWorldRevision==0);
  PersistentCommonWorldRuntime fresh(reopened,"process-two",source.a.publisher_id); CHECK(!fresh.snapshot().world && !fresh.snapshot().mapping && !fresh.snapshot().publisherSessionActive);
  CHECK(fresh.deliver(t,receipt,102).empty()); source.begin(fresh,103); auto n=fresh.prepareTransition(); CHECK(n.candidate.worldRevision==1 && n.candidate.mappingRevision==2);
  CHECK(fresh.deliver(n,receipt,104).empty()); auto nr=fresh.persist(n); fresh.deliver(n,nr,105); auto map=*fresh.snapshot().mapping;
  CHECK(map.mapping.world.world_epoch!=previous.mapping.world.world_epoch && map.mapping.calibration_epoch!=previous.mapping.calibration_epoch);
  CHECK(d::source(map.mapping.source_space)==d::source(previous.mapping.source_space));
  fails([&]{reopened->acquireAuthorization("process-one");});
 });
 run("handover supersedes committed and pending owner publication",[&] {
  auto s=store(path("handover")); Source source; PersistentCommonWorldRuntime old(s,"handover-old",source.a.publisher_id); source.begin(old); auto t=old.prepareTransition(); auto receipt=old.persist(t); old.deliver(t,receipt,101);
  CHECK(old.beginDryRunPublisherSession(S1,S2,102).events.size()==2);
  PersistentCommonWorldRuntime fresh(s,"handover-new",source.a.publisher_id); CHECK(!fresh.snapshot().world);
  CHECK(old.deliver(t,receipt,103).empty()); auto result=old.receiveSource(source.p,104); CHECK(result.status=="authorization-unavailable");
  CHECK(!old.snapshot().world); CHECK(result.events.size()==1); CHECK(std::holds_alternative<w::CommonWorldRevocation>(result.events[0])); fails([&]{old.persist(t);});
  source.begin(fresh,105); auto n=fresh.prepareTransition(); auto nr=fresh.persist(n); fresh.deliver(n,nr,106); CHECK(fresh.snapshot().world);
 });
 run("mapping update revoke reestablish persistence and bit identity",[&] {
  auto s=store(path("mapping")); Source source; PersistentCommonWorldRuntime r(s,"mapping-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition(); auto receipt=r.persist(t); r.deliver(t,receipt,101);
  auto old=*r.snapshot().mapping; auto m=r.prepareMappingUpdate(2); auto mr=r.persist(m); CHECK(sameCommonWorldRevisionCandidate(mr.committed,m.candidate)); r.deliver(m,mr,102);
  CHECK(r.snapshot().mapping->mapping.mapping_revision==2); CHECK(d::transform(r.snapshot().mapping->transform)==d::transform(old.transform));
  auto wrong=m.candidate; wrong.mappingRevision=3; wrong.mapping.mapping.mapping_revision=3; wrong.mapping.transform.translation_xyz[0]=-0.0; fails([&]{s->commit(wrong);});
  r.revokeMapping(103); CHECK(!r.snapshot().mapping && r.snapshot().world); CHECK(r.deliver(m,mr,104).empty());
  auto re=r.prepareMappingReestablishment(3); auto rr=r.persist(re); r.deliver(re,rr,105); CHECK(r.snapshot().mapping);
  CHECK(r.snapshot().mapping->mapping.calibration_epoch!=old.mapping.calibration_epoch); CHECK(d::world(r.snapshot().mapping->mapping.world)==d::world(old.mapping.world));
  CHECK(d::transform(r.snapshot().mapping->transform)==d::transform(d::identity())); CHECK(s->open().highWater.lastMappingRevision==3);
 });
 run("binary64 pose copy through durable owner preserves sign bits subnormal quaternion sign",[&] {
  auto s=store(path("bits")); Source source; PersistentCommonWorldRuntime r(s,"bits-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition(); auto receipt=r.persist(t); r.deliver(t,receipt,101); r.beginDryRunPublisherSession(S1,S2,102);
  source.p.position={-0.0,std::numeric_limits<double>::denorm_min(),1.0000000000000002}; source.p.orientation={-0.0,0.0,-0.0,-1.0};
  auto events=r.receiveSource(source.p,103).events; CHECK(events.size()==1); const auto& p=std::get<w::TrustedHmdCommonPose>(events[0]);
  CHECK(d::bits(p.source_position)==d::bits(source.p.position)); CHECK(d::bits(p.common_position)==d::bits(source.p.position));
  CHECK(d::bits(p.source_orientation)==d::bits(source.p.orientation)); CHECK(d::bits(p.common_orientation)==d::bits(source.p.orientation));
  auto invalid=source.p; invalid.sequence=2; invalid.source.observation_id++; invalid.position[0]=std::numeric_limits<double>::infinity();
  CHECK(r.receiveSource(invalid,104).events.size()<=1); CHECK(!r.snapshot().world); // baseline wire rejects nonfinite, no manufactured support
 });
 run("runtime disposal during persistence prevents late activation",[&] {
  Pause pause; auto s=store(path("dispose-runtime"),[&](auto b){pause.hook(b);}); Source source; PersistentCommonWorldRuntime r(s,"dispose-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition(); pause.armed=true;
  auto job=std::async(std::launch::async,[&]{return r.persist(t);}); pause.wait(); r.dispose(); CHECK(r.snapshot().disposed && !r.snapshot().world); pause.release(); auto receipt=job.get(); CHECK(r.deliver(t,receipt,101).empty());
 });
 run("store dispose serializes behind commit and releases exclusive ownership",[&] {
  Pause pause; auto file=path("dispose-store"); auto s=store(file,[&](auto b){pause.hook(b);}); Source source; PersistentCommonWorldRuntime r(s,"dispose-store-epoch",source.a.publisher_id); source.begin(r); auto t=r.prepareTransition(); pause.armed=true;
  auto job=std::async(std::launch::async,[&]{return r.persist(t);}); pause.wait(); auto disposal=std::async(std::launch::async,[&]{s->dispose();});
  CHECK(disposal.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout); pause.release(); auto receipt=job.get(); disposal.get(); fails([&]{s->commit(t.candidate);});
  auto reopened=store(file); CHECK(reopened->matchesCurrentCommit(receipt));
 });
 run("bounded authorization history and revision exhaustion fail closed",[&] {
  auto s=store(path("history")); for(int i=0;i<128;++i) s->acquireAuthorization("history-"+std::to_string(i)); fails([&]{s->acquireAuthorization("history-overflow");}); CHECK(s->open().retiredAndCurrentAuthorizationEpochs.size()==128);
  auto file=path("exhausted"); auto floor=initial(); floor.lastWorldRevision=UINT32_MAX; floor.lastMappingRevision=UINT32_MAX;
  auto full=std::make_shared<CommonWorldRevisionStore>(makeCommonWorldFileStorage(file),floor); Source source; PersistentCommonWorldRuntime r(full,"exhausted-epoch",source.a.publisher_id); source.begin(r); CHECK(!r.snapshot().pending); fails([&]{r.prepareTransition();});
 });
 run("durable lifecycle generates actual ordered codec bytes for consumer replay",[&] {
  const fs::path fixtures=std::string(ROOT_DIR)+"/build/persistent-common-world-fixtures"; fs::create_directories(fixtures);
  auto save=[&](const std::string& name,const std::vector<w::Envelope>& all) {
   std::ofstream out(fixtures/(name+".jsonl")); CHECK(out.good());
   for(const auto& event:all) { std::string bytes; w::Error error; CHECK(w::EncodeEnvelope(event,bytes,error));
    CHECK(decode(bytes).index()==event.index()); out<<bytes<<'\n'; }
   CHECK(out.good());
  };
  for(int scenario=0;scenario<3;++scenario) {
   auto file=path("generated-"+std::to_string(scenario)); auto s=store(file); Source source;
   PersistentCommonWorldRuntime r(s,"replay-"+std::to_string(scenario),source.a.publisher_id); source.begin(r);
   std::vector<w::Envelope> all;
   auto append=[&](const auto& events) { all.insert(all.end(),events.begin(),events.end()); };
   CHECK(r.beginDryRunPublisherSession(S1,S2,101).events.empty()); auto t=r.prepareTransition(); auto receipt=r.persist(t); append(r.deliver(t,receipt,102)); append(r.receiveSource(source.p,103).events);
   if(scenario==0) {
    auto m=r.prepareMappingUpdate(2); auto mr=r.persist(m); append(r.deliver(m,mr,104));
    source.p.sequence=2; ++source.p.source.observation_id; append(r.receiveSource(source.p,105).events);
    append(r.revokeMapping(106)); auto re=r.prepareMappingReestablishment(3); auto rr=r.persist(re); append(r.deliver(re,rr,107));
    source.p.sequence=3; ++source.p.source.observation_id; append(r.receiveSource(source.p,108).events); append(r.revokeWorld(109)); save("durable-lifecycle",all);
   } else if(scenario==1) {
    append(r.disconnect(104)); r.dispose(); s->dispose(); auto restored=store(file);
    PersistentCommonWorldRuntime fresh(restored,"replay-restarted",source.a.publisher_id);
    CHECK(fresh.beginDryRunPublisherSession(S3,S4,105).events.empty()); source.begin(fresh,106); CHECK(fresh.deliver(t,receipt,107).empty());
    auto nt=fresh.prepareTransition(); auto nr=fresh.persist(nt); append(fresh.deliver(nt,nr,108)); ++source.p.source.observation_id;
    append(fresh.receiveSource(source.p,109).events); save("durable-restart",all);
   } else {
    auto authority=source.a; authority.sequence=2; ++authority.source_space.source_space_generation;
    append(r.receiveSource(authority,104).events); CHECK(r.deliver(t,receipt,105).empty());
    auto nt=r.prepareTransition(); auto nr=r.persist(nt); append(r.deliver(nt,nr,106));
    source.p.sequence=3; ++source.p.source.observation_id; source.p.source.source_space=authority.source_space;
    append(r.receiveSource(source.p,107).events); save("durable-replacement",all);
   }
  }
 });
 std::cout<<"PASS "<<cases<<" cases, "<<checks<<" assertions; all persistence/runtime outputs dormant/synthetic"<<std::endl; return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<std::endl; return 1; }
