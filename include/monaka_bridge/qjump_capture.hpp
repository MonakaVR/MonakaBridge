#pragma once
// Generic opt-in diagnostics. No product controls or tracking decisions.
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace qjump {
using J=nlohmann::json;
inline std::int64_t mono(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline std::int64_t wall(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
inline std::string id(){std::random_device r;const char* h="0123456789abcdef";std::string s;for(int i=0;i<32;++i){if(i==8||i==12||i==16||i==20)s+='-';s+=h[r()%16];}s[14]='4';s[19]='8';return s;}
inline bool durable(const std::filesystem::path& path,const std::vector<std::string>& lines){
#ifdef _WIN32
 auto* f=_wfopen(path.c_str(),L"wb");
#else
 auto* f=std::fopen(path.c_str(),"wb");
#endif
 if(!f)return false;bool ok=true;for(const auto& line:lines)if(std::fwrite(line.data(),1,line.size(),f)!=line.size()||std::fputc('\n',f)==EOF){ok=false;break;}if(std::fflush(f)!=0)ok=false;
#ifdef _WIN32
 if(_commit(_fileno(f))!=0)ok=false;
#else
 if(fsync(fileno(f))!=0)ok=false;
#endif
 return std::fclose(f)==0&&ok;
}
inline std::optional<double> angle(const std::array<double,4>& a,const std::array<double,4>& b){
 double aa=0,bb=0,d=0;for(int i=0;i<4;++i){if(!std::isfinite(a[i])||!std::isfinite(b[i]))return {};aa+=a[i]*a[i];bb+=b[i]*b[i];d+=a[i]*b[i];}
 if(aa<1e-24||bb<1e-24||!std::isfinite(aa*bb))return {};return 2*std::acos(std::min(1.,std::abs(d)/std::sqrt(aa*bb)))*180/3.14159265358979323846;
}
// Validate recursively before copying into collector-owned storage. Fixed cap
// bounds serialization, nested objects, strings and abnormal caller payloads.
inline bool bounded(const J& j,std::size_t& budget,int depth=0,std::size_t* encoded=nullptr){
 if(depth>8||!budget)return false;--budget;
 if(encoded){*encoded+=32;if(*encoded>12000)return false;}
 if(j.is_string()){const auto size=j.get_ref<const std::string&>().size();if(encoded)*encoded+=size*6;return size<=1024&&(!encoded||*encoded<=12000);}
 if(j.is_object()){if(j.size()>48)return false;for(auto i=j.begin();i!=j.end();++i){if(encoded)*encoded+=i.key().size()*6+4;if(i.key().size()>96||!bounded(i.value(),budget,depth+1,encoded))return false;}}
 else if(j.is_array()){if(j.size()>64)return false;for(const auto& v:j)if(!bounded(v,budget,depth+1,encoded))return false;}
 return true;
}
struct Sample {
 std::int64_t process_monotonic_ns=mono(),wall_clock_utc_ns=wall();
 std::string stream_id,stage,clock_domain="process.steady",publication_phase;
 J identity=J::object(),timestamps=J::object(),validity=J::object(),links=J::object(),context=J::object();
 std::optional<std::array<double,4>> q;
 std::optional<bool> wire_send_success;
};
struct Options {
 std::string component;
 J build_identity={{"status","unavailable"}};
 std::filesystem::path output_dir,coordination_dir;
 std::int64_t pre_ns=10000000000LL,post_ns=10000000000LL,persistent_ns=2000000000LL,sample_ns=50000000LL;
 double threshold_deg=45;
 std::size_t queue_count=512,ring_count=51200,ring_bytes=16*1024*1024,event_bytes=32*1024*1024,max_events=16;
 // Focused tests may block/fail this worker-only sink. Production uses files.
 std::function<bool(const std::filesystem::path&,const std::vector<std::string>&)> sink;
};
class Collector {
 struct Lane {std::optional<Sample> previous,reference,lifecycle_sample;std::int64_t retained=-1,step=0;bool persistent=false;std::string lifecycle;};
 struct Row {std::int64_t mono;std::string bytes;std::int64_t utc=0;std::string stream;bool usable=false;};
 struct Event {std::string event;std::vector<std::string> aliases;std::vector<Row> samples;std::vector<J> triggers;std::map<std::string,std::int64_t> saved;J lane_coverage=J::array();std::int64_t started=0,started_wall=0,deadline=0,trigger_sample=0,pre_start=0,closed=0,closed_wall=0,last_valid=0;std::size_t bytes=0;std::uint64_t drops=0;bool truncated=false,shutdown=false;};
 Options o_;std::string process_=id();std::filesystem::path run_;
 // Exactly one tracking/service-thread producer, one collector worker consumer.
 // Coordination is consumed by the worker, never a second sample producer.
 // Release/acquire ownership eliminates mutex-contention evidence drops.
 std::unique_ptr<std::optional<Sample>[]> slots_;
 alignas(64) std::atomic_size_t head_{0};
 alignas(64) std::atomic_size_t tail_{0};
 std::mutex wait_mutex_,writer_mutex_;std::condition_variable ready_,writer_ready_;
 std::optional<Event> pending_;std::atomic_bool stop_{false},writer_stop_{false};
 std::thread worker_,writer_;std::map<std::string,Lane> lanes_;std::set<std::string> entities_,seen_;
 std::deque<Row> ring_;std::size_t ring_bytes_=0;std::optional<Event> active_;std::size_t events_=0;std::int64_t started_wall_=wall();
 std::atomic_uint64_t dropped_{0},errors_{0},written_{0},offered_{0},max_ring_bytes_{0},max_event_bytes_{0};
 std::atomic_bool exhausted_{false};
 J base(const char* kind,std::int64_t utc=wall())const{return { {"schema","qjump.capture.v1"},{"kind",kind},{"component",o_.component},{"process_instance",process_},{"wall_clock_utc_ns",std::to_string(utc)} };}
 J sampleJson(const Sample& s)const{auto j=base("sample",s.wall_clock_utc_ns);j["process_monotonic_ns"]=std::to_string(s.process_monotonic_ns);j["clock_domain"]=s.clock_domain;j["stream_id"]=s.stream_id;j["stage"]=s.stage;j["identity"]=s.identity;j["timestamps"]=s.timestamps;j["validity"]=s.validity;j["links"]=s.links;j["context"]=s.context;j["q"]=s.q?J(*s.q):J(nullptr);if(!s.publication_phase.empty())j["publication_phase"]=s.publication_phase;if(s.wire_send_success)j["wire_send_success"]=*s.wire_send_success;return j;}
 std::string lifecycle(const Sample& s){J j;for(const char* k:{"session_id","sender_session_id","clock_id","space_id","space_revision","mapping_revision","coordinate_space"})if(s.identity.contains(k))j[k]=s.identity[k];for(const char* k:{"reset_generation","correction_epoch","assignment_generation","proxy_generation","topology_generation","policy","transform"})if(s.context.contains(k))j[k]=s.context[k];return j.dump();}
 std::size_t metadataReserve()const{return std::min<std::size_t>(o_.event_bytes/2,256*1024);}
 Row row(const Sample& s,bool boundary=false)const{auto j=sampleJson(s);if(boundary){j["boundary_record"]=true;j["boundary_reason"]="event_close_latest_actual";}return {s.process_monotonic_ns,j.dump(),s.wall_clock_utc_ns,s.stream_id,bool(s.q&&angle(*s.q,*s.q)&&s.validity.value("fresh",true)&&s.validity.value("rotation_valid",true))};}
 void append(Event& e,Row value){if(e.bytes+value.bytes.size()+1>o_.event_bytes-metadataReserve()){e.truncated=true;++dropped_;return;}e.bytes+=value.bytes.size()+1;if(!value.stream.empty())e.saved[value.stream]=std::max(e.saved[value.stream],value.mono);if(value.usable)e.last_valid=std::max(e.last_valid,value.mono);e.samples.push_back(std::move(value));max_event_bytes_.store(std::max<std::uint64_t>(max_event_bytes_.load(),e.bytes));}
 void request(const std::string& incident,const char* type){try{J r={{"schema","qjump.request.v1"},{"incident_id",incident},{"origin_component",o_.component},{"origin_process_instance",process_},{"wall_clock_utc_ns",std::to_string(wall())},{"trigger_type",type}};auto target=o_.coordination_dir/(incident+".json");auto temp=o_.coordination_dir/(incident+".tmp");std::ofstream f(temp,std::ios::binary|std::ios::trunc);f<<r.dump();f.close();if(!f)throw std::runtime_error("request write");std::filesystem::rename(temp,target);}catch(...){++errors_;}}
 void trigger(const char* type,const Sample* s,const Sample* before=nullptr,double deg=0,const std::string& remote={}){
  if(exhausted_&&!active_){++dropped_;return;}
  if(!active_){if(events_>=o_.max_events){exhausted_=true;++dropped_;return;}Event e;e.event=id();e.started=mono();e.started_wall=wall();e.deadline=e.started+o_.post_ns;e.trigger_sample=s?s->process_monotonic_ns:mono();e.drops=dropped_;e.aliases.push_back(remote.empty()?e.event:remote);for(const auto& row:ring_){if(!e.pre_start&&row.mono>=e.trigger_sample-o_.pre_ns-o_.sample_ns&&row.mono<=e.trigger_sample)e.pre_start=row.mono;append(e,row);}active_=std::move(e);++events_;if(events_>=o_.max_events)exhausted_=true;if(remote.empty())request(active_->event,type);}
  auto& e=*active_;if(!remote.empty()&&std::find(e.aliases.begin(),e.aliases.end(),remote)==e.aliases.end()){if(e.aliases.size()<128)e.aliases.push_back(remote);else{e.truncated=true;++dropped_;}}
  if(e.triggers.size()>=128){e.truncated=true;++dropped_;return;}
  auto t=base("trigger");t["event_id"]=e.event;t["incident_id"]=remote.empty()?e.event:remote;t["trigger_type"]=type;t["process_monotonic_ns"]=std::to_string(mono());if(s){t["identity"]=s->identity;t["stage"]=s->stage;t["sample_process_monotonic_ns"]=std::to_string(s->process_monotonic_ns);t["angular_distance_deg"]=deg;t["after"]=sampleJson(*s);if(before)t["before"]=sampleJson(*before);}else t["remote_request_received"]=true;auto size=t.dump().size();if(e.bytes+size>o_.event_bytes-metadataReserve()){e.truncated=true;++dropped_;return;}e.bytes+=size;e.triggers.push_back(std::move(t));if(before)append(e,row(*before));if(s)append(e,row(*s));
 }
 void consume(Sample s){
  if(!lanes_.count(s.stream_id)&&lanes_.size()>=256){++dropped_;return;}
  auto entity=s.identity.value("source_id","")+"\n"+s.identity.value("device_id",s.identity.value("tracker_id",s.identity.value("serial","")));if(!entities_.count(entity)&&entities_.size()>=16){++dropped_;return;}entities_.insert(entity);
  auto& lane=lanes_[s.stream_id];const auto life=lifecycle(s);if(!lane.lifecycle.empty()&&life!=lane.lifecycle){bool specific=false;if(lane.lifecycle_sample){for(const auto& group:std::vector<std::pair<const char*,std::vector<const char*>>>{{"T3",{"reset_generation","correction_epoch"}},{"T4",{"assignment_generation"}},{"T5",{"proxy_generation","topology_generation"}}}){bool changed=false;for(auto k:group.second)changed=changed||s.context.value(k,J())!=lane.lifecycle_sample->context.value(k,J());if(changed){trigger(group.first,&s,&*lane.lifecycle_sample);specific=true;}}}if(!specific)trigger("T6",&s,lane.lifecycle_sample?&*lane.lifecycle_sample:nullptr);lane.previous.reset();lane.reference.reset();}lane.lifecycle=life;lane.lifecycle_sample=s;
  const bool fresh=s.validity.value("fresh",true)&&s.validity.value("rotation_valid",true);
  if(fresh&&s.q&&angle(*s.q,*s.q)){
   if(lane.previous&&lane.previous->q){auto d=angle(*lane.previous->q,*s.q);if(d&&*d+1e-8>=o_.threshold_deg){trigger("T1",&s,&*lane.previous,*d);if(!lane.reference){lane.reference=lane.previous;lane.step=s.process_monotonic_ns;lane.persistent=false;}}}
   if(lane.reference&&lane.reference->q){auto d=angle(*lane.reference->q,*s.q);const bool gap=lane.previous&&s.process_monotonic_ns-lane.previous->process_monotonic_ns>250000000;bool progress=s.validity.value("persistence_fresh",true);if(lane.previous&&s.timestamps.contains("source_timestamp_ns")&&lane.previous->timestamps.contains("source_timestamp_ns"))progress=progress&&std::stoll(s.timestamps.at("source_timestamp_ns").get<std::string>())>std::stoll(lane.previous->timestamps.at("source_timestamp_ns").get<std::string>());if(!d||*d+1e-8<o_.threshold_deg||gap||!progress){lane.reference.reset();}else if(!lane.persistent&&s.process_monotonic_ns-lane.step>=o_.persistent_ns){trigger("T2",&s,&*lane.reference,*d);lane.persistent=true;}}
   lane.previous=s;
  }else{lane.previous.reset();lane.reference.reset();}
  if(lane.retained>=0&&s.process_monotonic_ns-lane.retained<o_.sample_ns)return;lane.retained=s.process_monotonic_ns;
  Row record=row(s);if(record.bytes.size()>16384){++dropped_;return;}
  while(!ring_.empty()&&(ring_.size()>=o_.ring_count||ring_bytes_+record.bytes.size()>o_.ring_bytes||(ring_.size()>1&&s.process_monotonic_ns-ring_[1].mono>o_.pre_ns))){ring_bytes_-=ring_.front().bytes.size();ring_.pop_front();}
  if(record.bytes.size()>o_.ring_bytes){++dropped_;return;}ring_bytes_+=record.bytes.size();ring_.push_back(record);max_ring_bytes_.store(std::max<std::uint64_t>(max_ring_bytes_,ring_bytes_));if(active_)append(*active_,std::move(record));
 }
 void complete(bool shutdown=false){if(!active_)return;auto& e=*active_;e.shutdown=shutdown;e.closed=mono();e.closed_wall=wall();
  // lifecycle_sample is the bounded latest actual offer, including invalid Q.
  // Preserve original observation clocks/identity; never synthesize a heartbeat.
  for(const auto& [stream,lane]:lanes_)if(lane.lifecycle_sample){const auto& latest=*lane.lifecycle_sample;auto saved=e.saved.find(stream);if(saved==e.saved.end()||saved->second<latest.process_monotonic_ns)append(e,row(latest,true));}
  std::stable_sort(e.samples.begin(),e.samples.end(),[](const Row& a,const Row& b){return a.mono<b.mono;});
  struct Coverage{std::int64_t first=0,last=0,firstUtc=0,lastUtc=0,lastValid=0;};std::map<std::string,Coverage> coverage;
  for(const auto& r:e.samples){auto& c=coverage[r.stream];if(!c.first){c.first=r.mono;c.firstUtc=r.utc;}c.last=r.mono;c.lastUtc=r.utc;if(r.usable)c.lastValid=r.mono;}
  std::size_t coverageBytes=0;for(const auto& [stream,c]:coverage){J value={{"stream_id",stream},{"first_sample_ns",std::to_string(c.first)},{"last_sample_ns",std::to_string(c.last)},{"first_sample_utc_ns",std::to_string(c.firstUtc)},{"last_sample_utc_ns",std::to_string(c.lastUtc)},{"post_valid_sample_present",c.lastValid>=e.trigger_sample+o_.post_ns-o_.sample_ns*2}};auto n=value.dump().size();if(coverageBytes+n>metadataReserve()-16384){e.truncated=true;++dropped_;break;}coverageBytes+=n;e.lane_coverage.push_back(std::move(value));}
  std::unique_lock<std::mutex> l(writer_mutex_,std::try_to_lock);if(!l||pending_){++dropped_;++errors_;active_.reset();return;}pending_=std::move(active_);active_.reset();l.unlock();writer_ready_.notify_one();}
 void poll(){try{std::size_t count=0;for(const auto& entry:std::filesystem::directory_iterator(o_.coordination_dir)){
   if(++count>256){++dropped_;break;}auto name=entry.path().filename().string();if(name.size()!=41||name.substr(36)!=".json")continue;bool valid=true;for(std::size_t i=0;i<36;++i)if(i==8||i==13||i==18||i==23){if(name[i]!='-')valid=false;}else if(!std::isxdigit(static_cast<unsigned char>(name[i])))valid=false;if(!valid||seen_.count(name))continue;
   if(seen_.size()>=256){++dropped_;break;}seen_.insert(name);if(entry.is_symlink()||!entry.is_regular_file()||entry.file_size()>4096){++errors_;continue;}
   std::ifstream f(entry.path(),std::ios::binary);std::array<char,4097> buffer{};f.read(buffer.data(),buffer.size());const auto n=f.gcount();if(n>4096){++errors_;continue;}auto j=J::parse(std::string(buffer.data(),static_cast<std::size_t>(n)));if(j.value("schema","")!="qjump.request.v1")continue;const auto incident=j.at("incident_id").get<std::string>();if(incident!=name.substr(0,36)){++errors_;continue;}const auto utc=std::stoll(j.at("wall_clock_utc_ns").get<std::string>());const auto age=wall()-utc;if(utc<started_wall_||age<0||age>30000000000LL)continue;if(j.value("origin_process_instance","")==process_)continue;const auto type=j.value("trigger_type","");if(type.size()!=2||type[0]!='T'||type[1]<'1'||type[1]>'6'){++errors_;continue;}std::size_t budget=32;if(!bounded(j,budget)){++errors_;continue;}trigger(type.c_str(),nullptr,nullptr,0,incident);if(active_&&!active_->triggers.empty()){if(active_->bytes+j.dump().size()<o_.event_bytes-metadataReserve()){active_->bytes+=j.dump().size();active_->triggers.back()["remote_request"]=j;}else{active_->truncated=true;++dropped_;}}
  }}catch(...){++errors_;}}
 void status(){try{auto j=base("status");j["state"]=exhausted_?"EXHAUSTED":"ARMED";j["dropped"]=std::to_string(dropped_);j["writer_errors"]=std::to_string(errors_);j["written"]=std::to_string(written_);j["max_ring_bytes"]=std::to_string(max_ring_bytes_);j["max_event_bytes"]=std::to_string(max_event_bytes_);auto temp=run_/"status.tmp";{std::ofstream f(temp,std::ios::binary|std::ios::trunc);f<<j.dump();}std::error_code ec;std::filesystem::remove(run_/"status.json",ec);std::filesystem::rename(temp,run_/"status.json");}catch(...){++errors_;}}
 void run(){auto next=mono(),nextStatus=mono();while(true){if(tail_.load(std::memory_order_relaxed)==head_.load(std::memory_order_acquire)){std::unique_lock<std::mutex> l(wait_mutex_);ready_.wait_for(l,std::chrono::milliseconds(10),[&]{return stop_||tail_.load(std::memory_order_relaxed)!=head_.load(std::memory_order_acquire);});}for(std::size_t count=0;count<o_.queue_count;++count){auto tail=tail_.load(std::memory_order_relaxed);if(tail==head_.load(std::memory_order_acquire))break;Sample s=std::move(*slots_[tail]);slots_[tail].reset();tail_.store((tail+1)%(o_.queue_count+1),std::memory_order_release);try{consume(std::move(s));}catch(...){++errors_;++dropped_;}}auto now=mono();if(now>=next){poll();next=now+100000000;}if(now>=nextStatus){status();nextStatus=now+1000000000;}if(active_&&now>=active_->deadline)complete();if(stop_&&tail_.load(std::memory_order_relaxed)==head_.load(std::memory_order_acquire))break;}complete(true);status();}
 void write(){for(;;){std::optional<Event> event;{std::unique_lock<std::mutex> l(writer_mutex_);writer_ready_.wait(l,[&]{return writer_stop_||pending_.has_value();});if(!pending_&&writer_stop_)break;event.swap(pending_);}try{
   auto& e=*event;std::vector<std::string> lines;auto h=base("header",e.started_wall);h["event_id"]=e.event;h["incident_ids"]=e.aliases;h["rate_hz"]=1000000000.0/static_cast<double>(o_.sample_ns);h["pre_ns"]=std::to_string(o_.pre_ns);h["post_ns"]=std::to_string(o_.post_ns);h["threshold_deg"]=o_.threshold_deg;h["ring_cap_bytes"]=std::to_string(o_.ring_bytes);h["event_cap_bytes"]=std::to_string(o_.event_bytes);h["max_events"]=std::to_string(o_.max_events);h["clock_meanings"]={{"process.steady","process-local steady_clock nanoseconds; never cross-process subtraction"}};h["source_build_identity"]=o_.build_identity;lines.push_back(h.dump());for(auto& r:e.samples)lines.push_back(std::move(r.bytes));for(auto& t:e.triggers)lines.push_back(t.dump());auto f=base("footer");const auto first=e.samples.empty()?0:e.samples.front().mono,last=e.samples.empty()?0:e.samples.back().mono;f["event_id"]=e.event;f["incident_ids"]=e.aliases;f["coverage_start_wall_clock_utc_ns"]=std::to_string(e.samples.empty()?e.started_wall:e.samples.front().utc);f["coverage_end_wall_clock_utc_ns"]=std::to_string(e.samples.empty()?e.started_wall:e.samples.back().utc);f["capture_window_start_wall_clock_utc_ns"]=std::to_string(e.started_wall);f["capture_window_end_wall_clock_utc_ns"]=std::to_string(e.closed_wall);f["lane_coverage"]=e.lane_coverage;f["coverage_start_ns"]=std::to_string(first);f["coverage_end_ns"]=std::to_string(last);f["prewindow_complete"]=e.pre_start>0&&e.trigger_sample-e.pre_start>=o_.pre_ns-o_.sample_ns;f["capture_window_start_ns"]=std::to_string(e.started);f["capture_window_end_ns"]=std::to_string(e.closed);f["sample_resolution_ns"]=std::to_string(o_.sample_ns);f["post_data_present"]=e.last_valid>=e.trigger_sample+o_.post_ns-o_.sample_ns*2;f["no_new_post_samples"]=last<=e.trigger_sample;f["sample_count"]=std::to_string(e.samples.size());f["dropped"]=std::to_string(dropped_);f["writer_errors"]=std::to_string(errors_);f["truncated"]=e.truncated;f["shutdown"]=e.shutdown;f["complete"]=!e.shutdown&&!e.truncated&&dropped_==0&&errors_==0&&e.pre_start>0&&e.trigger_sample-e.pre_start>=o_.pre_ns-o_.sample_ns;lines.push_back(f.dump());std::size_t bytes=0;for(const auto& line:lines)bytes+=line.size()+1;if(bytes>o_.event_bytes){++errors_;continue;}auto target=run_/(e.event+".jsonl");bool ok=false;if(o_.sink)ok=o_.sink(target,lines);else{auto tmp=target;tmp+=".incomplete";if(durable(tmp,lines)){std::filesystem::rename(tmp,target);ok=true;}}if(ok)++written_;else ++errors_;
  }catch(...){++errors_;}}}
public:
 explicit Collector(Options options):o_(std::move(options)){
  if(!o_.output_dir.is_absolute()||!o_.coordination_dir.is_absolute()||o_.component.size()>32||o_.ring_bytes<16384||o_.ring_bytes>16*1024*1024||o_.event_bytes<131072||o_.event_bytes>32*1024*1024||o_.max_events>16||!o_.max_events||!o_.queue_count||o_.queue_count>512||!o_.ring_count||o_.ring_count>51200||!std::isfinite(o_.threshold_deg)||o_.threshold_deg<=0||o_.threshold_deg>180||o_.pre_ns<=0||o_.post_ns<=0||o_.sample_ns<=0)throw std::invalid_argument("diagnostic bounds");
  slots_=std::make_unique<std::optional<Sample>[]>(o_.queue_count+1);run_=o_.output_dir/(o_.component+"-"+process_);std::filesystem::create_directories(run_);std::filesystem::create_directories(o_.coordination_dir);writer_=std::thread([this]{try{write();}catch(...){++errors_;}});try{worker_=std::thread([this]{try{run();}catch(...){++errors_;++dropped_;}});}catch(...){{std::lock_guard<std::mutex> l(writer_mutex_);writer_stop_=true;}writer_ready_.notify_all();writer_.join();throw;}
 }
 ~Collector(){stop();}
 Collector(const Collector&)=delete;Collector& operator=(const Collector&)=delete;
 static std::unique_ptr<Collector> fromEnvironment(const char* component,const char* enable,const char* output,J buildIdentity={{"status","unavailable"}}) noexcept {try{auto on=std::getenv(enable);if(!on||std::string(on)!="1")return {};auto dir=std::getenv(output),coord=std::getenv("QJUMP_CAPTURE_COORDINATION_DIR");if(!dir||!coord)return {};Options o;o.component=component;o.output_dir=dir;o.coordination_dir=coord;o.build_identity=std::move(buildIdentity);if(auto threshold=std::getenv("QJUMP_CAPTURE_STEP_DEGREES"))o.threshold_deg=std::stod(threshold);return std::make_unique<Collector>(std::move(o));}catch(...){return {};}}
 bool offer(Sample sample)noexcept{try{std::size_t budget=256,encoded=0;if(sample.stream_id.size()>512||sample.stage.size()>48||sample.clock_domain.size()>96||sample.publication_phase.size()>32||!bounded(sample.identity,budget,0,&encoded)||!bounded(sample.timestamps,budget,0,&encoded)||!bounded(sample.validity,budget,0,&encoded)||!bounded(sample.links,budget,0,&encoded)||!bounded(sample.context,budget,0,&encoded)){++dropped_;return false;}const auto head=head_.load(std::memory_order_relaxed),next=(head+1)%(o_.queue_count+1);if(stop_||next==tail_.load(std::memory_order_acquire)){++dropped_;return false;}slots_[head].emplace(std::move(sample));head_.store(next,std::memory_order_release);++offered_;ready_.notify_one();return true;}catch(...){++dropped_;return false;}}
 void stop(){if(stop_.exchange(true))return;ready_.notify_all();if(worker_.joinable())worker_.join();{std::lock_guard<std::mutex> l(writer_mutex_);writer_stop_=true;}writer_ready_.notify_all();if(writer_.joinable())writer_.join();}
 void noteDrop()noexcept{++dropped_;}
 bool exhausted()const{return exhausted_;}std::uint64_t dropped()const{return dropped_;}std::uint64_t errors()const{return errors_;}std::uint64_t written()const{return written_;}std::uint64_t maxRingBytes()const{return max_ring_bytes_;}std::uint64_t maxEventBytes()const{return max_event_bytes_;}const std::filesystem::path& directory()const{return run_;}
};
}
