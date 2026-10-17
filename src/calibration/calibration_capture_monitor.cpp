#include "monaka_bridge/calibration_capture_monitor.hpp"
#include <algorithm>

namespace mb::local_calibration {
namespace {
void need(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
bool sameIdentity(const InputWitness& a,const InputWitness& b){return a.source==b.source&&a.device==b.device&&a.session==b.session&&a.clock==b.clock&&a.space==b.space&&a.convention==b.convention&&a.revision==b.revision&&a.peer==b.peer&&a.referenceSerial==b.referenceSerial&&a.referenceRole==b.referenceRole&&a.referenceGeneration==b.referenceGeneration&&a.standing==b.standing;}
}
bool CaptureMonitor::Counter::accept(std::int64_t sequence,const std::string& bytes){
 for(const auto& [s,b]:recent)if(s==sequence){need(b==bytes,"SEQUENCE_CONTENT_CONFLICT");return false;}
 if(high&&sequence<=*high)return false;
 high=sequence;recent.emplace_back(sequence,bytes);if(recent.size()>128)recent.pop_front();return true;
}
void CaptureMonitor::check(bool b,const char* reason){if(!b){fail(reason);throw std::runtime_error(reason);}}
void CaptureMonitor::fail(const std::string& reason){if(state_.fault.empty()){state_.fault=reason;++state_.invalidationEpoch;}}
CaptureMonitor::CaptureMonitor(const ProviderOpenOptions& options,const ProviderBatch& batch,std::int64_t now,std::int64_t created)
 :input_(batch.identity),created_(created),lastNow_(now){
 const auto& frozen=options.measurementTarget();const auto& b=frozen.target().binding;
 need(bool(options.capabilityProfile()),"EXPLICIT_DECLARATION_REQUIRED");declaration_=options.capabilityProfile()->inputDeclaration;
 need(input_.source==b.source&&input_.device==b.device&&input_.space==b.inputSpace&&input_.revision==b.inputRevision&&input_.convention==frozen.target().profile.convention&&!input_.session.empty()&&!input_.clock.empty()&&!input_.peer.empty()&&!input_.referenceSerial.empty()&&!input_.standing.empty()&&input_.referenceRole==options.referenceRole(),"DISCOVERY_WITNESS_MISMATCH");
 // This local state becomes the active monitor by move, never by reseeding.
 consume(batch,false,now);requireLive(now,true);
 need(poses_.high.has_value(),"FRESH_POSE_REQUIRED");input_.initialPoseFloor=*poses_.high;input_.initialStateFloor=states_.high;
}
void CaptureMonitor::tick(std::int64_t now){
 check(now>=lastNow_,"MONOTONIC_CLOCK_ROLLBACK");lastNow_=now;
 check(now-created_<20*60*second,"WORKER_TTL_EXPIRED");
 check(state_.lastValidPose>=0&&now-state_.lastValidPose<second&&state_.lastValidReference>=0&&now-state_.lastValidReference<second,"LIVENESS_GAP");
}
void CaptureMonitor::requireLive(std::int64_t now,bool fresh){
 if(!state_.fault.empty())throw std::runtime_error(state_.fault);
 now=std::max(now,lastNow_);tick(now);if(fresh)need(state_.poseValid&&state_.referenceValid&&now-state_.lastValidPose<=50000000&&now-state_.referenceAt<=50000000,"NOT_READY_STALE");
}
RecordingDelivery CaptureMonitor::consume(const ProviderBatch& b,bool recording,std::int64_t now){
 if(!state_.fault.empty())throw std::runtime_error(state_.fault);
 check(now>=lastNow_,"MONOTONIC_CLOCK_ROLLBACK");lastNow_=now;
 check(b.frames.size()<=128&&b.references.size()<=128,"PROVIDER_BATCH_LIMIT");
 check(sameIdentity(input_,b.identity)&&!b.eventOverflow,"INPUT_REFERENCE_FRAME_CONFLICT");check(!b.absent,"EXPLICIT_ABSENCE");
 check(b.referenceReceiptNs<=now&&b.referenceReceiptNs>=0&&(state_.referenceAt<0||b.referenceReceiptNs>=state_.referenceAt),"REFERENCE_RECEIPT_CLOCK_CHANGED");
 state_.referenceAt=b.referenceReceiptNs;state_.referenceValid=b.referenceValid;
 if(b.referenceValid&&now-b.referenceReceiptNs<=50000000){check(state_.lastValidReference<0||b.referenceReceiptNs-state_.lastValidReference<second,"REFERENCE_LIVENESS_GAP");state_.lastValidReference=b.referenceReceiptNs;}
 RecordingDelivery out;if(recording){out.values.identity=b.identity;out.values.referenceReceiptNs=b.referenceReceiptNs;out.values.referenceValid=b.referenceValid;}
 for(const auto& f:b.frames){
  auto handle=[&](const auto& p,bool pose){
   if(p.source_id!=input_.source||p.device_id!=input_.device)return;
   check(p.session_id==input_.session&&p.clock_id==input_.clock&&p.coordinate_space.id==input_.space&&p.coordinate_space.convention==input_.convention&&p.coordinate_space.revision==input_.revision&&f.peer==input_.peer,"SELECTED_LIFETIME_PEER_SPACE_CONFLICT");
   std::string raw;c1::Error error;check(c1::EncodeEnvelope(f.envelope,raw,error),"INVALID_PROVIDER_ENVELOPE");check(raw.size()<=4096,"PROVIDER_ENVELOPE_LIMIT");
   check(monaka::input::CapabilitySet(p.capabilities)==monaka::input::CapabilitySet(declaration_),"DECLARATION_SCOPE_CHANGED");
   bool accepted=false;try{accepted=(pose?poses_:states_).accept(p.sequence,raw);}catch(const std::exception& e){fail(diagnostic(e));throw;}
   if(!accepted)return;
   check(f.receiptNs>=0&&f.receiptNs<=now,"INVALID_RECEIPT_TIME");auto& receipt=pose?lastPoseReceipt_:lastStateReceipt_;check(f.receiptNs>=receipt,"RECEIPT_CLOCK_BACKWARD");receipt=f.receiptNs;
   const auto age=p.sent_at_ns-p.timestamp_ns;
   bool fresh=p.timestamp_ns>=0&&p.sent_at_ns>=p.timestamp_ns&&age<=50000000&&now-f.receiptNs<=50000000;
   bool eligible=true;using P=std::decay_t<decltype(p)>;
   if constexpr(std::is_same_v<P,c1::TrackerObservation>){
    eligible=fresh&&p.modality=="full"&&p.tracking_state=="tracked"&&p.validity.position&&p.validity.orientation;
    if(eligible){check(state_.lastValidPose<0||f.receiptNs-state_.lastValidPose<second,"SOURCE_LIVENESS_GAP");state_.lastValidPose=f.receiptNs;}state_.poseValid=eligible;
   }else check(p.presence!="absent","EXPLICIT_ABSENCE");
   if(recording){out.values.frames.push_back(f);out.eligible.push_back(eligible);}
  };
  if(auto p=std::get_if<c1::TrackerObservation>(&f.envelope))handle(*p,true);
  else if(auto p=std::get_if<c1::ObservationDeviceState>(&f.envelope))handle(*p,false);
 }
 if(recording)for(const auto& r:b.references){check(r.timeNs>=0&&r.timeNs<=now,"INVALID_REFERENCE_SAMPLE_TIME");out.values.references.push_back(r);out.referenceEligible.push_back(r.valid&&now-r.timeNs<=50000000);}
 state_.poseHigh=poses_.high.value_or(-1);state_.stateHigh=states_.high;
 out.received=++state_.received;tick(now);return out;
}

struct CaptureOwner::Context {
 CaptureProvider& provider;Clock clock;const std::atomic<bool>& cancel;const std::int64_t created;const std::string run;
 mutable std::mutex mutex;std::condition_variable changed;
 std::unique_ptr<CaptureMonitor> monitor;std::optional<InputWitness> witness;
 std::deque<RecordingDelivery> queue;std::optional<std::uint64_t> inflight;
 std::atomic<bool> stopping{false};bool active=true,discovering=true,recordRequested=false,recording=false,recordingStarted=false,captureDone=false;
 std::string error,shutdownFault;std::uint64_t preflightEpoch=0,generation=1,nextBarrier=0,pendingBarrier=0,ackedBarrier=0,usedBarrier=0,barrierEpoch=0,barrierReceived=0;
 std::string barrierRequest;std::uint64_t issued=0,acked=0,stopReceived=0,stopDelivery=0;
 std::size_t frames=0,references=0;std::thread::id owner;
 Context(CaptureProvider& p,Clock c,const std::atomic<bool>& stop,std::int64_t t,std::string r):provider(p),clock(std::move(c)),cancel(stop),created(t),run(std::move(r)){}
 void fail(const std::string& why){if(error.empty()){error=why;if(!monitor)++preflightEpoch;}if(monitor)monitor->fail(why);changed.notify_all();}
 void guard(){auto batch=provider.guard();auto now=clock();std::lock_guard<std::mutex> lock(mutex);if(active&&monitor)monitor->consume(batch,false,now);}
 void receive(const ProviderBatch& batch,std::uint64_t expected){
  {std::lock_guard<std::mutex> lock(mutex);
   if(!active||expected!=generation)return; // Do not even call a closed context's clock.
   if(std::this_thread::get_id()!=owner){fail("FOREIGN_PROVIDER_CALLBACK_THREAD");return;}
   if(!recording){fail("LATE_PROVIDER_CALLBACK");return;}
  }
  // Clock/provider callbacks always run outside the monitor mutex.
  auto t=clock();std::lock_guard<std::mutex> lock(mutex);
  if(!active||expected!=generation)return;
  if(cancel||stopping)throw std::runtime_error("CANCELLED");
  try{
   auto delivery=monitor->consume(batch,true,t);
   if(frames+delivery.values.frames.size()>128||references+delivery.values.references.size()>128){fail("RECORDING_QUEUE_OVERFLOW");throw std::runtime_error(error);}
   if(!delivery.values.frames.empty()||!delivery.values.references.empty()){
    frames+=delivery.values.frames.size();references+=delivery.values.references.size();delivery.delivery=++issued;queue.push_back(std::move(delivery));
   }changed.notify_all();
  }catch(const std::exception& e){fail(diagnostic(e));throw;}
 }
 void loop(){
  owner=std::this_thread::get_id();
  try{
   auto start=clock();auto batch=provider.discover(cancel);need(!cancel&&!stopping,"CANCELLED");auto t=clock();need(t-start<10*second,"DISCOVER_TIMEOUT");
   auto initial=std::make_unique<CaptureMonitor>(*provider.configuration(),batch,t,created);
   {std::lock_guard<std::mutex> lock(mutex);witness=initial->witness();monitor=std::move(initial);if(!error.empty())monitor->fail(error);discovering=false;}changed.notify_all();
   while(!stopping&&!cancel){
    bool capture=false;std::uint64_t gen=0;
    {std::unique_lock<std::mutex> lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(8),[&]{return stopping||cancel||recordRequested||pendingBarrier>ackedBarrier;});if(stopping||cancel)break;capture=recordRequested;recordRequested=false;if(capture){recording=true;captureDone=false;gen=generation;}}
    if(capture){
     auto self=shared_from_context();
     try{provider.capture([self,gen](const ProviderBatch& b){self->receive(b,gen);},cancel);}catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex);if(!cancel)fail(diagnostic(e));}
     {std::lock_guard<std::mutex> lock(mutex);recording=false;captureDone=true;stopReceived=monitor->snapshot().received;stopDelivery=issued;}changed.notify_all();
    }
    if(stopping||cancel)break;guard();
    {std::lock_guard<std::mutex> lock(mutex);if(pendingBarrier>ackedBarrier&&pendingBarrier>usedBarrier&&!cancel&&!stopping){ackedBarrier=pendingBarrier;barrierEpoch=monitor->snapshot().invalidationEpoch;barrierReceived=monitor->snapshot().received;}}changed.notify_all();
   }
  }catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex);fail(diagnostic(e));discovering=false;captureDone=true;}
  try{provider.shutdown();}catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex);shutdownFault=diagnostic(e);fail(shutdownFault);}
  changed.notify_all();
 }
 // Set once before thread start; callbacks retain this context, never Session.
 std::weak_ptr<Context> self;
 std::shared_ptr<Context> shared_from_context(){return self.lock();}
};
CaptureOwner::CaptureOwner(CaptureProvider& provider,Clock clock,const std::atomic<bool>& cancel,std::int64_t created,std::string run)
 :context_(std::make_shared<Context>(provider,std::move(clock),cancel,created,std::move(run))){context_->self=context_;thread_=std::thread([c=context_]{c->loop();});}
CaptureOwner::~CaptureOwner(){shutdown();}
InputWitness CaptureOwner::discover(){
 auto c=context_;std::unique_lock<std::mutex> lock(c->mutex);
 need(c->changed.wait_for(lock,std::chrono::seconds(10),[&]{return !c->discovering||c->cancel||c->stopping;}),"DISCOVER_TIMEOUT");
 need(!c->cancel&&!c->stopping,"CANCELLED");if(!c->error.empty())throw std::runtime_error(c->error);need(c->witness.has_value(),"DISCOVERY_WITNESS_MISMATCH");return *c->witness;
}
CaptureSnapshot CaptureOwner::snapshot()const{
 auto c=context_;std::lock_guard<std::mutex> lock(c->mutex);CaptureSnapshot s;if(c->monitor)s=c->monitor->snapshot();else s.invalidationEpoch=c->preflightEpoch;if(s.fault.empty())s.fault=c->error;
 s.shutdownFault=c->shutdownFault;s.deliveryIssued=c->issued;s.deliveryAcked=c->acked;s.framesOutstanding=c->frames;s.referencesOutstanding=c->references;s.captureDone=c->captureDone;s.stopReceived=c->stopReceived;s.stopDelivery=c->stopDelivery;return s;
}
CaptureSnapshot CaptureOwner::check(std::int64_t now,bool fresh){auto c=context_;std::lock_guard<std::mutex> lock(c->mutex);if(!c->error.empty())throw std::runtime_error(c->error);need(c->monitor!=nullptr,"MONITOR_NOT_DISCOVERED");c->monitor->requireLive(now,fresh);return c->monitor->snapshot();}
CaptureOwner::Barrier CaptureOwner::barrier(const std::string& request,std::int64_t maximumWait){
 need(!request.empty()&&maximumWait>0&&maximumWait<=10*second,"INPUT_BARRIER_DEADLINE");
 auto c=context_;std::unique_lock<std::mutex> lock(c->mutex);need(c->active&&!c->stopping&&!c->cancel,"CANCELLED_BARRIER");need(c->monitor!=nullptr,"MONITOR_NOT_DISCOVERED");
 if(!c->error.empty())throw std::runtime_error(c->error);
 auto id=++c->nextBarrier;c->pendingBarrier=id;c->barrierRequest=request;c->changed.notify_all();
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::nanoseconds(maximumWait);
 auto ready=[&]{return c->ackedBarrier==id||!c->error.empty()||c->cancel||c->stopping;};
 while(!ready()&&std::chrono::steady_clock::now()<deadline)c->changed.wait_until(lock,std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(10)));
 if(!ready()){c->usedBarrier=id;c->fail("INPUT_BARRIER_TIMEOUT");throw std::runtime_error("INPUT_BARRIER_TIMEOUT");}
 if(c->cancel||c->stopping){c->usedBarrier=std::max(c->usedBarrier,id);throw std::runtime_error("CANCELLED_BARRIER");}if(!c->error.empty())throw std::runtime_error(c->error);
 return {c->run,request,c->generation,id,c->barrierEpoch,c->barrierReceived};
}
CaptureOwner::Admission CaptureOwner::admit(const Barrier& b,std::int64_t now){
 auto c=context_;std::unique_lock<std::mutex> lock(c->mutex);
 need(c->active&&!c->stopping&&!c->cancel,"CANCELLED_BARRIER");need(b.run==c->run&&b.request==c->barrierRequest&&b.generation==c->generation&&b.id==c->ackedBarrier&&b.id==c->pendingBarrier&&b.id>c->usedBarrier,"STALE_INPUT_BARRIER");
 c->usedBarrier=b.id;need(c->monitor->snapshot().invalidationEpoch==b.epoch,"INVALIDATED_INPUT_BARRIER");
 if(!c->error.empty())throw std::runtime_error(c->error);c->monitor->requireLive(now,true);return Admission(std::move(lock));
}
void CaptureOwner::record(const std::function<void(const RecordingDelivery&)>& sink){
 auto c=context_;{std::lock_guard<std::mutex> lock(c->mutex);need(c->active&&!c->stopping&&!c->cancel&&!c->recordingStarted,"CAPTURE_STATE_REJECTED");c->recordingStarted=true;c->captureDone=false;c->recordRequested=true;}c->changed.notify_all();
 while(true){
  RecordingDelivery delivery;
  {std::unique_lock<std::mutex> lock(c->mutex);c->changed.wait_for(lock,std::chrono::milliseconds(10),[&]{return !c->queue.empty()||c->captureDone||!c->error.empty()||c->cancel||c->stopping;});
   need(!c->cancel&&!c->stopping,"CANCELLED");if(!c->error.empty())throw std::runtime_error(c->error);
   if(c->queue.empty()){if(c->captureDone){need(c->acked==c->stopDelivery&&!c->inflight,"RECORDING_DRAIN_INCOMPLETE");return;}continue;}
   need(!c->inflight,"RECORDING_DELIVERY_INFLIGHT");delivery=std::move(c->queue.front());c->queue.pop_front();c->inflight=delivery.delivery;
  }
  sink(delivery); // Worker-only recorder, outside all monitor/queue locks.
  {std::lock_guard<std::mutex> lock(c->mutex);need(c->inflight&&*c->inflight==delivery.delivery&&delivery.delivery==c->acked+1,"RECORDING_ACK_ORDER");c->frames-=delivery.values.frames.size();c->references-=delivery.values.references.size();c->acked=delivery.delivery;c->inflight.reset();}c->changed.notify_all();
 }
}
void CaptureOwner::invalidate(const std::string& reason){auto c=context_;std::lock_guard<std::mutex> lock(c->mutex);c->fail(reason);}
void CaptureOwner::shutdown(){
 auto c=context_;if(!c)return;{std::lock_guard<std::mutex> lock(c->mutex);if(c->active){c->active=false;++c->generation;}c->stopping=true;}c->changed.notify_all();
 if(thread_.joinable()){need(thread_.get_id()!=std::this_thread::get_id(),"OWNER_SHUTDOWN_REENTRY");thread_.join();}
}
} // namespace mb::local_calibration
