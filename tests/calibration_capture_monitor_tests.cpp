#include "calibration_fixture_provider.hpp"
#include "monaka_bridge/calibration_capture_monitor.hpp"
#include <condition_variable>
#include <iostream>
#include <thread>

namespace lc=mb::local_calibration;
using namespace std::chrono_literals;
namespace {
struct Gate {
 std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
 void pause(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();if(!changed.wait_for(lock,5s,[&]{return released;}))throw std::runtime_error("FIXTURE_GATE_TIMEOUT");}
 void wait(){std::unique_lock<std::mutex> lock(mutex);CHECK(changed.wait_for(lock,5s,[&]{return entered;}));}
 void release(){std::lock_guard<std::mutex> lock(mutex);released=true;changed.notify_all();}
};
struct GuardedWorker {
 Gate& gate;std::thread thread;
 template<class F>GuardedWorker(Gate& g,F f):gate(g),thread(std::move(f)){}
 void join(){if(thread.joinable())thread.join();}
 ~GuardedWorker(){gate.release();join();}
};
template<class F>void until(F f){auto end=std::chrono::steady_clock::now()+5s;while(!f()){CHECK(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(1ms);}}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}CHECK(failed);}
struct Controlled final:fixture::Provider {
 using fixture::Provider::Provider;
 std::function<void(lc::ProviderBatch&)> onGuard;
 std::function<void()> onShutdown;
 std::function<void(const std::function<void(const lc::ProviderBatch&)>&,const std::atomic<bool>&)> onCapture;
 std::mutex retainedMutex;std::function<void(const lc::ProviderBatch&)> retained;
 std::atomic<unsigned> polls{0};
 lc::ProviderBatch guard()override{auto b=fixture::Provider::guard();if(onGuard)onGuard(b);++polls;return b;}
 void shutdown()override{if(onShutdown)onShutdown();}
 void capture(const std::function<void(const lc::ProviderBatch&)>& sink,const std::atomic<bool>& cancel)override{
  {std::lock_guard<std::mutex> lock(retainedMutex);retained=sink;}
  if(onCapture)onCapture(sink,cancel);else fixture::Provider::capture(sink,cancel);
 }
 auto callback(){std::lock_guard<std::mutex> lock(retainedMutex);return retained;}
 lc::ProviderBatch next(std::int64_t sequence){
  std::lock_guard<std::recursive_mutex> lock(fixtureMutex);time+=1000;
  auto p=sample("pico",sequence);p.orientation=mb::Quat{0,0,0,1};p.timestamp_ns=time-2000000;p.sent_at_ns=time;
  latest.frames={{p,"fixture-peer",time}};latest.referenceReceiptNs=time;latest.referenceValid=true;
  latest.references={{time-2000000,{},true}};return latest;
 }
};
void configure(fixture::Run& r,Controlled& p){CHECK(r.execute(r.open())["status"]=="OPEN");p.configure(r.provider.configuration());}
lc::ProviderBatch initial(fixture::Run& r){lc::ProviderBatch b;r.provider.edit([&]{b=r.provider.latest;});return b;}
int groups=0;
void pass(const char* text){++groups;std::cout<<"PASS N0b "<<text<<std::endl;}
}
int main(int argc,char** argv)try{
 CHECK(argc>=2);const auto base=std::filesystem::absolute(std::filesystem::u8path(argv[1]));
 if(argc==3){
  std::string mode=argv[2];fixture::Run r(base);
  if(mode=="solver-stall"){
   r.env.ioHook=[](const char* point){if(std::string(point)=="solve-before"){std::cout<<"WATCHDOG_READY solver"<<std::endl;std::this_thread::sleep_for(60s);}};r.reset();r.result();return 11;
  }
  Controlled p(r.time);configure(r,p);std::atomic<bool> cancel{false};std::atomic<bool> armed{false};
  if(mode=="owner-stall"){
   p.onGuard=[&](auto&){if(armed){std::cout<<"WATCHDOG_READY owner"<<std::endl;std::this_thread::sleep_for(60s);}};
   lc::CaptureOwner owner(p,r.env.clock,cancel,r.time,"child");owner.discover();armed=true;owner.barrier("stalled");return 12;
  }
  if(mode=="api-idle"){
   // The local API owns its separate Session; use the same immutable open data.
   // Configure once there, not in this fixture's resource-free preparatory Session.
   fixture::Run api(base);Controlled live(api.time);std::atomic<int> writes{0};
   live.onGuard=[&](auto& b){if(armed)b.absent=true;};
   api.env.ioHook=[&](const char* point){if(std::string(point)=="writer-before-write"&&++writes==3)armed=true;};
   std::cout<<lc::Json{{"test_only_fixture_open",api.open()}}.dump()<<std::endl;
   return lc::runLocalApi(live,api.env);
  }
  throw std::runtime_error("unknown child mode");
 }
 // One canonical detector, including the discovery set before ownership transfer.
 {fixture::Run r(base);CHECK(r.execute(r.open())["status"]=="OPEN");r.provider.discoverHook=[&]{auto value=std::get<mb::c1::TrackerObservation>(r.provider.latest.frames[0].envelope);value.position=mb::Vec{9,8,7};r.provider.latest.frames.push_back({value,"fixture-peer",r.time});};
  auto out=r.execute(r.command("discover"));CHECK(out["reason"]=="SEQUENCE_CONTENT_CONFLICT"&&r.session->phase()=="INSPECT_ONLY"&&!r.session->frozenInput()&&r.session->captureSnapshot().invalidationEpoch==1);
  r.provider.edit([&]{r.provider.latest.frames.resize(1);});r.provider.fresh();CHECK(r.execute(r.command("discover"))["reason"]=="INSPECT_ONLY_NEW_MEASUREMENT_REQUIRED");pass("Session discovery fault is sticky before handoff; new request cannot revive same run");
 }
 {fixture::Run r(base);CHECK(r.execute(r.open())["status"]=="OPEN");auto b=initial(r),bad=b;
  auto value=std::get<mb::c1::TrackerObservation>(b.frames[0].envelope);value.position=mb::Vec{9,8,7};bad.frames.push_back({value,"fixture-peer",r.time});
  rejects([&]{lc::CaptureMonitor m(*r.provider.configuration(),bad,r.time,r.time);});
  lc::CaptureMonitor monitor(*r.provider.configuration(),b,r.time,r.time);auto w=monitor.witness();CHECK(w.initialPoseFloor==0&&!w.initialStateFloor);
  auto first=monitor.snapshot();monitor.consume(b,false,r.time);CHECK(monitor.snapshot().poseHigh==0&&monitor.snapshot().invalidationEpoch==0&&monitor.snapshot().received==first.received+1);
  auto state=::state(std::get<mb::c1::TrackerObservation>(b.frames[0].envelope),77);b.frames={{state,"fixture-peer",r.time}};monitor.consume(b,false,r.time);
  CHECK(monitor.snapshot().stateHigh==77&&!monitor.witness().initialStateFloor);
  rejects([&]{monitor.consume(bad,false,r.time);});CHECK(monitor.snapshot().invalidationEpoch==1);
  rejects([&]{monitor.consume(initial(r),false,r.time);});CHECK(monitor.snapshot().invalidationEpoch==1);
  pass("handoff conflict before/after; immutable None floor; duplicates not reaccepted; sticky epoch exactly once");
 }
 {fixture::Run r(base);Controlled p(r.time);configure(r,p);std::atomic<bool> cancel{false};
  std::atomic<std::int64_t> seq{1};p.onGuard=[&](auto& b){b.frames.clear();for(int i=0;i<128;++i){auto n=++seq;r.time+=1000;auto value=sample("pico",n);value.orientation=mb::Quat{0,0,0,1};value.timestamp_ns=r.time-2000000;value.sent_at_ns=r.time;b.frames.push_back({value,"fixture-peer",r.time});}b.referenceReceiptNs=r.time;};
  lc::CaptureOwner owner(p,r.env.clock,cancel,r.time,"high-rate");auto w=owner.discover();
  for(int i=0;i<64;++i){auto ack=owner.barrier("commit-"+std::to_string(i));auto count=p.polls.load();until([&]{return p.polls>count;});{auto admission=owner.admit(ack,r.time);}rejects([&]{auto admission=owner.admit(ack,r.time);});}
  auto s=owner.snapshot();CHECK(s.poseHigh>8192&&s.invalidationEpoch==0&&w.initialPoseFloor==0);cancel=true;owner.shutdown();
  pass("128-per-poll high-rate barriers do not starve; ordinary samples preserve epoch; ACK single-use");
 }
 {fixture::Run r(base);Controlled p(r.time);configure(r,p);std::atomic<bool> cancel{false},fault{false};p.onGuard=[&](auto& b){if(fault)b.absent=true;};
  lc::CaptureOwner owner(p,r.env.clock,cancel,r.time,"barriers");owner.discover();auto old=owner.barrier("old"),current=owner.barrier("new");rejects([&]{auto a=owner.admit(old,r.time);});
  auto foreign=current;foreign.run="foreign";rejects([&]{auto a=owner.admit(foreign,r.time);});fault=true;until([&]{return !owner.snapshot().fault.empty();});rejects([&]{auto a=owner.admit(current,r.time);});CHECK(owner.snapshot().invalidationEpoch==1);
  cancel=true;rejects([&]{owner.barrier("cancelled");});owner.shutdown();
  pass("stale/foreign/cancelled barrier; fault between ACK and admission rejects");
 }
 for(bool timeout:{false,true}){
  fixture::Run r(base);Controlled p(r.time);configure(r,p);std::atomic<bool> cancel{false},armed{false};Gate blocked;
  p.onGuard=[&](auto&){if(armed)blocked.pause();};lc::CaptureOwner owner(p,r.env.clock,cancel,r.time,"pending");owner.discover();armed=true;std::string reason;
  GuardedWorker worker(blocked,[&]{try{owner.barrier("pending-request",timeout?50000000:lc::second);}catch(const std::exception& e){reason=e.what();}});
  blocked.wait();if(!timeout)cancel=true;worker.join();CHECK(reason==(timeout?"INPUT_BARRIER_TIMEOUT":"CANCELLED_BARRIER"));blocked.release();cancel=true;owner.shutdown();
  pass(timeout?"barrier absolute remaining budget expires once without ACK/retry":"cancellation observed while owner barrier is pending; no stale ACK issued");
 }
 {fixture::Run r(base);Controlled p(r.time);configure(r,p);std::atomic<bool> cancel{false};Gate inflight;std::atomic<bool> producing{false};
  p.onCapture=[&](const auto& sink,const auto&){sink(p.next(1));inflight.wait();for(int i=2;i<=129;++i)sink(p.next(i));};
  lc::CaptureOwner owner(p,r.env.clock,cancel,r.time,"overflow");owner.discover();std::string reason;
  GuardedWorker worker(inflight,[&]{try{owner.record([&](const auto&){inflight.pause();});}catch(const std::exception& e){reason=e.what();}});
  inflight.wait();until([&]{return owner.snapshot().fault=="RECORDING_QUEUE_OVERFLOW";});auto s=owner.snapshot();CHECK(s.framesOutstanding==128&&s.referencesOutstanding==128&&s.deliveryAcked==0&&s.deliveryIssued==128);
  inflight.release();worker.join();CHECK(reason=="RECORDING_QUEUE_OVERFLOW");cancel=true;owner.shutdown();
  pass("queue capacity includes blocked in-flight delivery; overflow sticky and cannot publish");
 }
 {fixture::Run r(base);Controlled p(r.time);configure(r,p);std::atomic<bool> cancel{false};Gate inflight;
  p.onCapture=[&](const auto& sink,const auto&){for(int i=1;i<=3;++i)sink(p.next(i));};lc::CaptureOwner owner(p,r.env.clock,cancel,r.time,"drain");owner.discover();
  std::vector<lc::RecordingDelivery> values;GuardedWorker worker(inflight,[&]{owner.record([&](const auto& d){if(values.empty())inflight.pause();CHECK(owner.snapshot().framesOutstanding>0);values.push_back(d);});});
  inflight.wait();until([&]{return owner.snapshot().captureDone;});auto before=owner.snapshot();CHECK(before.stopDelivery==3&&before.deliveryAcked==0&&before.framesOutstanding==3&&before.received>=before.stopReceived);
  r.time+=80000000;inflight.release();worker.join();auto after=owner.snapshot();CHECK(after.deliveryAcked==3&&after.framesOutstanding==0&&values.size()==3);
  for(const auto& d:values){CHECK(d.received<=before.stopReceived&&d.eligible[0]&&r.time-d.values.frames[0].receiptNs>=80000000);}
  cancel=true;owner.shutdown();pass("stop receive cutoff N distinct from delivery ACK; strict drain; canonical times/eligibility retained");
 }
 for(bool conflict:{false,true}){
  fixture::Run r(base);Gate boundary;std::atomic<bool> armed{false};r.env.ioHook=[&](const char* point){if(armed&&std::string(point)=="scope-before-read")boundary.pause();};r.reset();auto confirmed=r.confirmed();auto command=r.command("begin");command["confirmation_token"]=confirmed["confirmation_token"];armed=true;lc::Json out;
  GuardedWorker worker(boundary,[&]{out=r.execute(command);if(out.contains("reason"))std::cerr<<out.dump()<<std::endl;});boundary.wait();r.provider.edit([&]{if(conflict)std::get<mb::c1::TrackerObservation>(r.provider.latest.frames[0].envelope).position=mb::Vec{9,8,7};else r.provider.latest.absent=true;});
  until([&]{return !r.session->captureSnapshot().fault.empty();});r.provider.edit([&]{r.provider.latest.absent=false;});r.provider.fresh();boundary.release();worker.join();
  CHECK(out["status"]=="REJECTED"&&r.session->phase()=="INSPECT_ONLY"&&r.session->commitCalls()==0);
  pass(conflict?"worker scope read stall: owner detects content conflict before worker resumes":"worker scope read stall: owner detects absence; later fresh cannot clear fault");
 }
 for(const auto* phase:{"solve","review"}){
  fixture::Run r(base);Gate gate;std::atomic<bool> armed{false};r.env.ioHook=[&](const char* point){if(armed&&std::string(point)==(std::string(phase)=="solve"?"solve-before":"scope-before-read"))gate.pause();};r.reset();lc::Json command;
  if(std::string(phase)=="solve"){auto c=r.confirmed();command=r.command("begin");command["confirmation_token"]=c["confirmation_token"];}
  else{auto result=r.result();command=r.command("prepare_commit");command["result_id"]=result["result"]["result_id"];}
  armed=true;lc::Json out;GuardedWorker worker(gate,[&]{out=r.execute(command);if(out.contains("reason"))std::cerr<<out.dump()<<std::endl;});gate.wait();r.provider.edit([&]{r.provider.latest.absent=true;});until([&]{return !r.session->captureSnapshot().fault.empty();});r.provider.edit([&]{r.provider.latest.absent=false;});r.provider.fresh();gate.release();worker.join();CHECK(out["status"]=="REJECTED"&&r.session->phase()=="INSPECT_ONLY"&&r.session->commitCalls()==0);
  pass(std::string(phase)=="solve"?"solve stall continues independent monitoring; no result publication after fault":"review stall and later fresh cannot revive tokens/result eligibility");
 }
 for(bool fault:{false,true}){
  fixture::Run r(base);Gate ack;std::atomic<bool> armed{false};r.env.ioHook=[&](const char* point){if(armed&&std::string(point)=="precommit-barrier-acked")ack.pause();};r.reset();auto prepared=r.prepared();auto command=r.commit(prepared);armed=true;lc::Json out;
  GuardedWorker worker(ack,[&]{out=r.execute(command);if(out.contains("reason"))std::cerr<<out.dump()<<std::endl;});ack.wait();auto before=r.session->captureSnapshot();
  if(fault){r.provider.edit([&]{r.provider.latest.absent=true;});until([&]{return !r.session->captureSnapshot().fault.empty();});}
  else{r.provider.fresh();until([&]{return r.session->captureSnapshot().poseHigh>before.poseHigh;});CHECK(r.session->captureSnapshot().invalidationEpoch==before.invalidationEpoch);}
  ack.release();worker.join();CHECK(out["status"]==(fault?"REJECTED":"SAVED")&&r.session->commitCalls()==(fault?0:1));if(fault)CHECK(mb::readConfigBytes(r.path)==r.original);
  pass(fault?"ACK-to-COMMITTING known fault rejects before transaction":"ACK-to-COMMITTING ordinary sample admitted without epoch retry");
 }
 {fixture::Run r(base);r.env.transactionHook=[&](const char* stage){if(std::string(stage)=="before-replace"){r.provider.edit([&]{r.provider.latest.absent=true;});until([&]{return !r.session->captureSnapshot().fault.empty();});}};r.reset();auto p=r.prepared();auto out=r.execute(r.commit(p));CHECK(out["status"]=="SAVED_CONFIG_EVIDENCE_STALE"&&r.session->commitCalls()==1&&mb::readConfigBytes(r.path)!=r.original);pass("fault inside COMMITTING completes existing B2 transaction; saved stale, no new abort");}
 {fixture::Run r(base);auto result=r.result();auto config=mb::loadConfig(r.path);config.bindings.at({"pico","sibling"}).mount.translation={1,2,3};++config.revision;mb::saveConfig(r.path,config);auto j=r.command("prepare_commit");j["result_id"]=result["result"]["result_id"];auto out=r.execute(j);CHECK(out["status"]=="REJECTED"&&r.session->captureSnapshot().invalidationEpoch==1);pass("worker boundary rejects whole-group sourceScope drift and advances invalidation epoch");}
 {fixture::Run r(base);Controlled p(r.time);configure(r,p);std::atomic<bool> cancel{false},armed{false};lc::CaptureOwner* pointer=nullptr;
  p.onGuard=[&](auto&){if(armed){auto snapshot=pointer->snapshot();CHECK(snapshot.invalidationEpoch==0);pointer->shutdown();}};
  lc::CaptureOwner owner(p,r.env.clock,cancel,r.time,"reentry");pointer=&owner;owner.discover();armed=true;until([&]{return owner.snapshot().fault=="OWNER_SHUTDOWN_REENTRY";});cancel=true;owner.shutdown();pass("provider callback runs outside monitor mutex; shutdown reentry rejects and external join completes");
 }
 {fixture::Run r(base);Controlled p(r.time);configure(r,p);std::atomic<bool> cancel{false};std::thread::id discovered,closed;std::atomic<int> closes{0};lc::CaptureOwner* pointer=nullptr;
  p.discoverHook=[&]{discovered=std::this_thread::get_id();};p.onShutdown=[&]{closed=std::this_thread::get_id();CHECK(pointer->snapshot().invalidationEpoch==0);++closes;throw std::runtime_error("FIXTURE_SHUTDOWN_FAILURE");};
  lc::CaptureOwner owner(p,r.env.clock,cancel,r.time,"cleanup");pointer=&owner;owner.discover();cancel=true;owner.shutdown();owner.shutdown();CHECK(discovered==closed&&closed!=std::this_thread::get_id()&&closes==1&&owner.snapshot().shutdownFault=="FIXTURE_SHUTDOWN_FAILURE");
  pass("resource cleanup exactly once on discovery owner, outside mutex; shutdown fault retained");
 }
 {std::function<void(const lc::ProviderBatch&)> late;lc::ProviderBatch value;std::atomic<unsigned> clockCalls{0};
  {fixture::Run r(base);Controlled p(r.time);configure(r,p);std::atomic<bool> cancel{false};p.onCapture=[&](const auto& sink,const auto&){value=p.next(1);sink(value);};
   lc::CaptureOwner owner(p,[&]{++clockCalls;return r.time.load();},cancel,r.time,"old-generation");owner.discover();owner.record([](const auto&){});late=p.callback();cancel=true;owner.shutdown();}
  auto before=clockCalls.load();late(value);CHECK(clockCalls==before);pass("late callback after owner/provider/Session destruction retains closed context; no clock/Session access");
 }
 std::cout<<"PASS N0b continuous monitor "<<groups<<" groups; no SDK/socket/GUI/HIL"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
