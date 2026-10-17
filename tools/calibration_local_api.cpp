#include "monaka_bridge/calibration_session.hpp"
#include "monaka_bridge/config_transaction.hpp"
#define NOMINMAX
#include <windows.h>
#include <condition_variable>
#include <iostream>
#include <thread>
#include <algorithm>

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
 // Dedicated machine process, one reader per inherited byte pipe. No mode
 // mutation: SetNamedPipeHandleState can fail while the parent's read is pending.
 HANDLE input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE);
 DWORD flags=0;
 if(GetFileType(input)!=FILE_TYPE_PIPE||GetFileType(output)!=FILE_TYPE_PIPE||
    !GetNamedPipeInfo(input,&flags,nullptr,nullptr,nullptr)||(flags&PIPE_TYPE_MESSAGE))return 2;
 auto ioHook=env.ioHook;auto io=[&](const char* point){if(ioHook)ioHook(point);};
 EventOutbox out;env.progress=[&](Json event){
  const bool progress=event.value("status",std::string{})=="PROGRESS";
  if(!out.push(std::move(event),progress,false,steadyNow()))throw std::runtime_error("OUTPUT_BACKPRESSURE");
 };CalibrationSession session(provider,std::move(env));
 std::mutex mutex;std::condition_variable changed;std::deque<Json> queue,reserved;std::optional<Json> inflight;
 std::atomic<bool> inputDone{false},workerDone{false},writerDone{false},stopWriter{false};std::atomic<unsigned> rejects{0};
 auto reject=[&](const char* reason){if(++rejects>=16){session.requestCancel(true);inputDone=true;}if(!out.push({{"status","REJECTED"},{"reason",reason}})){session.requestCancel(true);inputDone=true;}changed.notify_one();};
 std::thread writer([&]{
  while(!stopWriter){
   auto event=out.next();if(!event){if(out.failed()||workerDone)break;std::this_thread::sleep_for(std::chrono::milliseconds(5));continue;}
   auto bytes=event->dump()+"\n";std::size_t offset=0;
   while(offset<bytes.size()&&!stopWriter){
    DWORD done=0;
    io("writer-before-write"); // Test barrier deliberately after the stop check.
    if(!WriteFile(output,bytes.data()+offset,DWORD(bytes.size()-offset),&done,nullptr)){
     out.broken();session.requestCancel(true);inputDone=true;changed.notify_one();io("writer-failed");break;
    }
    offset+=done;if(!done)std::this_thread::sleep_for(std::chrono::milliseconds(5));
   }
   if(out.failed()||stopWriter)break;
   out.acknowledge(event->at("event_sequence").get<std::uint64_t>());
  }io("writer-finished");writerDone=true;
 });
 std::thread worker([&]{bool terminalSent=false;
  while(true){
   // This absolute lifetime applies even when the command queue never empties.
   if(session.lifetimeExpired()){session.tick();inputDone=true;break;}
   Json command;
   {std::unique_lock<std::mutex> lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(10),[&]{return inputDone||!reserved.empty()||!queue.empty()||out.failed();});
    if(out.failed()){session.requestCancel(true);inputDone=true;break;}
    if(!reserved.empty()){command=std::move(reserved.front());reserved.pop_front();inflight=command;}
    else if(!queue.empty()){command=std::move(queue.front());queue.pop_front();inflight=command;}
    else if(inputDone)break;
   }
   if(command.is_null()){session.tick();if(session.terminal()&&!terminalSent){out.push({{"status",session.phase()},{"phase",session.phase()},{"eligible",false},{"reason","SESSION_INVALIDATED"},{"session_id",session.sessionId()},{"run_id",session.runId()}},false,true);terminalSent=true;}continue;}
   if(!out.reserveControl()){session.requestCancel(true);inputDone=true;out.push({{"status","UNAVAILABLE"},{"reason","OUTPUT_BACKPRESSURE"}},false,true);break;}
   io("worker-before-execute");Json reply;try{reply=session.execute(command);}catch(const std::exception& e){reply={{"status","REJECTED"},{"reason",diagnostic(e)}};}
   reply["request_id"]=command.at("request_id");bool terminal=session.terminal()&&!terminalSent;out.publishReserved(std::move(reply),terminal);terminalSent=terminalSent||terminal;
   {std::lock_guard<std::mutex> lock(mutex);inflight.reset();}
   if(command.at("op")=="close")break;
  }
  session.tick();inputDone=true;workerDone=true;io("worker-stopped");
 });
 auto enqueue=[&](const std::string& line,bool oversized){
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
 };
 std::string line;bool oversized=false,lineStart=true;
 while(!inputDone&&!out.failed()){
  if(lineStart){lineStart=false;io("reader-line-start");if(inputDone||out.failed())break;}
  char bytes[4096];DWORD count=0,available=0;
  // There is no other IO on this handle in this process. Only this reader can
  // consume the peeked bytes; the peer may append or close, never remove them.
  if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)){session.requestCancel(true);inputDone=true;break;}
  if(!available){std::this_thread::sleep_for(std::chrono::milliseconds(5));continue;}
  if(!ReadFile(input,bytes,std::min<DWORD>(available,sizeof(bytes)),&count,nullptr)){session.requestCancel(true);inputDone=true;break;}
  if(!count){session.requestCancel(true);inputDone=true;break;}
  for(DWORD i=0;i<count&&!inputDone&&!out.failed();++i){
   if(lineStart){lineStart=false;io("reader-line-start");if(inputDone||out.failed())break;}
   if(bytes[i]=='\n'){
    if(!line.empty()&&line.back()=='\r')line.pop_back();enqueue(line,oversized);line.clear();oversized=false;lineStart=true;
   }else if(line.size()<64*KiB)line+=bytes[i];else oversized=true;
  }
 }
 inputDone=true;changed.notify_one();worker.join();
 const auto deadline=steadyNow()+2*second,cancelAt=deadline-100000000;
 while(WaitForSingleObject(writer.native_handle(),0)!=WAIT_OBJECT_0&&steadyNow()<deadline){
  if(steadyNow()>=cancelAt&&!stopWriter){out.broken();stopWriter=true;CancelSynchronousIo(writer.native_handle());io("writer-stop-requested");}
  // Repeated cancellation closes the check-to-WriteFile race: if this attempt
  // sees no pending IO, a later attempt cancels a write started after the latch.
  if(stopWriter)CancelSynchronousIo(writer.native_handle());
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
 if(WaitForSingleObject(writer.native_handle(),0)!=WAIT_OBJECT_0){
  // Never join an unproven blocking thread. This API owns the entire calibrator
  // worker process. Broken/undelivered commit responses remain UNKNOWN at parent.
  io("writer-deadline-expired");TerminateProcess(GetCurrentProcess(),3);std::terminate();
 }
 writer.join();return out.failed()?3:0;
}
} // namespace mb::local_calibration
