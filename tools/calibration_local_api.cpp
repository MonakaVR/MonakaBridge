#include "monaka_bridge/calibration_session.hpp"
#include "monaka_bridge/config_transaction.hpp"
#define NOMINMAX
#include <windows.h>
#include <condition_variable>
#include <iostream>
#include <thread>

namespace mb::local_calibration {
bool EventOutbox::push(Json value,bool progress,bool terminal,std::int64_t now){
 std::lock_guard<std::mutex> lock(mutex_);if(broken_)return false;
 if(progress&&progressAt_>=0&&now-progressAt_<second/4)return true;
 if(terminal&&terminal_)return false;if(!progress&&!terminal&&controls_.size()+reserved_>=7)return false;
 value["event_sequence"]=++sequence_;
 if(progress){progressAt_=now;progress_=std::move(value);}else if(terminal)terminal_=std::move(value);else controls_.push_back(std::move(value));return true;
}
bool EventOutbox::reserveControl(){std::lock_guard<std::mutex> lock(mutex_);if(broken_||controls_.size()+reserved_>=7)return false;++reserved_;return true;}
bool EventOutbox::publishReserved(Json value,bool terminal){std::lock_guard<std::mutex> lock(mutex_);if(!reserved_)return false;--reserved_;if(broken_)return false;if(terminal&&terminal_)return false;value["event_sequence"]=++sequence_;if(terminal)terminal_=std::move(value);else controls_.push_back(std::move(value));return true;}
std::optional<Json> EventOutbox::next()const{
 std::lock_guard<std::mutex> lock(mutex_);if(broken_)return {};
 const Json* selected=nullptr;
 auto consider=[&](const Json& j){if(j.at("event_sequence").get<std::uint64_t>()>last_&&(!selected||j.at("event_sequence")<selected->at("event_sequence")))selected=&j;};
 if(!controls_.empty())consider(controls_.front());if(progress_)consider(*progress_);if(terminal_)consider(*terminal_);
 if(!selected)return {};auto out=*selected;out["previous_emitted_sequence"]=last_;return out;
}
void EventOutbox::acknowledge(std::uint64_t s){std::lock_guard<std::mutex> lock(mutex_);if(s<=last_)return;last_=s;if(!controls_.empty()&&controls_.front().at("event_sequence")==s)controls_.pop_front();if(progress_&&progress_->at("event_sequence")<=s)progress_.reset();if(terminal_&&terminal_->at("event_sequence")==s)terminal_.reset();}
void EventOutbox::broken(){std::lock_guard<std::mutex> lock(mutex_);broken_=true;}
bool EventOutbox::failed()const{std::lock_guard<std::mutex> lock(mutex_);return broken_;}
std::size_t EventOutbox::size()const{std::lock_guard<std::mutex> lock(mutex_);return controls_.size()+reserved_+std::size_t(progress_.has_value())+std::size_t(terminal_.has_value());}
bool acceptEventChain(const Json& j,std::uint64_t& last){auto seq=j.at("event_sequence").get<std::uint64_t>();if(seq<=last)return false;if(j.at("previous_emitted_sequence")!=last)throw std::runtime_error("EVENT_CHAIN_MISMATCH");last=seq;return true;}

int runLocalApi(CaptureProvider& provider,SessionEnvironment env){
 EventOutbox out;env.progress=[&](Json event){out.push(std::move(event),true,false,steadyNow());};CalibrationSession session(provider,std::move(env));
 HANDLE readerThread=nullptr;DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&readerThread,0,FALSE,DUPLICATE_SAME_ACCESS);
 std::mutex mutex;std::condition_variable changed;std::deque<Json> queue,reserved;std::optional<Json> inflight;
 std::atomic<bool> inputDone{false},workerDone{false},writerDone{false};std::atomic<unsigned> rejects{0};
 auto reject=[&](const char* reason){if(++rejects>=16){session.requestCancel(true);inputDone=true;}if(!out.push({{"status","REJECTED"},{"reason",reason}})){session.requestCancel(true);inputDone=true;}};
 std::thread writer([&]{
  HANDLE pipe=GetStdHandle(STD_OUTPUT_HANDLE);
  while(!workerDone||out.size()){
   auto event=out.next();if(!event){if(out.failed())break;std::this_thread::sleep_for(std::chrono::milliseconds(5));continue;}
   auto bytes=event->dump()+"\n";DWORD done=0;
   if(!WriteFile(pipe,bytes.data(),DWORD(bytes.size()),&done,nullptr)||done!=bytes.size()){out.broken();session.requestCancel(true);if(readerThread)CancelSynchronousIo(readerThread);break;}
   out.acknowledge(event->at("event_sequence").get<std::uint64_t>());
  }writerDone=true;
 });
 std::thread worker([&]{bool terminalSent=false;
  while(true){
   Json command;
   {std::unique_lock<std::mutex> lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(10),[&]{return inputDone||!reserved.empty()||!queue.empty()||out.failed();});
    if(out.failed()){session.requestCancel(true);break;}
    if(!reserved.empty()){command=std::move(reserved.front());reserved.pop_front();inflight=command;}
    else if(!queue.empty()){command=std::move(queue.front());queue.pop_front();inflight=command;}
    else if(inputDone)break;
   }
   if(command.is_null()){session.tick();if(session.lifetimeExpired()){inputDone=true;if(readerThread)CancelSynchronousIo(readerThread);break;}continue;}
   // Reserve output capacity before an action with side effects.
   if(!out.reserveControl()){session.requestCancel(true);inputDone=true;if(readerThread)CancelSynchronousIo(readerThread);out.push({{"status","UNAVAILABLE"},{"reason","OUTPUT_BACKPRESSURE"}},false,true);break;}
   Json reply;try{reply=session.execute(command);}catch(const std::exception& e){reply={{"status","REJECTED"},{"reason",diagnostic(e)}};}
   reply["request_id"]=command.at("request_id");bool terminal=session.terminal()&&!terminalSent;out.publishReserved(std::move(reply),terminal);terminalSent=terminalSent||terminal;
   {std::lock_guard<std::mutex> lock(mutex);inflight.reset();}
   if(command.at("op")=="close")break;
  }
  session.tick();workerDone=true;
 });
 while(!inputDone&&!out.failed()){
  std::string line;bool oversized=false;char c;
  while(std::cin.get(c)&&c!='\n'){if(line.size()<64*KiB)line+=c;else oversized=true;}
  if(!std::cin){session.requestCancel(true);inputDone=true;break;}
  if(!line.empty()&&line.back()=='\r')line.pop_back();
  try{
   if(oversized)throw std::runtime_error("LINE_LIMIT");auto request=parseRequest(line);bool control=request.at("op")=="cancel"||request.at("op")=="close";
   std::lock_guard<std::mutex> lock(mutex);auto& q=control?reserved:queue;
   bool duplicate=false;
   if(inflight&&inflight->at("request_id")==request.at("request_id")){
    if(inflight->dump()!=request.dump())throw std::runtime_error("REQUEST_ID_REUSE_MISMATCH");duplicate=true;
   }
   for(const auto* list:{&queue,&reserved})for(const auto& pending:*list)if(pending.at("request_id")==request.at("request_id")){
    if(pending.dump()!=request.dump())throw std::runtime_error("REQUEST_ID_REUSE_MISMATCH");duplicate=true;
   }
   if(duplicate){if(!out.push({{"status","IN_PROGRESS"},{"request_id",request.at("request_id")}})){inputDone=true;session.requestCancel(true);}}
   else if(q.size()>=(control?2:8)){if(!out.push({{"status","REJECTED"},{"reason","BUSY"},{"request_id",request.at("request_id")}})){inputDone=true;session.requestCancel(true);}}
   else{if(control)session.latchControl(request);bool close=request.at("op")=="close";q.push_back(std::move(request));if(close)inputDone=true;}changed.notify_one();
  }catch(const std::exception& e){auto reason=diagnostic(e);reject(reason.c_str());}
 }
 inputDone=true;changed.notify_one();worker.join();
 const auto deadline=steadyNow()+2*second;
 while(!writerDone&&steadyNow()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
 if(!writerDone){out.broken();CancelSynchronousIo(writer.native_handle());}
 writer.join();if(readerThread)CloseHandle(readerThread);return out.failed()?3:0;
}
int calibrationLocalDispatch(int argc,char** argv){
 if(argc!=2)return -1;std::string mode=argv[1];
 if(mode=="--local-api-version"){
  std::cout<<Json{{"api_version",1},{"software_policy",1},{"capture_supported",false},{"static_supported",false},{"production_capture","B3_REQUIRED"},{"output_approval",false}}.dump()<<'\n';return 0;
 }
 if(mode!="--local-api")return -1;
 try{
  wchar_t path[32768];DWORD length=GetModuleFileNameW(nullptr,path,32768);if(!length||length==32768)throw std::runtime_error("HELPER_PATH_UNAVAILABLE");
  wchar_t local[32768];DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);if(!n||n>=32768)throw std::runtime_error("OWNER_APPDATA_UNAVAILABLE");
  SessionEnvironment env;env.draftRoot=std::filesystem::path(local)/"MonakaBridge"/"CalibrationDrafts";env.helperPath=std::filesystem::path(path).u8string();env.helperSha256=sha256(readConfigBytes(std::filesystem::path(path)));
  UnsupportedProvider provider;return runLocalApi(provider,std::move(env));
 }catch(const std::exception& e){std::cout<<Json{{"status","UNAVAILABLE"},{"reason",diagnostic(e)}}.dump()<<'\n';return 2;}
}
} // namespace mb::local_calibration
