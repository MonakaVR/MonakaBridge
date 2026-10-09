#include "monaka_bridge/diagnostic.hpp"
#include "diagnostic_build.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
namespace mb {
using Json=nlohmann::json;
namespace {std::string utc(){auto n=std::chrono::system_clock::now();auto t=std::chrono::system_clock::to_time_t(n);std::tm tm{};
#ifdef _WIN32
 gmtime_s(&tm,&t);
#else
 gmtime_r(&t,&tm);
#endif
 std::ostringstream s;s<<std::put_time(&tm,"%Y-%m-%dT%H:%M:%S")<<'.'<<std::setw(3)<<std::setfill('0')<<(std::chrono::duration_cast<std::chrono::milliseconds>(n.time_since_epoch()).count()%1000)<<'Z';return s.str();}
std::int64_t observationNs(){static const auto epoch=std::chrono::steady_clock::now();return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-epoch).count();}
}
DiagnosticWriter::Json DiagnosticWriter::build(){return {{"component_name","MonakaBridge"},{"version","0.1.0"},{"git_commit",MB_DIAG_COMMIT},{"git_branch_or_ref",MB_DIAG_REF},{"dirty_at_build",bool(MB_DIAG_DIRTY)},{"build_timestamp_utc",MB_DIAG_TIMESTAMP},{"binary_sha256",nullptr},{"build_system","CMake"}};}
DiagnosticWriter::DiagnosticWriter(Sink sink,std::size_t capacity):sink_(std::move(sink)),capacity_(capacity),criticalQueue_(capacity){if(capacity<2)throw std::invalid_argument("diagnostic queue too small");worker_=std::thread([this]{run();});}
DiagnosticWriter::~DiagnosticWriter(){close();}
void DiagnosticWriter::emit(std::string type,std::string source,Json identity,Json session,Json validity,Json details,bool critical,Json identities)noexcept{try{
 if(stopping_){++dropped_;if(critical)++criticalDropped_;return;}
 Json e={{"schema_version",1},{"timestamp_utc",utc()},{"monotonic_ns",observationNs()},{"monotonic_domain","provider-process"},{"component","MonakaBridge"},{"event_type",type},{"source",source},{"tracker_identity",identity},{"role",nullptr},{"session",session},{"validity",validity},{"build",nullptr},{"details",details}};
 if(!identities.is_null())e["identities"]=identities;if(type=="component_started")e["build"]=build();
 if(critical){auto head=criticalHead_.load(std::memory_order_relaxed);if(head-criticalTail_.load(std::memory_order_acquire)>=capacity_){++dropped_;++criticalDropped_;return;}criticalQueue_[head%capacity_]=std::move(e);criticalHead_.store(head+1,std::memory_order_release);wake_.notify_one();return;}
 std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock||queue_.size()>=capacity_){++dropped_;return;}queue_.push_back({std::move(e),false});wake_.notify_one();
 }catch(...){++dropped_;if(critical)++criticalDropped_;}}
void DiagnosticWriter::run(){std::uint64_t sequence=0;
 for(;;){Item item;{std::unique_lock<std::mutex> lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(2),[&]{return stopping_||!queue_.empty()||criticalTail_.load()<criticalHead_.load();});auto tail=criticalTail_.load();bool important=tail<criticalHead_.load(std::memory_order_acquire);
 if(!important&&queue_.empty()){if(stopping_)break;continue;}
 if(important&&(queue_.empty()||criticalQueue_[tail%capacity_]["monotonic_ns"]<=queue_.front().event["monotonic_ns"])){item={std::move(criticalQueue_[tail%capacity_]),true};criticalTail_.store(tail+1,std::memory_order_release);}else{item=std::move(queue_.front());queue_.pop_front();}}
 try{auto& e=item.event;e["sequence"]=sequence++;e["details"]["diagnostic_delivery_age_ns"]=observationNs()-e["monotonic_ns"].get<std::int64_t>();e["details"]["dropped_events"]=dropped_.load();e["details"]["critical_dropped_events"]=criticalDropped_.load();e["details"]["write_failures"]=writeFailures_.load();sink_(e);}catch(...){++writeFailures_;++dropped_;if(item.critical)++criticalDropped_;}
 }}
void DiagnosticWriter::close(){if(!worker_.joinable())return;{std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}wake_.notify_one();worker_.join();}
DiagnosticWriter::Json DiagnosticObserver::identity(const std::string& source,const std::string& device){return {{"domain","backend_device_id"},{"value",device},{"stable_scope",source},{"source",source}};}
DiagnosticWriter::Json DiagnosticObserver::session(const Lifetime& l){return {{"session_id",l.session},{"session_token",nullptr},{"state",l.connected?"connected":"disconnected"},{"generation",l.generation},{"peer",nullptr}};}
void DiagnosticObserver::disconnect(const std::string& source,Lifetime& l,const std::string& reason){l.connected=false;for(auto& [device,t]:l.trackers)if(t.registered){writer_.emit("tracker_unregistered",source,identity(source,device),session(l),nullptr,{{"reason",reason}});t.registered=false;}writer_.emit("session_disconnected",source,nullptr,session(l),nullptr,{{"reason",reason},{"boundary","source-observation"}});}
void DiagnosticObserver::admitted(const c1::Envelope& e,std::string_view,std::int64_t now)noexcept{try{
 auto p=std::get_if<c1::TrackerObservation>(&e);auto s=std::get_if<c1::ObservationDeviceState>(&e);if(!p&&!s)return;
 const auto source=p?p->source_id:s->source_id,device=p?p->device_id:s->device_id,sid=p?p->session_id:s->session_id;
 auto& l=sources_[source];if(l.session!=sid||!l.connected){if(l.connected)disconnect(source,l,"session_replaced");bool reconnect=!l.session.empty();l.session=sid;++l.generation;l.trackers.clear();writer_.emit(reconnect?"session_reconnecting":"session_connecting",source,nullptr,session(l));l.connected=true;writer_.emit("session_connected",source,nullptr,session(l));}l.last=now;
 auto& t=l.trackers[device];auto id=identity(source,device),ss=session(l);
 if(!t.registered){writer_.emit("tracker_discovered",source,id,ss);writer_.emit("tracker_registered",source,id,ss,nullptr,{{"registration_kind","observation-cache"},{"role_authoritative_available",false}});t.registered=true;}
 if(s){writer_.emit("tracker_updated",source,id,ss,nullptr,{{"presence",s->presence},{"tracking_state",s->tracking_state}});if(s->presence=="absent"||s->tracking_state=="disconnected"||s->tracking_state=="lost"){writer_.emit("tracker_unregistered",source,id,ss,nullptr,{{"reason","device_state"}});t.registered=false;}return;}
 Json v={{"pose_present",true},{"position_valid",p->validity.position},{"orientation_valid",p->validity.orientation},{"fully_valid",p->validity.position&&p->validity.orientation}};
 writer_.emit("pose_observed",source,id,ss,v,{{"input_sequence",p->sequence},{"validity_authority","Observation.validity"}},false);
 if(!t.firstPose){writer_.emit("first_pose",source,id,ss,v);t.firstPose=true;}
 if(p->validity.position&&!t.firstPosition){writer_.emit("first_position_valid",source,id,ss,v);t.firstPosition=true;}
 if(t.p!=p->validity.position)writer_.emit("position_valid_changed",source,id,ss,v);if(t.q!=p->validity.orientation)writer_.emit("orientation_valid_changed",source,id,ss,v);
 if(!t.p||!t.q||(*t.p&&*t.q)!=(p->validity.position&&p->validity.orientation))writer_.emit("fully_valid_changed",source,id,ss,v);t.p=p->validity.position;t.q=p->validity.orientation;
 }catch(...){writer_.emit("diagnostic_warning","component",nullptr,nullptr,nullptr,{{"reason","observer_exception"}});}}
void DiagnosticObserver::rejected(const c1::Envelope& e,std::string reason)noexcept{try{auto p=std::get_if<c1::TrackerObservation>(&e);auto s=std::get_if<c1::ObservationDeviceState>(&e);if(p||s)writer_.emit("diagnostic_warning",p?p->source_id:s->source_id,identity(p?p->source_id:s->source_id,p?p->device_id:s->device_id),nullptr,nullptr,{{"reason",reason},{"duplicate_observable",true}});}catch(...){}}
void DiagnosticObserver::tick(std::int64_t now,std::int64_t timeout)noexcept{try{for(auto& [source,l]:sources_)if(l.connected&&now>=l.last&&now-l.last>=timeout)disconnect(source,l,"observation_timeout");}catch(...){}}
void DiagnosticObserver::mapped(const std::string& source,const std::string& device,const std::string& tracker)noexcept{try{auto l=sources_.find(source);if(l==sources_.end())return;Json id={{"domain","protocol_tracker_id"},{"value",tracker},{"stable_scope",source},{"source",source}};writer_.emit("tracker_mapping",source,id,session(l->second),nullptr,{{"registration_kind","logical-output-mapping"}},true,Json::array({id,identity(source,device)}));}catch(...){}}
}
