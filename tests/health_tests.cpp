#include "helpers.hpp"
#include "monaka_bridge/health.hpp"
#include <future>
#include <fstream>
#include <iostream>
using namespace std::chrono_literals;
struct BlockedIo {
 std::promise<void> entered;
 std::promise<void> release;
 std::shared_future<void> resume=release.get_future().share();
 std::atomic_bool once{false};
 bool fail(){if(!once.exchange(true)){entered.set_value();resume.wait_for(5s);}return false;}
 ~BlockedIo(){try{release.set_value();}catch(...){}}
};
void track(mb::Bridge& bridge,mb::HealthWriter& writer,int first,int count){
 std::size_t outputs=0;
 for(int i=first;i<first+count;++i){
  auto p=sample("pico",i);p.timestamp_ns+=i*1000000LL;p.sent_at_ns=p.timestamp_ns+1000000;
  const auto now=10000000LL+i*1000000LL;
  CHECK(bridge.receive(wire(p),"publisher",now));bridge.tick(now);
  bridge.fanout.flush([&](mb::Channel channel,std::string_view bytes){
   if(channel==mb::Channel::Monaka){mb::c1::Envelope e;mb::c1::Error error;
    CHECK(mb::c1::DecodeEnvelope(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),e,error));
    if(auto pose=std::get_if<mb::c1::MtpPose>(&e)){CHECK(pose->input.sequence==i);++outputs;}
   }return true;
  },now);
  writer.publish({{"frame",i}});
 }
 CHECK(outputs==static_cast<std::size_t>(count));
}
int main(int argc,char** argv)try{
 CHECK(argc==2);const auto directory=std::filesystem::path(argv[1]);std::filesystem::create_directories(directory);
 // Both underlying file operations can block/fail while the real Bridge pipeline advances.
 for(bool replace : {false,true}){
  BlockedIo blocked;auto entered=blocked.entered.get_future();
  mb::HealthFileOperations ops;
  ops.write=[&](auto&,auto){return replace?true:blocked.fail();};
  ops.replace=[&](auto&,auto&){return blocked.fail();};
  mb::HealthWriter writer(directory/(replace?"replace.json":"write.json"),ops,{1,0ms});
  CHECK(writer.publish({{"frame",-1}}));CHECK(entered.wait_for(2s)==std::future_status::ready);
  mb::Bridge bridge(config(),SB);
  const auto start=std::chrono::steady_clock::now();track(bridge,writer,0,100);
  CHECK(std::chrono::steady_clock::now()-start<2s);CHECK(writer.dropped()>0);
  blocked.release.set_value();writer.stop();
  CHECK(replace?writer.replaceFailures()>0:writer.writeFailures()>0);
 }
 // Real retry wait is interruptible at shutdown and never held under publish's lock.
 std::promise<void> retryEntered;std::atomic_bool first{true};
 mb::HealthWriter retry(directory/"retry.json",{
  [](auto&,auto){return true;},[&](auto&,auto&){if(first.exchange(false))retryEntered.set_value();return false;}
 },{20,5s});
 retry.publish({{"frame",-1}});CHECK(retryEntered.get_future().wait_for(2s)==std::future_status::ready);
 mb::Bridge bridge(config(),SB);track(bridge,retry,0,100);
 const auto beforeStop=std::chrono::steady_clock::now();retry.stop();
 CHECK(std::chrono::steady_clock::now()-beforeStop<1s);CHECK(retry.replaceFailures()>0);
 CHECK(!retry.publish({{"after_stop",true}}));
 // While a snapshot is in-flight, only the newest pending snapshot survives.
 std::promise<void> writing,releaseWrite;auto resume=releaseWrite.get_future().share();
 std::vector<int> written;std::atomic_bool initial{true};
 mb::HealthWriter latest(directory/"latest.json",{
  [&](auto&,std::string_view bytes){written.push_back(nlohmann::json::parse(bytes).at("frame"));
   if(initial.exchange(false)){writing.set_value();resume.wait_for(5s);}return true;},
  [](auto&,auto&){return true;}
 });
 latest.publish({{"frame",0}});CHECK(writing.get_future().wait_for(2s)==std::future_status::ready);
 for(int i=1;i<=1000;++i)CHECK(latest.publish({{"frame",i}}));
 releaseWrite.set_value();
 const auto deadline=std::chrono::steady_clock::now()+2s;
 while(latest.written()<2&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
 latest.stop();CHECK((written==std::vector<int>{0,1000}));CHECK(latest.dropped()==999);
 // Default atomic replacement produces complete snapshots and does not refresh pose age.
 auto snapshot=mb::healthSnapshot(bridge,109000000,1234);auto& row=snapshot["devices"][0];
 CHECK(snapshot["pose_stage"]=="native_observation"&&!snapshot["is_final_output"].get<bool>());
 CHECK(row["coordinate_space"]["convention"]=="fixture-native");
 CHECK(row["position"]==*sample().position);CHECK(row["position_valid"]==true);
 CHECK(row["fresh"]==true);CHECK(row["publisher_id"]==config().bridgeId);
 auto stale=mb::healthSnapshot(bridge,1000000000,9999);
 CHECK(stale["devices"][0]["fresh"]==false);CHECK(stale["devices"][0]["position_valid"]==true);
 CHECK(stale["devices"][0]["pose_sequence"]==99);
 mb::HealthWriter disk(directory/"atomic.json");disk.publish(snapshot);
 const auto diskDeadline=std::chrono::steady_clock::now()+2s;
 while(disk.written()<1&&std::chrono::steady_clock::now()<diskDeadline)std::this_thread::yield();
 disk.stop();CHECK(disk.written()==1);std::ifstream in(directory/"atomic.json");nlohmann::json actual;in>>actual;CHECK(actual==snapshot);
 std::cout<<"PASS health: write failure / replace failure / retry keep tracking flowing; latest-only, shutdown, atomic native snapshot and stale validity\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
