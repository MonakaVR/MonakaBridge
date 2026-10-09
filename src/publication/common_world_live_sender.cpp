#include "monaka_bridge/common_world_live_sender.hpp"
#include "monaka_bridge/network.hpp"
#include "../authority/common_world_facts.hpp"
#include <deque>
#include <stdexcept>

namespace monaka_bridge {
namespace w=authority_wire;
namespace d=common_world_detail;
namespace {
class LoopbackDatagram final:public CommonWorldDatagramTransport {
 std::uint16_t port_; std::unique_ptr<mb::Udp> socket_;
public:
 explicit LoopbackDatagram(std::uint16_t port):port_(port) {
  if(!port || (port>=29810 && port<=29813)) throw std::invalid_argument("caller-owned non-production loopback test port required");
 }
 bool connect() override { socket_.reset(); try { socket_=std::make_unique<mb::Udp>(); return true; } catch(...) { return false; } }
 void close() noexcept override { socket_.reset(); }
 DatagramSubmissionResult submit(std::string_view bytes) override {
  if(!socket_ || bytes.empty() || bytes.size()>4096) return {DatagramSubmission::Rejected,0};
  // Existing synchronous nonblocking sendto, full datagram local return only.
  // Its bool API has no error classification or ACK: false conservatively means ambiguity.
  return socket_->send(port_,bytes)?DatagramSubmissionResult{DatagramSubmission::Accepted,bytes.size()}:
   DatagramSubmissionResult{DatagramSubmission::Ambiguous,0};
 }
};
bool sameContext(const CommonWorldLiveSnapshot& a,const CommonWorldLiveSnapshot& b) {
 return a.sourceActive && b.sourceActive && a.ownerApproved && b.ownerApproved && !a.disposed && !b.disposed &&
  a.sourceGeneration==b.sourceGeneration && a.runtime.world && b.runtime.world && a.runtime.mapping && b.runtime.mapping &&
  d::world(a.runtime.world->world)==d::world(b.runtime.world->world) &&
  d::source(a.runtime.world->anchor_source)==d::source(b.runtime.world->anchor_source) &&
  d::mapping(a.runtime.mapping->mapping)==d::mapping(b.runtime.mapping->mapping) &&
  d::transform(a.runtime.mapping->transform)==d::transform(b.runtime.mapping->transform);
}
bool receiptContext(const CommonWorldDurableReceipt& r,const CommonWorldLiveSnapshot& s) {
 if(!s.sourceActive || !s.ownerApproved || s.disposed || !s.runtime.world || !s.runtime.mapping || s.runtime.pending) return false;
 const auto& c=r.committed; const auto& world=s.runtime.world->world; const auto& map=*s.runtime.mapping;
 if(c.worldRevision!=world.coordinate_space.revision || c.mappingRevision!=map.mapping.mapping_revision) return false;
 if(c.operation==RevisionOperation::Transition)
  return c.transition.ownerId==world.owner_id && c.transition.worldSpaceId==world.coordinate_space.id &&
   d::source(c.transition.anchor.source_space)==d::source(s.runtime.world->anchor_source);
 if(c.operation==RevisionOperation::MappingUpdate)
  return d::mapping(c.mapping.mapping)==d::mapping(map.mapping) && d::transform(c.mapping.transform)==d::transform(map.transform);
 return c.operation==RevisionOperation::MappingReestablishment && d::world(c.reestablishment.world)==d::world(world) &&
  d::source(c.reestablishment.anchor.source_space)==d::source(s.runtime.world->anchor_source);
}
std::int64_t sequence(const w::Envelope& e) { return std::visit([](const auto& m){return m.sequence;},e); }
}
std::unique_ptr<CommonWorldDatagramTransport> makeCommonWorldLoopbackDatagramTransport(std::uint16_t p) { return std::make_unique<LoopbackDatagram>(p); }
struct CommonWorldLiveSender::Plan {
 w::Envelope envelope;
 CommonWorldDurableReceipt receipt;
 CommonWorldLiveSnapshot context;
 std::uint64_t publisherGeneration,transportGeneration,id;
};
struct CommonWorldLiveSender::State {
 mutable std::mutex mutex;
 std::shared_ptr<CommonWorldRevisionStore> store;
 CommonWorldLiveIngress ingress;
 std::unique_ptr<CommonWorldDatagramTransport> transport;
 std::size_t capacity,retryLimit,headAttempts=0,frameSize=0;
 bool connected=false,enabled=false,disposed=false,awaiting=false;
 std::uint64_t publisherGeneration=0,transportGeneration=0,attemptId=0;
 std::deque<Plan> queue;
 std::shared_ptr<const Plan> plan;
 std::optional<CommonWorldDurableReceipt> receipt;
 std::optional<CommonWorldLiveSnapshot> context;
 std::optional<std::int64_t> cursor;
 State(std::shared_ptr<CommonWorldRevisionStore> s,std::string e,std::string p,std::unique_ptr<CommonWorldDatagramTransport> t,std::size_t cap,std::size_t limit)
  :store(s),ingress(std::move(s),std::move(e),std::move(p)),transport(std::move(t)),capacity(cap),retryLimit(limit) {
  if(!transport || cap<2 || !limit || limit>32) throw std::invalid_argument("transport, capacity >=2 and retry limit 1..32 required");
 }
 void increment(std::uint64_t& g) {
  if(g==UINT64_MAX) { disposed=true; enabled=false; connected=false; queue.clear(); plan.reset(); transport->close(); throw std::runtime_error("sender generation exhausted"); }
  ++g;
 }
 void retire() { enabled=false; queue.clear(); plan.reset(); awaiting=false; headAttempts=0; receipt.reset(); context.reset(); increment(publisherGeneration); }
 bool current(const std::shared_ptr<const Plan>& p) const {
  return !disposed && enabled && connected && p && plan==p && !queue.empty() &&
   p->publisherGeneration==publisherGeneration && p->transportGeneration==transportGeneration;
 }
 std::string enqueue(const std::vector<w::Envelope>& events) {
  if(events.empty()) return "no-publication";
  if(!enabled || !receipt || !context) return "publisher-disabled";
  if(events.size()>capacity-queue.size()) { retire(); return "queue-overflow"; }
  for(const auto& e:events) queue.push_back({e,*receipt,*context,publisherGeneration,transportGeneration,0});
  return "queued";
 }
 std::string outcome(const std::shared_ptr<const Plan>& p,DatagramSubmissionResult result) {
  if(!current(p)) return "stale-completion";
  if(result.status==DatagramSubmission::Accepted && result.acceptedBytes==frameSize) {
   cursor=sequence(p->envelope); queue.pop_front(); plan.reset(); awaiting=false; headAttempts=0; return "local-accepted";
  }
  if(result.status==DatagramSubmission::Rejected && result.acceptedBytes==0) {
   plan.reset(); awaiting=false;
   if(headAttempts<retryLimit) return "retryable-rejection";
   retire(); return "retry-exhausted";
  }
  // Partial datagram, timeout/cancellation after entry or unknown outcome: no cursor,
  // no retransmit of an ambiguously escaped frame, no overtaking in this generation.
  retire(); connected=false; transport->close(); increment(transportGeneration); return "ambiguous-retired";
 }
 CommonWorldSenderSourceResult receive(const Source& c,const w::Envelope& event,std::int64_t at) {
  std::lock_guard<std::mutex> guard(mutex);
  if(disposed) return {{"disposed",{}},"publisher-disabled"};
  auto result=c.inner_.receive(event,at);
  auto snapshot=ingress.snapshot();
  if(enabled && (!context || !sameContext(*context,snapshot))) { retire(); return {std::move(result),"authorization-retired"}; }
  auto publication=enqueue(result.events); return {std::move(result),std::move(publication)};
 }
};
CommonWorldLiveSender::CommonWorldLiveSender(std::shared_ptr<CommonWorldRevisionStore> s,std::string e,std::string p,
 std::unique_ptr<CommonWorldDatagramTransport> t,std::size_t cap,std::size_t retry)
 :state_(std::make_shared<State>(std::move(s),std::move(e),std::move(p),std::move(t),cap,retry)) {}
CommonWorldLiveSender::~CommonWorldLiveSender() { dispose(); }
CommonWorldSenderSourceResult CommonWorldLiveSender::Source::receive(const w::Envelope& e,std::int64_t at) const {
 auto s=state_.lock(); if(!s) return {{"disposed",{}},"publisher-disabled"}; return s->receive(*this,e,at);
}
CommonWorldLiveSender::Source CommonWorldLiveSender::startSource(std::int64_t at) {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex);
 if(s->disposed) throw std::runtime_error("sender disposed"); s->retire();
 Source c; c.state_=s; c.inner_=s->ingress.start(at); return c;
}
CommonWorldOperationResult CommonWorldLiveSender::authorizeSource(const Source& c,const std::string& p,const std::string& session,const std::string& clock,const w::Envelope& e,std::int64_t at) {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex);
 if(s->disposed || c.state_.lock()!=s) return {"stale-source",{}};
 return s->ingress.authorize(c.inner_,p,session,clock,e,at);
}
CommonWorldLiveCommitResult CommonWorldLiveSender::commitSource(Source c,std::int64_t at,const std::atomic_bool* cancel) {
 auto s=state_;
 if(c.state_.lock()!=s) return {"stale-source",{},{},false,{}};
 // 5AE handles all delayed source/owner fences. Never hold sender lock during I/O.
 return s->ingress.commitPending(c.inner_,at,cancel);
}
void CommonWorldLiveSender::stopSource(const Source& c,std::int64_t at) {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex);
 if(s->disposed || c.state_.lock()!=s || c.generation()!=s->ingress.snapshot().sourceGeneration) return;
 s->retire(); s->ingress.stop(c.inner_,at);
}
void CommonWorldLiveSender::revokeOwner(std::int64_t at) {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex); if(s->disposed) return; s->retire(); s->ingress.revokeOwner(at);
}
bool CommonWorldLiveSender::connect() {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex); if(s->disposed) return false;
 s->retire(); s->transport->close(); s->connected=false; s->increment(s->transportGeneration);
 try { s->connected=s->transport->connect(); } catch(...) { s->connected=false; } return s->connected;
}
void CommonWorldLiveSender::disconnect() {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex); if(s->disposed) return;
 s->retire(); s->connected=false; s->transport->close(); s->increment(s->transportGeneration);
}
std::string CommonWorldLiveSender::enablePublisher(const CommonWorldDurableReceipt& receipt,const std::string& session,const std::string& clock,std::int64_t at) {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex);
 if(s->disposed || !s->connected) return "transport-unavailable";
 if(s->enabled || s->plan || !s->queue.empty()) return "disable-required";
 const auto snapshot=s->ingress.snapshot(); if(!receiptContext(receipt,snapshot)) return "receipt-ineligible";
 bool accepted=false;
 try { s->store->acceptCurrentCommit(receipt,[&] { accepted=sameContext(snapshot,s->ingress.snapshot()); }); }
 catch(...) { s->retire(); return "authorization-unavailable"; }
 if(!accepted) return "receipt-ineligible";
 // Do NOT nest runtime store operations inside acceptCurrentCommit's callback.
 auto r=s->ingress.beginDryRunPublisherSession(session,clock,at);
 if(r.status!="session" || r.events.size()!=2 || !sameContext(snapshot,s->ingress.snapshot())) return "session-rejected";
 s->increment(s->publisherGeneration); s->enabled=true; s->receipt=receipt; s->context=snapshot; s->cursor.reset(); s->headAttempts=0;
 return s->enqueue(r.events);
}
void CommonWorldLiveSender::disablePublisher() { auto s=state_; std::lock_guard<std::mutex> g(s->mutex); if(!s->disposed) s->retire(); }
CommonWorldLiveSender::Attempt CommonWorldLiveSender::prepare() {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex); Attempt a;
 if(s->disposed || !s->connected || !s->enabled || s->queue.empty() || s->plan) return a;
 s->increment(s->attemptId); auto p=s->queue.front(); p.id=s->attemptId; s->plan=std::make_shared<const Plan>(std::move(p)); a.plan_=s->plan; return a;
}
CommonWorldLiveSender::PumpResult CommonWorldLiveSender::submit(Attempt a,const std::atomic_bool* cancel) {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex);
 if(!s->current(a.plan_) || s->awaiting) return {"stale-attempt",{}};
 if(cancel && cancel->load()) { s->retire(); return {"cancelled-before-send",{}}; }
 PumpResult result{"authorization-unavailable",{}}; bool gated=false,entered=false;
 try {
  gated=s->store->acceptCurrentCommit(a.plan_->receipt,[&] {
   // Outer mutex excludes EVERY source/Owner/publisher/transport mutation in this
   // composition; store gate excludes epoch handover. Known codec + nonblocking
   // submit run in this same short critical section, no cached authorization.
   if(!s->current(a.plan_) || !sameContext(a.plan_->context,s->ingress.snapshot()) || !receiptContext(a.plan_->receipt,s->ingress.snapshot())) return;
   if(cancel && cancel->load()) { result.status="cancelled-before-send"; return; }
   std::string bytes; w::Error error;
   if(!w::EncodeEnvelope(a.plan_->envelope,bytes,error)) { result.status="serialization-failed"; return; }
   if(cancel && cancel->load()) { result.status="cancelled-before-send"; return; }
   s->frameSize=bytes.size(); ++s->headAttempts;
   // SEND LINEARIZATION POINT: entry into the nonblocking local datagram API.
   // Revocation effective before this entry excludes send; after entry cannot recall bytes.
   entered=true;
   const auto submitted=s->transport->submit(bytes);
   if(submitted.status==DatagramSubmission::Pending && submitted.acceptedBytes==0) {
    s->awaiting=true; result={"pending-local-completion",a};
   } else result.status=s->outcome(a.plan_,submitted);
  });
 } catch(...) { s->retire(); s->connected=false; s->transport->close(); s->increment(s->transportGeneration); return {entered?"submission-exception-ambiguous":"authorization-exception-before-send",{}}; }
 if(!gated || result.status=="authorization-unavailable" || result.status=="cancelled-before-send" || result.status=="serialization-failed") s->retire();
 return result;
}
CommonWorldLiveSender::PumpResult CommonWorldLiveSender::pump(const std::atomic_bool* c) {
 auto a=prepare(); return a?submit(a,c):PumpResult{"no-ready-head",{}};
}
std::string CommonWorldLiveSender::complete(Attempt a,DatagramSubmissionResult r) {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex);
 if(!s->current(a.plan_) || !s->awaiting) return "stale-completion";
 bool current=false;
 try { s->store->acceptCurrentCommit(a.plan_->receipt,[&] {current=sameContext(a.plan_->context,s->ingress.snapshot());}); }
 catch(...) { s->retire(); return "authorization-unavailable"; }
 if(!current) { s->retire(); return "stale-completion"; }
 return s->outcome(a.plan_,r);
}
void CommonWorldLiveSender::dispose() {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex); if(s->disposed) return;
 s->retire(); s->connected=false; s->increment(s->transportGeneration); s->transport->close(); s->disposed=true; s->ingress.dispose();
}
CommonWorldSenderSnapshot CommonWorldLiveSender::snapshot() const {
 auto s=state_; std::lock_guard<std::mutex> g(s->mutex);
 return {s->connected,s->enabled,s->disposed,s->awaiting,s->publisherGeneration,s->transportGeneration,s->queue.size(),s->capacity,s->headAttempts,s->cursor,s->ingress.snapshot()};
}
}
