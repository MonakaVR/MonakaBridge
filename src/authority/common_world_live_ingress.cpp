#include "monaka_bridge/common_world_live_ingress.hpp"
#include <stdexcept>

namespace monaka_bridge {
namespace w = authority_wire;
struct CommonWorldLiveIngress::State {
 std::mutex mutex;
 PersistentCommonWorldRuntime runtime;
 std::uint64_t generation = 0;
 bool active = false, approved = false, attempted = false, busy = false, disposed = false;
 std::optional<CommonWorldPersistenceTicket> retry;
 State(std::shared_ptr<CommonWorldRevisionStore> store,std::string epoch,std::string publisher,std::size_t capacity)
  :runtime(std::move(store),std::move(epoch),std::move(publisher),capacity) {}
 bool current(std::uint64_t g) const { return !disposed && active && g==generation; }
 void close(std::int64_t at) {
  active=false; approved=false; retry.reset(); // effective before runtime cleanup
  runtime.disconnect(at);
 }
 CommonWorldOperationResult receive(std::uint64_t g,const w::Envelope& event,std::int64_t at) {
  std::lock_guard<std::mutex> guard(mutex);
  if(!current(g)) return {disposed?"disposed":"stale-source",{}};
  if(!approved) return {"owner-not-approved",{}};
  if(busy) return {"backpressure",{}}; // NOT admitted; caller retains/retries the same event
  try {
   auto result=runtime.receiveSource(event,at);
   if(result.status=="authority" && retry) {
    const auto pending=runtime.snapshot().pending;
    if(!pending || pending->requestId!=retry->candidate.transition.requestId) retry.reset();
   }
   if(result.status=="authorization-unavailable" || result.status=="gap" || result.status=="conflict" ||
      result.status=="malformed" || result.status=="bootstrap-failed" || result.status=="closed" ||
      result.status=="untrusted" || result.status=="observation-conflict" || result.status=="capacity-exhausted") close(at);
   return result;
  } catch(...) { close(at); throw; }
 }
};
CommonWorldLiveIngress::CommonWorldLiveIngress(std::shared_ptr<CommonWorldRevisionStore> s,std::string e,std::string p,std::size_t c)
 :state_(std::make_shared<State>(std::move(s),std::move(e),std::move(p),c)) {}
CommonWorldLiveIngress::~CommonWorldLiveIngress() { dispose(); }
CommonWorldOperationResult CommonWorldLiveIngress::Callback::receive(const w::Envelope& value,std::int64_t at) const {
 auto s=state_.lock(); if(!s) return {"disposed",{}};
 // The typed value is copied by the receiver; callers must not mutate it during this call.
 return s->receive(generation_,value,at);
}
CommonWorldLiveIngress::Callback CommonWorldLiveIngress::start(std::int64_t at) {
 auto s=state_; std::lock_guard<std::mutex> guard(s->mutex);
 if(s->disposed) throw std::runtime_error("ingress disposed");
 if(s->active) s->close(at);
 if(s->generation==UINT64_MAX) { s->disposed=true; s->runtime.dispose(); throw std::runtime_error("source generation exhausted"); }
 ++s->generation; s->active=true; s->approved=false; s->attempted=false; s->retry.reset();
 return Callback(s,s->generation);
}
CommonWorldOperationResult CommonWorldLiveIngress::authorize(const Callback& c,const std::string& p,const std::string& session,
 const std::string& clock,const w::Envelope& bootstrap,std::int64_t at) {
 auto s=state_; std::lock_guard<std::mutex> guard(s->mutex);
 if(c.state_.lock()!=s || !s->current(c.generation_)) return {"stale-source",{}};
 if(s->busy) return {"backpressure",{}};
 if(s->attempted) return {"approval-retired",{}};
 s->attempted=true;
 try {
  auto r=s->runtime.beginSourceSession(p,session,clock,bootstrap,at);
  s->approved=r.status=="authority";
  if(!s->approved) s->close(at);
  return r;
 } catch(...) { s->close(at); throw; }
}
CommonWorldLiveCommitResult CommonWorldLiveIngress::commitPending(const Callback c,std::int64_t at,const std::atomic_bool* cancel) {
 auto s=state_;
 std::optional<CommonWorldPersistenceTicket> ticket;
 {
  std::lock_guard<std::mutex> guard(s->mutex);
  if(c.state_.lock()!=s || !s->current(c.generation_)) return {"stale-source",{},{},false};
  if(!s->approved) return {"owner-not-approved",{},{},false};
  if(s->busy) return {"backpressure",{},{},false};
  s->busy=true; ticket=s->retry;
 }
 // One slot across ALL generations. Neither open nor commit holds the lifecycle lock.
 // Cleanup releases the slot even on an exception; stop/revoke/dispose never wait for it.
 struct Release {
  std::shared_ptr<State> s;
  ~Release() { std::lock_guard<std::mutex> guard(s->mutex); s->busy=false; }
 } release{s};
 try {
  if(!ticket) ticket=s->runtime.prepareTransition();
  {
   std::lock_guard<std::mutex> guard(s->mutex);
   if(!s->current(c.generation_) || !s->approved) return {"stale-source",{},{},false};
   s->retry=ticket;
  }
  const auto receipt=s->runtime.persist(*ticket,cancel);
  std::lock_guard<std::mutex> guard(s->mutex);
  if(!s->current(c.generation_) || !s->approved) return {"stale-source",receipt,{},false};
  auto events=s->runtime.deliver(*ticket,receipt,at); // CURRENT 5AD receipt/epoch/identity gate
  const auto snap=s->runtime.snapshot();
  const bool eligible=snap.world && snap.mapping && !snap.pending &&
   snap.world->world.coordinate_space.revision==ticket->candidate.worldRevision &&
   snap.mapping->mapping.mapping_revision==ticket->candidate.mappingRevision;
  if(eligible) s->retry.reset();
  return {eligible?"committed":"receipt-not-eligible",receipt,std::move(events),eligible};
 } catch(const std::exception& error) {
  // No guessed receipt on failure/cancellation, including post-rename uncertainty.
  // Retain exact ticket for the existing store's idempotent retry semantics.
  return {"persistence-failed",{},{},false,error.what()};
 }
}
void CommonWorldLiveIngress::stop(const Callback& c,std::int64_t at) {
 auto s=state_; std::lock_guard<std::mutex> guard(s->mutex);
 if(c.state_.lock()==s && s->current(c.generation_)) s->close(at);
}
void CommonWorldLiveIngress::sourceFault(const Callback& c,std::int64_t at) { stop(c,at); }
void CommonWorldLiveIngress::revokeOwner(std::int64_t at) {
 auto s=state_; std::lock_guard<std::mutex> guard(s->mutex);
 if(s->disposed) return;
 s->approved=false; s->attempted=true; s->retry.reset(); s->runtime.revokeWorld(at);
}
void CommonWorldLiveIngress::dispose() {
 auto s=state_; std::lock_guard<std::mutex> guard(s->mutex);
 if(s->disposed) return;
 s->disposed=true; s->active=false; s->approved=false; s->retry.reset(); s->runtime.dispose();
}
CommonWorldLiveSnapshot CommonWorldLiveIngress::snapshot() const {
 auto s=state_; std::lock_guard<std::mutex> guard(s->mutex);
 return {s->generation,s->active,s->approved,s->busy,s->disposed,s->runtime.snapshot()};
}
CommonWorldOperationResult CommonWorldLiveIngress::beginDryRunPublisherSession(const std::string& session,const std::string& clock,std::int64_t at) {
 auto s=state_; std::lock_guard<std::mutex> guard(s->mutex);
 if(s->disposed) return {"disposed",{}};
 if(s->busy) return {"backpressure",{}};
 return s->runtime.beginDryRunPublisherSession(session,clock,at);
}
}
