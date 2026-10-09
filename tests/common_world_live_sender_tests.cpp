#include "monaka_bridge/common_world_live_sender.hpp"
#include "monaka_bridge/network.hpp"
#include "../src/authority/common_world_facts.hpp"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <thread>

using namespace monaka_bridge;
namespace w=authority_wire;namespace d=common_world_detail;namespace fs=std::filesystem;
int cases=0,checks=0;
#define CHECK(x) do {++checks;if(!(x))throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__));}while(false)
template<class F> void run(const std::string& n,F f){f();++cases;std::cout<<"PASS "<<n<<std::endl;}
const std::string S1="11111111-1111-4111-8111-111111111111",S2="22222222-2222-4222-8222-222222222222",
 S3="33333333-3333-4333-8333-333333333333",S4="44444444-4444-4444-8444-444444444444";
w::Envelope decode(const std::string& b){w::Envelope e;w::Error err;CHECK(w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(b.data()),b.size(),e,err));return e;}
std::int64_t seq(const w::Envelope& e){return std::visit([](const auto& m){return m.sequence;},e);}
mb::Datagram receivePacket(mb::Udp& sink){
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 while(std::chrono::steady_clock::now()<deadline){if(auto p=sink.receive())return *p;std::this_thread::yield();}
 throw std::runtime_error("loopback peer receive watchdog expired");
}
struct SourceFixture {
 w::TrustedHmdSourceAuthority a;w::TrustedHmdSourcePose p;
 SourceFixture(){std::ifstream in(std::string(ROOT_DIR)+"/third_party/monaka-protocol-v2/fixtures/v2.1/ordered-streams/source-normal.jsonl");std::string l;
  CHECK(bool(std::getline(in,l)));a=std::get<w::TrustedHmdSourceAuthority>(decode(l));CHECK(bool(std::getline(in,l)));p=std::get<w::TrustedHmdSourcePose>(decode(l));}
 void reconnect(){a.session_id=p.session_id=S3;a.sequence=0;p.sequence=1;++p.source.observation_id;}
};
struct Fake final:CommonWorldDatagramTransport {
 bool connectFail=false,connected=false,throwSubmit=false;std::size_t calls=0,connects=0;
 DatagramSubmission mode=DatagramSubmission::Accepted;std::size_t accepted=SIZE_MAX;
 std::vector<std::string> attempted,escaped;
 bool connect()override{++connects;connected=!connectFail;return connected;}
 void close()noexcept override{connected=false;}
 DatagramSubmissionResult submit(std::string_view b)override{
  ++calls;if(throwSubmit)throw std::runtime_error("injected transport exception; acceptance unknown");
  attempted.emplace_back(b);if(mode==DatagramSubmission::Accepted || mode==DatagramSubmission::Pending)escaped.emplace_back(b);
  return {mode,mode==DatagramSubmission::Accepted?(accepted==SIZE_MAX?b.size():accepted):0};
 }
};
struct Rig {
 SourceFixture f;std::shared_ptr<CommonWorldRevisionStore> store;Fake* transport;
 std::unique_ptr<CommonWorldLiveSender> sender;CommonWorldLiveSender::Source source;CommonWorldDurableReceipt receipt;
 Rig(const fs::path& dir,std::size_t cap=64,std::size_t retry=3,RevisionFaultHook hook={},std::string owner="synthetic-bridge"){
  fs::create_directories(dir);store=std::make_shared<CommonWorldRevisionStore>(makeCommonWorldFileStorage(dir/"world.rev",hook),CommonWorldPersistentHighWater{owner,"common-A",{},0});
  auto t=std::make_unique<Fake>();transport=t.get();sender=std::make_unique<CommonWorldLiveSender>(store,"sender-epoch",f.a.publisher_id,std::move(t),cap,retry);
 }
 void durable(){source=sender->startSource(1);CHECK(sender->authorizeSource(source,f.a.publisher_id,f.a.session_id,f.a.clock_id,f.a,2).status=="authority");
  auto c=sender->commitSource(source,3);CHECK(c.eligible && c.receipt);receipt=*c.receipt;}
 void live(){durable();CHECK(sender->connect());CHECK(sender->enablePublisher(receipt,S1,S2,4)=="queued");}
 void drain(){while(sender->snapshot().queued){CHECK(sender->pump().status=="local-accepted");}}
 void ready(){live();drain();}
 void pose(std::int64_t n=1){f.p.sequence=n;f.p.source.observation_id=n;auto r=source.receive(f.p,5+n);CHECK(r.source.status=="pose" && r.publication=="queued");}
};
struct Pause {
 std::mutex mutex;std::condition_variable cv;bool armed=false,entered=false,released=false;
 void hook(RevisionFaultBoundary b){std::unique_lock<std::mutex> g(mutex);if(!armed || b!=RevisionFaultBoundary::AfterPublish)return;entered=true;cv.notify_all();
  if(!cv.wait_for(g,std::chrono::seconds(10),[&]{return released;}))throw std::runtime_error("watchdog: release");}
 void wait(){std::unique_lock<std::mutex> g(mutex);CHECK(cv.wait_for(g,std::chrono::seconds(5),[&]{return entered;}));}
 void release(){std::lock_guard<std::mutex> g(mutex);released=true;cv.notify_all();}
};
int main(int argc,char** argv)try{
 CHECK(argc==2);const auto root=fs::path(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 run("explicit source/durable/publisher/transport lifetimes and local acceptance only",[&]{Rig r(root/"normal");
  CHECK(!r.sender->snapshot().connected && !r.sender->snapshot().enabled);CHECK(r.sender->connect());
  CHECK(r.sender->pump().status=="no-ready-head");CHECK(r.sender->enablePublisher({},S1,S2,0)=="receipt-ineligible");
  r.durable();CHECK(!r.sender->snapshot().enabled && !r.sender->snapshot().lastLocalAcceptedSequence);CHECK(r.transport->calls==0);
  CHECK(r.sender->enablePublisher(r.receipt,S1,S2,4)=="queued");CHECK(r.sender->snapshot().queued==2);r.drain();r.pose();r.drain();
  CHECK(r.transport->escaped.size()==3 && r.sender->snapshot().lastLocalAcceptedSequence==2);CHECK(r.store->matchesCurrentCommit(r.receipt));
 });
 run("candidate, forged and mismatched exact receipts cannot send",[&]{Rig r(root/"receipt");r.durable();CHECK(r.sender->connect());
  for(int i=0;i<6;++i){auto bad=r.receipt;switch(i){case 0:bad.committed.authorizationEpoch="foreign";break;case 1:++bad.committed.worldRevision;break;
   case 2:++bad.committed.mappingRevision;break;case 3:bad.committed.transition.ownerId="foreign";break;case 4:bad.committed.transition.anchor.source_time_domain_id="foreign";break;case 5:++bad.committed.transition.requestId;break;}
   CHECK(r.sender->enablePublisher(bad,S1,S2,4)=="receipt-ineligible");CHECK(r.transport->calls==0);}
 });
 run("source and transport connection without owner do not authorize",[&]{Rig r(root/"no-owner");r.source=r.sender->startSource(1);CHECK(r.sender->connect());
  CHECK(r.source.receive(r.f.p,2).source.status=="owner-not-approved");CHECK(!r.sender->commitSource(r.source,3).eligible);
  CHECK(r.sender->pump().status=="no-ready-head" && r.transport->calls==0);
 });
 for(int point=0;point<3;++point)run("revoke before enqueue/dequeue/final send boundary "+std::to_string(point),[&,point]{Rig r(root/("revoke-"+std::to_string(point)));r.durable();CHECK(r.sender->connect());
  CommonWorldLiveSender::Attempt plan;
  if(point>0)CHECK(r.sender->enablePublisher(r.receipt,S1,S2,4)=="queued");if(point==2){plan=r.sender->prepare();CHECK(plan);}
  r.sender->revokeOwner(5);CHECK(r.transport->calls==0);
  CHECK(r.sender->enablePublisher(r.receipt,S3,S4,6)=="receipt-ineligible");
  CHECK(point==2?r.sender->submit(plan).status=="stale-attempt":r.sender->pump().status=="no-ready-head");
  CHECK(!r.sender->snapshot().enabled && r.sender->snapshot().queued==0);
 });
 run("post-entry revoke cannot recall escaped datagram or accept late completion",[&]{Rig r(root/"after-entry");r.live();r.transport->mode=DatagramSubmission::Pending;
  auto pending=r.sender->pump();CHECK(pending.status=="pending-local-completion" && pending.completion);CHECK(r.transport->escaped.size()==1);
  r.sender->revokeOwner(5);CHECK(r.sender->complete(pending.completion,{DatagramSubmission::Accepted,r.transport->escaped[0].size()})=="stale-completion");
  CHECK(!r.sender->snapshot().lastLocalAcceptedSequence && r.sender->pump().status=="no-ready-head");CHECK(r.transport->calls==1);
 });
 run("post-success revoke keeps honest local cursor but cancels later work",[&]{Rig r(root/"after-success");r.live();CHECK(r.sender->pump().status=="local-accepted");
  r.sender->revokeOwner(5);CHECK(r.transport->escaped.size()==1 && r.sender->snapshot().lastLocalAcceptedSequence==0);CHECK(r.sender->snapshot().queued==0);
 });
 run("fresh same-owner epoch invalidates queue and completion",[&]{Rig r(root/"epoch");r.live();r.transport->mode=DatagramSubmission::Pending;auto pending=r.sender->pump();
  r.store->acquireAuthorization("new-owner-epoch");CHECK(r.sender->complete(pending.completion,{DatagramSubmission::Accepted,r.transport->escaped[0].size()})=="stale-completion");
  CHECK(r.sender->pump().status=="no-ready-head" && !r.sender->snapshot().lastLocalAcceptedSequence);
  r.sender->dispose();auto t=std::make_unique<Fake>();auto* raw=t.get();CommonWorldLiveSender fresh(r.store,"fresh-runtime-epoch",r.f.a.publisher_id,std::move(t));
  CHECK(fresh.connect());CHECK(fresh.enablePublisher(r.receipt,S3,S4,6)=="receipt-ineligible");CHECK(raw->calls==0);
  CHECK(fresh.complete(pending.completion,{DatagramSubmission::Accepted,1})=="stale-completion");
 });
 run("different installation owner identity rejects old receipt and attempt",[&]{Rig a(root/"identity-a"),b(root/"identity-b",64,3,{},"other-bridge");a.live();auto old=a.sender->prepare();b.durable();CHECK(b.sender->connect());
  CHECK(b.sender->enablePublisher(a.receipt,S1,S2,4)=="receipt-ineligible");CHECK(b.sender->submit(old).status=="stale-attempt");CHECK(b.transport->calls==0);
 });
 run("epoch handover after reservation before send has no API attempt",[&]{Rig r(root/"epoch-reserved");r.live();auto p=r.sender->prepare();r.store->acquireAuthorization("replacement");
  CHECK(r.sender->submit(p).status=="authorization-unavailable");CHECK(r.transport->calls==0 && !r.sender->snapshot().enabled);
 });
 run("source reconnect cannot admit old callback or auto-enable sender",[&]{Rig r(root/"source-reconnect");r.ready();auto oldSource=r.source;auto oldReceipt=r.receipt;
  r.f.reconnect();r.source=r.sender->startSource(10);CHECK(r.source.generation()!=oldSource.generation());CHECK(!r.sender->snapshot().enabled && r.sender->snapshot().connected);
  CHECK(oldSource.receive(r.f.p,11).source.status=="stale-source");CHECK(!r.sender->commitSource(oldSource,12).eligible);
  CHECK(r.sender->authorizeSource(r.source,r.f.a.publisher_id,r.f.a.session_id,r.f.a.clock_id,r.f.a,13).status=="authority");auto c=r.sender->commitSource(r.source,14);CHECK(c.eligible);
  CHECK(r.sender->enablePublisher(oldReceipt,S3,S4,15)=="receipt-ineligible");CHECK(r.sender->enablePublisher(*c.receipt,S3,S4,15)=="queued");r.drain();
  CHECK(std::get<w::CommonWorldAuthorityPublication>(decode(r.transport->escaped[2])).world.coordinate_space.revision==1);
 });
 run("sender reconnect retires pending work and does not reauthorize",[&]{Rig r(root/"transport-reconnect");r.live();r.transport->mode=DatagramSubmission::Pending;auto old=r.sender->pump();auto before=r.sender->snapshot();
  CHECK(r.sender->connect());auto after=r.sender->snapshot();CHECK(after.transportGeneration>before.transportGeneration && after.publisherGeneration>before.publisherGeneration && !after.enabled && after.queued==0);
  CHECK(r.sender->complete(old.completion,{DatagramSubmission::Accepted,r.transport->escaped[0].size()})=="stale-completion");CHECK(r.sender->pump().status=="no-ready-head");
  r.transport->mode=DatagramSubmission::Accepted;CHECK(r.sender->enablePublisher(r.receipt,S3,S4,6)=="queued");r.drain();CHECK(seq(decode(r.transport->escaped[1]))==0);
 });
 run("disable re-enable needs fresh publisher session and rejects old reservation",[&]{Rig r(root/"disable");r.live();auto old=r.sender->prepare();auto before=r.sender->snapshot();r.sender->disablePublisher();
  CHECK(r.sender->enablePublisher(r.receipt,S1,S2,5)=="session-rejected");CHECK(r.sender->enablePublisher(r.receipt,S3,S4,6)=="queued");
  CHECK(r.sender->snapshot().publisherGeneration>before.publisherGeneration);CHECK(r.sender->submit(old).status=="stale-attempt");r.drain();CHECK(r.transport->escaped.size()==2);
 });
 run("pending head excludes later frames; completion order is authoritative",[&]{Rig r(root/"pending-order");r.live();r.transport->mode=DatagramSubmission::Pending;auto p=r.sender->pump();r.pose();
  CHECK(r.sender->snapshot().queued==3 && r.sender->pump().status=="no-ready-head" && r.transport->calls==1);
  CHECK(r.sender->complete(p.completion,{DatagramSubmission::Accepted,r.transport->escaped[0].size()})=="local-accepted");r.transport->mode=DatagramSubmission::Accepted;r.drain();
  CHECK(r.transport->escaped.size()==3);for(int i=0;i<3;++i)CHECK(seq(decode(r.transport->escaped[i]))==i);
 });
 run("concurrent duplicate source callbacks cannot reorder queue insertion",[&]{Rig r(root/"concurrent");r.ready();std::vector<std::future<CommonWorldSenderSourceResult>> calls;
  for(int i=0;i<8;++i)calls.push_back(std::async(std::launch::async,[&]{return r.source.receive(r.f.p,5);}));int poses=0,duplicates=0;
  for(auto& f:calls){auto a=f.get();poses+=a.source.status=="pose";duplicates+=a.source.status=="duplicate";}CHECK(poses==1 && duplicates==7 && r.sender->snapshot().queued==1);r.drain();
 });
 run("known-zero rejection bounded retry never lets mapping overtake world",[&]{Rig r(root/"retry");r.live();r.transport->mode=DatagramSubmission::Rejected;
  CHECK(r.sender->pump().status=="retryable-rejection");CHECK(r.sender->pump().status=="retryable-rejection");CHECK(r.sender->snapshot().queued==2 && !r.sender->snapshot().lastLocalAcceptedSequence);
  r.transport->mode=DatagramSubmission::Accepted;r.drain();CHECK(r.transport->escaped.size()==2);
  CHECK(r.transport->attempted.size()==4);for(int i=0;i<3;++i)CHECK(seq(decode(r.transport->attempted[i]))==0);CHECK(seq(decode(r.transport->attempted[3]))==1);
 });
 run("retry exhaustion fails closed without storms or overtaking",[&]{Rig r(root/"exhaust",64,2);r.live();r.transport->mode=DatagramSubmission::Rejected;
  CHECK(r.sender->pump().status=="retryable-rejection");CHECK(r.sender->pump().status=="retry-exhausted");CHECK(!r.sender->snapshot().enabled && r.sender->snapshot().queued==0);
  for(int i=0;i<20;++i)CHECK(r.sender->pump().status=="no-ready-head");CHECK(r.transport->calls==2 && r.store->matchesCurrentCommit(r.receipt));
 });
 run("second bootstrap datagram failure holds later pose behind mapping",[&]{Rig r(root/"mapping-fail");r.live();CHECK(r.sender->pump().status=="local-accepted");r.transport->mode=DatagramSubmission::Rejected;r.pose();
  CHECK(r.sender->pump().status=="retryable-rejection");CHECK(r.sender->snapshot().queued==2 && r.sender->snapshot().lastLocalAcceptedSequence==0);
  r.transport->mode=DatagramSubmission::Accepted;r.drain();CHECK(r.transport->escaped.size()==3);
  for(int i=0;i<3;++i)CHECK(seq(decode(r.transport->escaped[i]))==i);
 });
 run("new durable authority cannot bypass failing old publisher generation",[&]{Rig r(root/"revision-fail");r.live();r.transport->mode=DatagramSubmission::Rejected;CHECK(r.sender->pump().status=="retryable-rejection");auto old=r.sender->prepare();
  r.f.a.sequence=1;++r.f.a.source_space.source_space_generation;auto changed=r.source.receive(r.f.a,5);CHECK(changed.source.status=="authority" && changed.publication=="authorization-retired");
  CHECK(r.sender->submit(old).status=="stale-attempt");auto c=r.sender->commitSource(r.source,6);CHECK(c.eligible && c.receipt->committed.worldRevision==1);
  CHECK(!r.sender->snapshot().enabled && r.transport->escaped.empty());r.transport->mode=DatagramSubmission::Accepted;CHECK(r.sender->enablePublisher(*c.receipt,S3,S4,7)=="queued");r.drain();
  CHECK(std::get<w::CommonWorldAuthorityPublication>(decode(r.transport->escaped[0])).world.coordinate_space.revision==1);
 });
 run("bounded queue saturation is explicit whole-generation failure",[&]{Rig r(root/"saturation",3);r.live();r.pose();CHECK(r.sender->snapshot().queued==3);
  r.f.p.sequence=2;++r.f.p.source.observation_id;auto overflow=r.source.receive(r.f.p,8);CHECK(overflow.source.status=="pose" && overflow.publication=="queue-overflow");
  CHECK(r.sender->snapshot().queued==0 && !r.sender->snapshot().enabled && r.transport->calls==0);
  for(int i=3;i<100;++i){r.f.p.sequence=i;++r.f.p.source.observation_id;auto v=r.source.receive(r.f.p,8+i);CHECK(v.source.status=="pose" && v.publication=="publisher-disabled");CHECK(r.sender->snapshot().queued==0);}
  CHECK(r.sender->enablePublisher(r.receipt,S3,S4,110)=="queued");r.drain();
 });
 run("connect failure never grants publication or changes durable store",[&]{Rig r(root/"connect-fail");r.durable();r.transport->connectFail=true;CHECK(!r.sender->connect());
  CHECK(r.sender->enablePublisher(r.receipt,S1,S2,4)=="transport-unavailable");CHECK(r.transport->calls==0 && r.store->matchesCurrentCommit(r.receipt));
 });
 run("transport exception has unknown outcome and no cursor/retry",[&]{Rig r(root/"exception");r.live();r.transport->throwSubmit=true;
  CHECK(r.sender->pump().status=="submission-exception-ambiguous");CHECK(!r.sender->snapshot().lastLocalAcceptedSequence && !r.sender->snapshot().connected);
  CHECK(r.sender->pump().status=="no-ready-head" && r.store->matchesCurrentCommit(r.receipt));
 });
 for(auto mode:{DatagramSubmission::Ambiguous,DatagramSubmission::TimedOut,DatagramSubmission::Cancelled})run("unknown/disconnect/timeout/cancel outcome retires generation "+std::to_string(int(mode)),[&,mode]{Rig r(root/("failure-"+std::to_string(int(mode))));r.live();r.transport->mode=mode;
  CHECK(r.sender->pump().status=="ambiguous-retired");CHECK(!r.sender->snapshot().lastLocalAcceptedSequence && r.sender->snapshot().queued==0 && !r.sender->snapshot().connected);
 });
 run("partial datagram acceptance violates contract and is never publication success",[&]{Rig r(root/"partial");r.live();r.transport->accepted=1;
  CHECK(r.sender->pump().status=="ambiguous-retired");CHECK(!r.sender->snapshot().lastLocalAcceptedSequence && r.transport->escaped.size()==1);
 });
 run("cancellation before final send gate prevents all API calls",[&]{Rig r(root/"cancel-before");r.live();std::atomic_bool cancelled{true};auto a=r.sender->prepare();
  CHECK(r.sender->submit(a,&cancelled).status=="cancelled-before-send");CHECK(r.transport->calls==0 && r.sender->snapshot().queued==0);
 });
 run("pending timeout then duplicate/late completion cannot advance new session",[&]{Rig r(root/"late-timeout");r.live();r.transport->mode=DatagramSubmission::Pending;auto old=r.sender->pump();
  CHECK(r.sender->complete(old.completion,{DatagramSubmission::TimedOut,0})=="ambiguous-retired");CHECK(r.sender->connect());
  CHECK(r.sender->enablePublisher(r.receipt,S3,S4,5)=="queued");CHECK(r.sender->complete(old.completion,{DatagramSubmission::Accepted,r.transport->escaped[0].size()})=="stale-completion");
  CHECK(!r.sender->snapshot().lastLocalAcceptedSequence);r.transport->mode=DatagramSubmission::Accepted;r.drain();
 });
 run("duplicate local completion only pops one head",[&]{Rig r(root/"duplicate-completion");r.live();r.transport->mode=DatagramSubmission::Pending;auto p=r.sender->pump();auto count=r.transport->escaped[0].size();
  CHECK(r.sender->complete(p.completion,{DatagramSubmission::Accepted,count})=="local-accepted");CHECK(r.sender->complete(p.completion,{DatagramSubmission::Accepted,count})=="stale-completion");
  CHECK(r.sender->snapshot().queued==1 && r.sender->snapshot().lastLocalAcceptedSequence==0);
 });
 run("disconnect/dispose while pending releases queue without waiting",[&]{Rig r(root/"dispose");r.live();r.transport->mode=DatagramSubmission::Pending;auto p=r.sender->pump();
  r.sender->disconnect();r.sender->dispose();r.sender->dispose();CHECK(r.sender->snapshot().queued==0 && r.sender->snapshot().disposed);
  CHECK(r.sender->complete(p.completion,{DatagramSubmission::Accepted,1})=="stale-completion");CHECK(r.source.receive(r.f.p,5).source.status=="disposed");
 });
 run("source transport loss after preparation excludes not-yet-entered send",[&]{Rig r(root/"source-stop");r.live();auto a=r.sender->prepare();r.sender->stopSource(r.source,5);
  CHECK(r.sender->submit(a).status=="stale-attempt" && r.transport->calls==0 && !r.sender->snapshot().ingress.sourceActive);
 });
 run("delayed durable commit revoked before receipt cannot enter sender",[&]{Pause pause;Rig r(root/"durable-delay",64,3,[&](auto b){pause.hook(b);});
  r.source=r.sender->startSource(1);CHECK(r.sender->authorizeSource(r.source,r.f.a.publisher_id,r.f.a.session_id,r.f.a.clock_id,r.f.a,2).status=="authority");pause.armed=true;
  auto work=std::async(std::launch::async,[&]{return r.sender->commitSource(r.source,3);});pause.wait();r.sender->revokeOwner(4);CHECK(!r.sender->snapshot().enabled);pause.release();auto done=work.get();
  CHECK(done.receipt && !done.eligible);CHECK(r.sender->connect());CHECK(r.sender->enablePublisher(*done.receipt,S1,S2,5)=="receipt-ineligible" && r.transport->calls==0);
 });
 run("real production codec bytes and finite binary64/identity exact round trip",[&]{Rig r(root/"wire");r.ready();
  r.f.p.position={-0.0,std::numeric_limits<double>::denorm_min(),1.2345678901234567};for(auto& n:r.f.p.orientation)n=-n;const auto frozen=r.f.p;
  auto result=r.source.receive(frozen,5);CHECK(result.source.status=="pose" && result.publication=="queued");r.drain();const auto& bytes=r.transport->escaped.back();
  std::string expected;w::Error error;CHECK(w::EncodeEnvelope(result.source.events[0],expected,error));CHECK(bytes==expected && bytes.size()<=4096);
  const auto pose=std::get<w::TrustedHmdCommonPose>(decode(bytes));CHECK(d::bits(pose.common_position)==d::bits(frozen.position));CHECK(d::bits(pose.source_position)==d::bits(frozen.position));
  CHECK(d::bits(pose.common_orientation)==d::bits(frozen.orientation));CHECK(pose.source.source_locate_time_ns==frozen.source.source_locate_time_ns);
  CHECK(pose.source.source_time_domain_id==frozen.source.source_time_domain_id && pose.source.observation_id==frozen.source.observation_id);
  CHECK(d::source(pose.mapping.source_space)==d::source(frozen.source.source_space) && pose.mapping.mapping_revision==r.receipt.committed.mappingRevision);
  CHECK(pose.mapping.world.owner_id=="synthetic-bridge" && pose.mapping.world.world_epoch==r.sender->snapshot().ingress.runtime.world->world.world_epoch);
  CHECK(pose.session_id==S1 && pose.clock_id==S2 && pose.timestamp_ns==5 && pose.sent_at_ns==5 && pose.version.minor==1);
  r.f.p.position[0]=99;CHECK(std::get<w::TrustedHmdCommonPose>(decode(bytes)).common_position[0]==0 && std::signbit(std::get<w::TrustedHmdCommonPose>(decode(bytes)).common_position[0]));
 });
 for(int variant=0;variant<3;++variant)run("nonfinite input remains forbidden at actual codec "+std::to_string(variant),[&,variant]{Rig r(root/("nan-"+std::to_string(variant)));r.ready();
  std::uint64_t payload=0xfff8000000000123ULL;double nan;std::memcpy(&nan,&payload,8);
  r.f.p.position[0]=variant==0?std::numeric_limits<double>::infinity():variant==1?-std::numeric_limits<double>::infinity():nan;
  CHECK(r.source.receive(r.f.p,5).source.status=="malformed");CHECK(r.sender->pump().status=="no-ready-head" && r.transport->escaped.size()==2);
 });
 run("codec framing rejection, no invented checksum or application ACK",[&]{Rig r(root/"framing");r.ready();auto b=r.transport->escaped[0];b.resize(b.size()/2);w::Envelope e;w::Error error;
  CHECK(!w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(b.data()),b.size(),e,error));b=std::string(4097,'x');CHECK(!w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(b.data()),b.size(),e,error));
  b=r.transport->escaped[0];b[0]='!';CHECK(!w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(b.data()),b.size(),e,error));
 });
 run("actual existing UDP loopback API and byte-level decoder",[&]{SourceFixture f;fs::create_directories(root/"loopback");auto store=std::make_shared<CommonWorldRevisionStore>(makeCommonWorldFileStorage(root/"loopback/world.rev"),CommonWorldPersistentHighWater{"synthetic-bridge","common-A",{},0});
  mb::Udp sink;CommonWorldLiveSender sender(store,"loopback-epoch",f.a.publisher_id,makeCommonWorldLoopbackDatagramTransport(sink.port()));CHECK(sender.connect());auto source=sender.startSource(1);
  CHECK(sender.authorizeSource(source,f.a.publisher_id,f.a.session_id,f.a.clock_id,f.a,2).status=="authority");auto c=sender.commitSource(source,3);CHECK(c.receipt && c.eligible);
  CHECK(sender.enablePublisher(*c.receipt,S1,S2,4)=="queued");
  for(int i=0;i<2;++i){CHECK(sender.pump().status=="local-accepted");auto packet=receivePacket(sink);CHECK(seq(decode(packet.bytes))==i);CHECK(packet.peer.find("127.0.0.1:")==0);}
  CHECK(source.receive(f.p,5).publication=="queued");CHECK(sender.pump().status=="local-accepted");auto packet=receivePacket(sink);auto pose=std::get<w::TrustedHmdCommonPose>(decode(packet.bytes));CHECK(d::bits(pose.common_position)==d::bits(f.p.position));
  bool rejected=false;try{makeCommonWorldLoopbackDatagramTransport(29812);}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);
 });
 run("restart restores no queue/cursor/session; explicit successor cannot reorder",[&]{auto dir=root/"restart";Rig r(dir);r.live();r.transport->mode=DatagramSubmission::Pending;auto ambiguous=r.sender->pump();CHECK(!r.sender->snapshot().lastLocalAcceptedSequence);
  r.sender->dispose();r.store->dispose();auto store=std::make_shared<CommonWorldRevisionStore>(makeCommonWorldFileStorage(dir/"world.rev"),CommonWorldPersistentHighWater{"synthetic-bridge","common-A",{},0});
  auto t=std::make_unique<Fake>();auto* raw=t.get();CommonWorldLiveSender fresh(store,"restart-epoch",r.f.a.publisher_id,std::move(t));auto s=fresh.snapshot();CHECK(!s.connected && !s.enabled && s.queued==0 && !s.lastLocalAcceptedSequence && !s.ingress.sourceActive);
  CHECK(fresh.complete(ambiguous.completion,{DatagramSubmission::Accepted,1})=="stale-completion");CHECK(fresh.connect());CHECK(fresh.enablePublisher(r.receipt,S3,S4,6)=="receipt-ineligible");
  auto c=fresh.startSource(7);CHECK(fresh.authorizeSource(c,r.f.a.publisher_id,r.f.a.session_id,r.f.a.clock_id,r.f.a,8).status=="authority");auto commit=fresh.commitSource(c,9);CHECK(commit.eligible && commit.receipt->committed.worldRevision==1);
  CHECK(fresh.enablePublisher(*commit.receipt,S3,S4,10)=="queued");CHECK(fresh.pump().status=="local-accepted");CHECK(std::get<w::CommonWorldAuthorityPublication>(decode(raw->escaped[0])).world.coordinate_space.revision==1);
 });
 run("actual sender bytes generate independent real receiver replay",[&]{auto fixtures=fs::path(ROOT_DIR)/"build/live-sender-fixtures";fs::create_directories(fixtures);
  for(int scenario=0;scenario<3;++scenario){Rig r(root/("generated-"+std::to_string(scenario)));r.ready();r.pose();r.drain();
   if(scenario==0){r.pose(2);r.drain();}else if(scenario==1){CHECK(r.sender->connect());CHECK(!r.sender->snapshot().enabled);
    CHECK(r.sender->enablePublisher(r.receipt,S3,S4,14)=="queued");r.drain();r.f.p.sequence=2;r.f.p.source.observation_id=2;
    auto pose=r.source.receive(r.f.p,15);CHECK(pose.source.status=="pose" && pose.publication=="queued");r.drain();
   }else{r.sender->revokeOwner(10);r.f.reconnect();r.source=r.sender->startSource(11);
    CHECK(r.sender->authorizeSource(r.source,r.f.a.publisher_id,r.f.a.session_id,r.f.a.clock_id,r.f.a,12).status=="authority");auto c=r.sender->commitSource(r.source,13);CHECK(c.eligible && c.receipt->committed.worldRevision==1);
    CHECK(r.sender->enablePublisher(*c.receipt,S3,S4,14)=="queued");r.drain();CHECK(r.source.receive(r.f.p,15).publication=="queued");r.drain();}
   const char* names[]={"sender-normal.jsonl","sender-reconnect.jsonl","sender-owner-handover.jsonl"};
   std::ofstream out(fixtures/names[scenario]);CHECK(out.good());for(const auto& b:r.transport->escaped)out<<b<<'\n';CHECK(out.good());
  }
 });
 std::cout<<"PASS "<<cases<<" cases, "<<checks<<" assertions; real codec, ordered bounded local datagram submission; no ACK/checkpoint/exactly-once"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
