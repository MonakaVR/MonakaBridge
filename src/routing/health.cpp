#include "monaka_bridge/health.hpp"
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mb {
using J=nlohmann::json;
J healthSnapshot(const Bridge& bridge,std::int64_t now,std::int64_t unixMs){
 J status={{"policy",policyName(bridge.config().policy)},{"publisher_id",bridge.config().bridgeId},
  {"snapshot_unix_ms",unixMs},{"pose_stage","native_observation"},{"is_final_output",false},
  {"mapping_revision",bridge.config().revision},{"pose_timeout_ms",bridge.config().timeoutNs/1000000},{"malformed",bridge.registry.malformed},
  {"rejected",bridge.registry.rejected},{"collisions",bridge.registry.collisions},
  {"send_errors",bridge.fanout.errors},{"devices",J::array()}};
 for(const auto& [source,s]:bridge.registry.sources)for(const auto& [device,d]:s.devices){
  const auto binding=bridge.config().bindings.find({source,device});
  const bool fixedValid=d.fixedTime>=0, fixedFuture=fixedValid&&d.fixedTime>now;
  J position=nullptr,orientation=nullptr;
  if(d.pose&&d.pose->position)position=*d.pose->position;
  if(d.pose&&d.pose->orientation)orientation=*d.pose->orientation;
  status["devices"].push_back({
   {"publisher_id",bridge.config().bridgeId},{"source",source},{"device",device},
   {"space",d.space},{"convention",d.convention},{"revision",d.revision},
   {"coordinate_space",{{"id",d.space},{"convention",d.convention},{"revision",d.revision}}},
   {"pose_stage","native_observation"},{"is_final_output",false},
   {"fresh",bridge.registry.fresh({source,device},now)},
   {"tracker",binding==bridge.config().bindings.end()?"unmapped":binding->second.tracker},
   {"collision",s.collision||d.collision},
   {"tracking_state",d.pose?d.pose->tracking_state:(d.state?d.state->tracking_state:"unknown")},
   {"position_valid",bool(d.pose&&d.pose->validity.position)},
   {"orientation_valid",bool(d.pose&&d.pose->validity.orientation)},
   {"position",position},{"orientation_xyzw",orientation},
   {"has_pose",bool(d.pose)},{"absent",d.absent},{"fixed_time_valid",fixedValid},{"fixed_time_future",fixedFuture},
   {"pose_age_ms",fixedValid&&!fixedFuture?double(now-d.fixedTime)/1000000.0:-1.0},
   {"source_receive_age_ms",s.lastReceive>=0&&now>=s.lastReceive?double(now-s.lastReceive)/1000000.0:-1.0},
   {"pose_sequence",d.poseSequence},{"state_sequence",d.stateSequence}
  });
 }
 return status;
}
HealthWriter::HealthWriter(std::filesystem::path target,HealthFileOperations operations,HealthRetry retry)
 :target_(std::move(target)),temporary_(target_),operations_(std::move(operations)),retry_(retry){
 temporary_+=L".tmp";
 if(!retry_.attempts||retry_.attempts>20||retry_.delay.count()<0)throw std::invalid_argument("health retry bounds");
 if(!operations_.write)operations_.write=[](const auto& path,std::string_view bytes){
  std::ofstream out(path,std::ios::binary|std::ios::trunc);
  if(!out)return false;out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.flush();return bool(out);
 };
 if(!operations_.replace)operations_.replace=[](const auto& from,const auto& to){
#ifdef _WIN32
  return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
#else
  std::error_code ec;std::filesystem::rename(from,to,ec);return !ec;
#endif
 };
 worker_=std::thread(&HealthWriter::run,this);
}
HealthWriter::~HealthWriter(){stop();}
bool HealthWriter::publish(J value) noexcept {
 try{
  std::optional<J> incoming(std::move(value));
  {std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
   if(!lock||stop_){++dropped_;return false;}
   if(pending_)++dropped_;
   pending_.swap(incoming);
  } // Replaced snapshot destruction is outside the critical section.
  ready_.notify_one();return true;
 }catch(...){++dropped_;return false;}
}
bool HealthWriter::stopping(){std::lock_guard<std::mutex> lock(mutex_);return stop_;}
void HealthWriter::stop(){
 {std::lock_guard<std::mutex> lock(mutex_);stop_=true;pending_.reset();}
 ready_.notify_all();if(worker_.joinable())worker_.join();
}
void HealthWriter::run(){
 for(;;){
  std::optional<J> snapshot;
  {std::unique_lock<std::mutex> lock(mutex_);ready_.wait(lock,[&]{return stop_||pending_.has_value();});
   if(stop_)break;snapshot.swap(pending_);
  }
  bool output=false;
  try{output=operations_.write(temporary_,snapshot->dump(2));}catch(...){ }
  if(!output){++writeFailures_;continue;}
  for(unsigned attempt=0;attempt<retry_.attempts&&!stopping();++attempt){
   bool replaced=false;
   try{replaced=operations_.replace(temporary_,target_);}catch(...){ }
   if(replaced){++written_;break;}
   ++replaceFailures_;
   if(attempt+1<retry_.attempts){
    std::unique_lock<std::mutex> lock(mutex_);
    if(ready_.wait_for(lock,retry_.delay,[&]{return stop_;}))break;
   }
  }
 }
 // Temporary diagnostic data has no correctness role; leave cleanup on writer.
 std::error_code ec;std::filesystem::remove(temporary_,ec);
}
}
