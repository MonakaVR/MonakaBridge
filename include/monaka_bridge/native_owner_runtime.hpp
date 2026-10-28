#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace mb::native_owner {
// Lifecycle and current-state authority shared by product owner and fault test.
// The owner supplies the existing service loop; tests inject only its boundary.
class Runtime {
public:
 using Runner=std::function<void(const std::function<bool()>&,const std::function<void()>&)>;
private:
 Runner run_;std::function<void()> publish_;std::thread thread_;std::atomic_bool stop_{false};
 std::mutex operation_,state_;std::condition_variable changed_;bool running_=false,done_=true;std::string fault_;
 static void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
 void publish(){if(publish_)publish_();}
 void stopOwned(){
  stop_=true;std::unique_lock<std::mutex> lock(state_);
  need(changed_.wait_for(lock,std::chrono::seconds(5),[&]{return done_;}),"Runtime stop unconfirmed; owner retained");lock.unlock();if(thread_.joinable())thread_.join();
 }
 void startOwned(){
  {std::lock_guard<std::mutex> lock(state_);if(running_)return;need(done_,"Prior runtime has not exited; no replacement");}
  if(thread_.joinable())thread_.join();stop_=false;{std::lock_guard<std::mutex> lock(state_);done_=false;fault_.clear();}publish();
  thread_=std::thread([this]{
   try{run_([&]{return stop_.load();},[&]{{std::lock_guard<std::mutex> lock(state_);running_=true;}changed_.notify_all();publish();});}
   catch(const std::exception& e){std::lock_guard<std::mutex> lock(state_);fault_=e.what();}
   catch(...){std::lock_guard<std::mutex> lock(state_);fault_="Unknown worker failure";}
   {std::lock_guard<std::mutex> lock(state_);running_=false;done_=true;}changed_.notify_all();publish();
  });
  std::unique_lock<std::mutex> lock(state_);need(changed_.wait_for(lock,std::chrono::seconds(5),[&]{return running_||done_;}),"Runtime start unconfirmed; owner retained");if(!running_)throw std::runtime_error("Runtime failed: "+fault_);
 }
public:
 Runtime(Runner run,std::function<void()> publish={}):run_(std::move(run)),publish_(std::move(publish)){}
 ~Runtime(){finish();}
 void finish(){stop_=true;if(thread_.joinable())thread_.join();}
 std::string command(unsigned char op){std::lock_guard<std::mutex> operation(operation_);need(op>=1&&op<=3,"Unknown runtime operation");if(op!=1)stopOwned();if(op!=2)startOwned();return op==2?"STOPPED: native worker exit and sockets released":"RUNNING: native worker ready; tracking/output not verified";}
 std::string snapshot(){std::lock_guard<std::mutex> lock(state_);if(running_)return "RUNNING: native worker ready; tracking/output not verified";if(!done_)return "UNCONFIRMED: worker transition in progress";if(!fault_.empty())return "UNCONFIRMED: native worker exited with fault: "+fault_;return "STOPPED: native worker exit and sockets released";}
};
}
